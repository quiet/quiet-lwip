#include <math.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#include "quiet-lwip-portaudio.h"

#include "lwip/sockets.h"

const int remote_port = 3334;
const int local_port = 3333;

const uint8_t mac[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x07};
const quiet_lwip_ipv4_addr ipaddr = (uint32_t)0xc0a80008;   // 192.168.0.8
const char *ipaddr_s = "192.168.0.8";
const quiet_lwip_ipv4_addr netmask = (uint32_t)0xffffff00;  // 255.255.255.0
const quiet_lwip_ipv4_addr gateway = (uint32_t)0xc0a80001;  // 192.168.0.1

const char *recv_str = "MARCO";
const char *send_str = "POLO";

int open_send() {
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (socket_fd < 0) {
        printf("socket failed\n");
        return -1;
    }

    return socket_fd;
}

int send_packet(int socket_fd, uint32_t remote_ip_addr, const uint8_t *packet, size_t packet_len) {
    struct sockaddr_in remote_addr;
    memset(&remote_addr, 0, sizeof(struct sockaddr_in));
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_addr.s_addr = remote_ip_addr;
    remote_addr.sin_port = htons(remote_port);
    int res = sendto(socket_fd, packet, packet_len, 0, (struct sockaddr *)&remote_addr, sizeof(remote_addr));

    if (res < 0) {
        printf("sendto failed\n");
        return -1;
    }

    return 0;
}

int open_recv(const char *addr) {
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);

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

    return socket_fd;
}

ssize_t recv_packet(int socket_fd, uint8_t *packet,
                    size_t packet_len, struct sockaddr_in *recv_from) {

    socklen_t recv_from_len = sizeof(recv_from);
    return recvfrom(socket_fd, packet, packet_len, 0, (struct sockaddr *)recv_from, &recv_from_len);
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

    conf->encoder_sample_size = 1 << 10;
    conf->decoder_sample_size = 1 << 10;

    memcpy(conf->hardware_addr, mac, 6);
    quiet_lwip_portaudio_interface *interface =
        quiet_lwip_portaudio_create(conf, htonl(ipaddr), htonl(netmask), htonl(gateway));
    free(conf);

    quiet_lwip_portaudio_audio_threads *audio_threads =
        quiet_lwip_portaudio_start_audio_threads(interface);

    int recv_socket = open_recv(ipaddr_s);
    int send_socket = open_send();

    size_t buf_len = 4096;
    uint8_t *buf = calloc(buf_len, sizeof(uint8_t));
    struct sockaddr_in *recv_from = calloc(1, sizeof(struct sockaddr_in));

    for (;;) {
        size_t recv_len = recv_packet(recv_socket, buf, buf_len, recv_from);
        printf("received request from %s: %.*s\n", inet_ntoa(recv_from->sin_addr.s_addr), (int)recv_len, buf);

        if (memcmp(buf, recv_str, recv_len) == 0) {
            printf("sending response to %s port %d\n", inet_ntoa(recv_from->sin_addr.s_addr), remote_port);
            memset(buf, 0, buf_len);
            memcpy(buf, send_str, strlen(send_str));
            send_packet(send_socket, recv_from->sin_addr.s_addr, buf, strlen(send_str));
        }
    }

    free(recv_from);
    free(buf);
    quiet_lwip_portaudio_stop_audio_threads(audio_threads);
    quiet_lwip_portaudio_destroy(interface);

    Pa_Terminate();

    return 0;
}
