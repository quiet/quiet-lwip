#include <math.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#include "quiet-lwip.h"

#include "lwip/sockets.h"

const int dest_port = 6000;
const char *dest_addr = "127.0.0.1";

int open_send(const char *from_addr) {
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (socket_fd < 0) {
        printf("socket failed\n");
        return -1;
    }

    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(struct sockaddr_in));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = inet_addr(from_addr);
    local_addr.sin_port = htons(0);

    int res = bind(socket_fd, (struct sockaddr *)&local_addr, sizeof(local_addr));

    if (res < 0) {
        printf("bind failed\n");
        return -1;
    }

    return socket_fd;
}

void send_packet(int socket_fd, quiet_sample_t *samplebuf,
                 const char *to_addr, unsigned int to_port,
                 const char *packet) {
    struct sockaddr_in remote_addr;
    memset(&remote_addr, 0, sizeof(struct sockaddr_in));
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_addr.s_addr = inet_addr(to_addr);
    remote_addr.sin_port = htons(to_port);
    int res = sendto(socket_fd, packet, strlen(packet), 0, (struct sockaddr *)&remote_addr, sizeof(remote_addr));

    if (res < 0) {
        printf("port %d sendto failed\n", to_port);
        return;
    }
}

void *recv_packet(void *listen_void) {
    int socket_fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (socket_fd < 0) {
        printf("socket failed\n");
        return NULL;
    }


    struct sockaddr_in *local_addr = (struct sockaddr_in*)listen_void;
    int res = bind(socket_fd, (struct sockaddr *)local_addr, sizeof(struct sockaddr_in));

    if (res < 0) {
        printf("bind failed\n");
        return NULL;
    }

    for (;;) {
        size_t buf_len = 4096;
        char *buf = calloc(buf_len, sizeof(char));

        int numbytes = recv(socket_fd, buf, buf_len, 0);

        if (numbytes > 0) {
            printf("received payload on port %d: %.*s\n", ntohs(local_addr->sin_port), numbytes, buf);
        }
    }

    return NULL;
}

pthread_t start_listen_thread(const char *addr, unsigned int port) {
    struct sockaddr_in *local_addr = calloc(1, sizeof(struct sockaddr_in));
    local_addr->sin_family = AF_INET;
    local_addr->sin_addr.s_addr = inet_addr(addr);
    local_addr->sin_port = htons(port);

    pthread_t recv_thread;
    pthread_create(&recv_thread, NULL, recv_packet, local_addr);

    return recv_thread;
}

int main(int argc, char **argv) {
    quiet_lwip_driver_config *conf = calloc(1, sizeof(quiet_lwip_driver_config));
    const char *profiles_json = "{ \
    \"audible-7k\": { \
      \"mod_scheme\": \"arb16opt\", \
      \"checksum_scheme\": \"crc32\", \
      \"inner_fec_scheme\": \"v29\", \
      \"outer_fec_scheme\": \"rs8\", \
      \"frame_length\": 600, \
      \"modulation\": { \
        \"center_frequency\": 9200, \
        \"gain\": 0.01 \
      }, \
      \"interpolation\": { \
        \"shape\": \"kaiser\", \
        \"samples_per_symbol\": 6, \
        \"symbol_delay\": 4, \
        \"excess_bandwidth\": 0.31 \
      }, \
      \"encoder_filters\": { \
        \"dc_filter_alpha\": 0.01 \
      }, \
      \"resampler\": { \
        \"delay\": 13, \
        \"bandwidth\": 0.45, \
        \"attenuation\": 60, \
        \"filter_bank_size\": 64 \
      }, \
      \"ofdm\": { \
        \"num_subcarriers\": 48, \
        \"cyclic_prefix_length\": 8, \
        \"taper_length\": 4, \
        \"left_band\": 0, \
        \"right_band\": 0 \
      } \
    }}";
    const char *encoder_key = "audible-7k";
    const char *decoder_key = "audible-7k";
    conf->encoder_opt =
        quiet_encoder_profile_str(profiles_json, encoder_key);
    conf->decoder_opt =
        quiet_decoder_profile_str(profiles_json, decoder_key);
    conf->encoder_rate = 44100;
    conf->decoder_rate = 44100;

    // init alice
    conf->hostname = "alice";
    const uint8_t alice_mac[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    memcpy(conf->hardware_addr, alice_mac, 6);
    quiet_lwip_ipv4_addr alice_ipaddr = htonl((uint32_t)0xc0a80002);   // 192.168.0.2
    quiet_lwip_ipv4_addr alice_netmask = htonl((uint32_t)0xffffff00);  // 255.255.255.0
    quiet_lwip_ipv4_addr alice_gateway = htonl((uint32_t)0xc0a80001);  // 192.168.0.1
    quiet_lwip_interface *alice = quiet_lwip_create(conf, alice_ipaddr,
                                                    alice_netmask, alice_gateway);
    start_listen_thread("192.168.0.2", 6000);

    // init bob
    conf->hostname = "bob";
    const uint8_t bob_mac[] = {0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c};
    memcpy(conf->hardware_addr, bob_mac, 6);
    quiet_lwip_ipv4_addr bob_ipaddr = htonl((uint32_t)0xc0a80003);   // 192.168.0.3
    quiet_lwip_ipv4_addr bob_netmask = htonl((uint32_t)0xffffff00);  // 255.255.255.0
    quiet_lwip_ipv4_addr bob_gateway = htonl((uint32_t)0xc0a80001);  // 192.168.0.1
    quiet_lwip_interface *bob = quiet_lwip_create(conf, bob_ipaddr,
                                                  bob_netmask, bob_gateway);
    start_listen_thread("192.168.0.3", 6001);

    quiet_sample_t *a2b = calloc(16384, sizeof(quiet_sample_t));
    quiet_sample_t *b2a = calloc(16384, sizeof(quiet_sample_t));
    int alice_fd = open_send("192.168.0.2");
    int bob_fd = open_send("192.168.0.3");
    int loop_counter = 0;
    while (1) {
        memset(a2b, 0, 16384 * sizeof(quiet_sample_t));
        memset(b2a, 0, 16384 * sizeof(quiet_sample_t));
        size_t a2b_len = quiet_lwip_get_next_audio_packet(alice, a2b, 16384);
        if (a2b_len) {
            printf("a2b packet size: %zu\n", a2b_len);
        }
        size_t b2a_len = quiet_lwip_get_next_audio_packet(bob, b2a, 16384);
        if (b2a_len) {
            printf("b2a packet size: %zu\n", b2a_len);
        }
        quiet_lwip_recv_audio_packet(alice, b2a, 16384);
        quiet_lwip_recv_audio_packet(bob, a2b, 16384);
        loop_counter++;
        if (loop_counter % 8 == 0) {
            send_packet(bob_fd, b2a, "192.168.0.2", 6000, "Hello, Alice!");
        }
        if (loop_counter % 13 == 0) {
            send_packet(alice_fd, a2b, "192.168.0.3", 6001, "Hello, Bob!");
        }
        usleep(300000);
    }
    free(a2b);
    free(b2a);
    quiet_lwip_destroy(alice);
    quiet_lwip_destroy(bob);
    return 0;
}
