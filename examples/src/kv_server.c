#include <math.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#include "quiet-lwip-portaudio.h"

#include "lwip/sockets.h"

const int local_port = 7173;

const uint8_t mac[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x07};
const quiet_lwip_ipv4_addr ipaddr = (uint32_t)0xc0a80008;   // 192.168.0.8
const char *ipaddr_s = "192.168.0.8";
const quiet_lwip_ipv4_addr netmask = (uint32_t)0xffffff00;  // 255.255.255.0
const quiet_lwip_ipv4_addr gateway = (uint32_t)0xc0a80001;  // 192.168.0.1

const char *recv_ping_str = "PING";
const char *send_ping_str = "PONG";

const char *recv_add_str = "ADD:";
const char *send_add_str = "ADDED";
const uint8_t add_delim = '=';

const char *recv_get_str = "GET:";
const char *get_notfound = " ** NOT FOUND **";

int open_recv(const char *addr) {
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (socket_fd < 0) {
        printf("socket failed\n");
        return -1;
    }

    struct sockaddr_in *local_addr = calloc(1, sizeof(struct sockaddr_in));
    local_addr->sin_family = AF_INET;
    local_addr->sin_addr.s_addr = inet_addr(addr);
    local_addr->sin_port = htons(local_port);

    int res = bind(socket_fd, (struct sockaddr *)local_addr, sizeof(struct sockaddr_in));
    free(local_addr);

    if (res < 0) {
        printf("bind failed\n");
        return -1;
    }

    res = listen(socket_fd, 1);

    if (res < 0) {
        printf("listen failed\n");
        return -1;
    }

    return socket_fd;
}

int recv_connection(int socket_fd, struct sockaddr_in *recv_from) {

    socklen_t recv_from_len = sizeof(recv_from);
    return accept(socket_fd, (struct sockaddr *)recv_from, &recv_from_len);
}

const size_t kv_num_items_init = 8;
const size_t key_len = 32;
const size_t value_len = 32;

typedef struct {
    uint8_t *key;
    uint8_t *value;
} kv_t;

int kv_compare(const void *lkv, const void *rkv) {
    const kv_t *l = (const kv_t*)lkv;
    const kv_t *r = (const kv_t*)rkv;

    return memcmp(l->key, r->key, key_len);
}

typedef struct {
    kv_t *kv;
    size_t num_items;
    size_t cap;
} key_value_t;

key_value_t *key_value_init() {
    key_value_t *kv = calloc(1, sizeof(key_value_t));
    kv->kv = calloc(kv_num_items_init, sizeof(kv_t));
    for (size_t i = 0; i < kv_num_items_init; i++) {
        kv->kv[i].key = calloc(key_len, sizeof(uint8_t));
        kv->kv[i].value = calloc(value_len, sizeof(uint8_t));
    }
    kv->num_items = 0;
    kv->cap = kv_num_items_init;
    return kv;
}

const kv_t *key_value_find(key_value_t *kv, const kv_t *k) {
    void *found = bsearch(k, kv->kv, kv->num_items, sizeof(kv_t), kv_compare);

    if (!found) {
        return NULL;
    }

    return (kv_t*)found;
}

void key_value_add(key_value_t *kv, const kv_t *pair) {
    const kv_t *found = key_value_find(kv, pair);
    if (found) {
        ptrdiff_t offset = found - kv->kv;
        kv_t *found_mut = kv->kv + offset;
        memcpy(found_mut->value, pair->value, value_len - 1);
        return;
    }

    if (kv->num_items == kv->cap) {
        // resize
        kv->kv = realloc(kv->kv, kv->cap * 2 * sizeof(kv_t));
        for (size_t i = kv->cap; i < 2 * kv->cap; i++) {
            kv->kv[i].key = calloc(key_len, sizeof(uint8_t));
            kv->kv[i].value = calloc(value_len, sizeof(uint8_t));
        }
        kv->cap *= 2;
    }

    memcpy(kv->kv[kv->num_items].key, pair->key, key_len - 1);
    memcpy(kv->kv[kv->num_items].value, pair->value, value_len - 1);
    kv->num_items++;

    qsort(kv->kv, kv->num_items, sizeof(kv_t), kv_compare);

}

void key_value_print(key_value_t *kv) {
    printf("key value contents:\n");
    for (size_t i = 0; i < kv->num_items; i++) {
        kv_t pair = kv->kv[i];
        printf("%.*s: %.*s\n", (int)key_len, pair.key, (int)value_len, pair.value);
    }
    printf("\n");
}

void key_value_destroy(key_value_t *kv) {
    for (size_t i = 0; i < kv->cap; i++) {
        free(kv->kv[i].value);
        free(kv->kv[i].key);
    }
    free(kv->kv);
    free(kv);
}

int main(int argc, char **argv) {
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        printf("failed to initialize port audio, %s\n", Pa_GetErrorText(err));
        return 1;
    }

    quiet_lwip_portaudio_driver_config *conf =
        calloc(1, sizeof(quiet_lwip_portaudio_driver_config));
    const char *encoder_key = "audible-7k-channel-1";
    const char *decoder_key = "audible-7k-channel-0";
    const char *fname = "/usr/local/share/quiet/quiet-profiles.json";
    conf->encoder_opt =
        quiet_encoder_profile_filename(fname, encoder_key);
    conf->decoder_opt =
        quiet_decoder_profile_filename(fname, decoder_key);

    conf->encoder_device = Pa_GetDefaultOutputDevice();
    const PaDeviceInfo *device_info = Pa_GetDeviceInfo(conf->encoder_device);
    conf->encoder_sample_rate = device_info->defaultSampleRate;
    conf->encoder_latency = device_info->defaultLowOutputLatency;

    conf->decoder_device = Pa_GetDefaultInputDevice();
    device_info = Pa_GetDeviceInfo(conf->decoder_device);
    conf->decoder_sample_rate = device_info->defaultSampleRate;
    conf->decoder_latency = device_info->defaultLowOutputLatency;

    conf->encoder_sample_size = 1 << 8;
    conf->decoder_sample_size = 1 << 8;

    memcpy(conf->hardware_addr, mac, 6);
    quiet_lwip_portaudio_interface *interface =
        quiet_lwip_portaudio_create(conf, htonl(ipaddr), htonl(netmask), htonl(gateway));
    free(conf);

    quiet_lwip_portaudio_audio_threads *audio_threads =
        quiet_lwip_portaudio_start_audio_threads(interface);

    key_value_t *kv = key_value_init();
    kv_t temp_kv;
    temp_kv.key = calloc(key_len, sizeof(uint8_t));
    temp_kv.value = calloc(value_len, sizeof(uint8_t));

    int recv_socket = open_recv(ipaddr_s);

    size_t buf_len = 4096;
    uint8_t *buf = calloc(buf_len, sizeof(uint8_t));

    struct sockaddr_in recv_from;
    int conn_fd = recv_connection(recv_socket, &recv_from);
    printf("received connection from %s\n", inet_ntoa(recv_from.sin_addr.s_addr));

    for (;;) {
        ssize_t recv_len = read(conn_fd, buf, buf_len);
        if (recv_len) {
            printf("received packet from %s: %.*s\n", inet_ntoa(recv_from.sin_addr.s_addr), (int)recv_len, buf);
        } else {
            close(conn_fd);
            conn_fd = recv_connection(recv_socket, &recv_from);
            printf("received connection from %s\n", inet_ntoa(recv_from.sin_addr.s_addr));
            continue;
        }

        if (recv_len < 0) {
            printf("read from connection failed\n");
        }

        if (memcmp(buf, recv_ping_str, strlen(recv_ping_str)) == 0) {
            printf("sending response to %s\n", inet_ntoa(recv_from.sin_addr.s_addr));
            memset(buf, 0, buf_len);
            memcpy(buf, send_ping_str, strlen(send_ping_str));
            write(conn_fd, buf, strlen(send_ping_str));
        } else if (memcmp(buf, recv_add_str, strlen(recv_add_str)) == 0) {
            ptrdiff_t key_offset = strlen(recv_add_str);
            size_t remaining = recv_len - key_offset;
            ptrdiff_t value_offset = 0;
            for (size_t i = 0; i < (remaining - 1) && i < key_len; i++) {
                if (buf[key_offset + i] == add_delim) {
                    value_offset = key_offset + i + 1;
                    break;
                }
            }

            if (value_offset) {
                memset(temp_kv.key, 0, key_len * sizeof(uint8_t));
                memset(temp_kv.value, 0, value_len * sizeof(uint8_t));
                memcpy(temp_kv.key, buf + key_offset, value_offset - key_offset - 1);
                memcpy(temp_kv.value, buf + value_offset, recv_len - value_offset);
                key_value_add(kv, &temp_kv);
                key_value_print(kv);

                memset(buf, 0, buf_len);
                memcpy(buf, send_add_str, strlen(send_add_str));
                write(conn_fd, buf, strlen(send_add_str));
            }
        } else if (memcmp(buf, recv_get_str, strlen(recv_get_str)) == 0) {
            ptrdiff_t key_offset = strlen(recv_get_str);
            memset(temp_kv.key, 0, key_len * sizeof(uint8_t));
            memcpy(temp_kv.key, buf + key_offset, recv_len - key_offset);

            const kv_t *found = key_value_find(kv, &temp_kv);
            if (found) {
                write(conn_fd, found->value, value_len);
            } else {
                write(conn_fd, get_notfound, strlen(get_notfound));
            }
        }

    }

    quiet_lwip_portaudio_stop_audio_threads(audio_threads);
    key_value_destroy(kv);
    free(buf);
    quiet_lwip_portaudio_destroy(interface);

    Pa_Terminate();

    return 0;
}
