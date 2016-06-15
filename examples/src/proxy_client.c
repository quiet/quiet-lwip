#include <math.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdatomic.h>

#include "quiet-lwip-portaudio.h"

#include "quiet-lwip/lwip-socket.h"

#include "relay.h"

const int local_port = 2160;
const int remote_port = 1080;

const uint8_t mac[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x07};
const quiet_lwip_ipv4_addr ipaddr = (uint32_t)0xc0a80002;   // 192.168.0.2
const char *ipaddr_s = "192.168.0.2";
const quiet_lwip_ipv4_addr netmask = (uint32_t)0xffffff00;  // 255.255.255.0
const quiet_lwip_ipv4_addr gateway = (uint32_t)0xc0a80001;  // 192.168.0.1

const uint8_t SOCKS_VERSION = 5;
const uint8_t SOCKS_AUTH_NOAUTH = 0;
const uint8_t SOCKS_CMD_CONNECT = 1;
const uint8_t SOCKS_ADDR_IPV4 = 1;
const uint8_t SOCKS_ADDR_DOMAINNAME = 3;

const uint8_t SOCKS_CONN_SUCCEEDED = 0;
const uint8_t SOCKS_CONN_FAILED = 1;

int open_send(const char *addr) {
    int socket_fd = lwip_socket(AF_INET, SOCK_STREAM, 0);

    if (socket_fd < 0) {
        printf("socket failed\n");
        return -1;
    }

    struct sockaddr_in remote;
    remote.sin_family = AF_INET;
    remote.sin_addr.s_addr = inet_addr(addr);
    remote.sin_port = htons(remote_port);
    int res = lwip_connect(socket_fd, (struct sockaddr*)&remote, sizeof(remote));

    if (res < 0) {
        printf("connect failed\n");
    }

    return socket_fd;
}

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

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        printf("failed to initialize port audio, %s\n", Pa_GetErrorText(err));
        return 1;
    }

    quiet_lwip_portaudio_driver_config *conf =
        calloc(1, sizeof(quiet_lwip_portaudio_driver_config));
    const char *encoder_key = "cable-64k";
    const char *decoder_key = "cable-64k";
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

    conf->encoder_sample_size = 1 << 7;
    conf->decoder_sample_size = 1 << 7;

    memcpy(conf->hardware_addr, mac, 6);
    quiet_lwip_portaudio_interface *interface =
        quiet_lwip_portaudio_create(conf, htonl(ipaddr), htonl(netmask), htonl(gateway));
    free(conf);

    quiet_lwip_portaudio_audio_threads *audio_threads =
        quiet_lwip_portaudio_start_audio_threads(interface);

    crossbar client_crossbar;
    crossbar remote_crossbar;
    crossbar_init(&client_crossbar);
    crossbar_init(&remote_crossbar);

    relay_t client_relay = {
        .agent = agent_native,
        .other_agent = agent_lwip,
        .incoming = &client_crossbar,
        .outgoing = &remote_crossbar,
        .read = read,
        .write = write,
        .select = select,
        .other_shutdown = lwip_shutdown,
        .get_errno = native_errno,
    };

    relay_t remote_relay = {
        .agent = agent_lwip,
        .other_agent = agent_native,
        .incoming = &remote_crossbar,
        .outgoing = &client_crossbar,
        .read = _lwip_read,
        .write = _lwip_write,
        .select = lwip_select,
        .other_shutdown = shutdown,
        .get_errno = lwip_errno,
    };

    start_relay_thread(&client_relay);
    start_relay_thread(&remote_relay);

    int recv_socket = open_recv("127.0.0.1");

    if (recv_socket < 0) {
        printf("couldn't open socket for listening\n");
        exit(1);
    }

    size_t buf_len = 512;
    uint8_t *buf = calloc(buf_len, sizeof(uint8_t));

    for (;;) {
        struct sockaddr_in recv_from;
        int conn_fd = recv_connection(recv_socket, &recv_from);
        if (conn_fd < 0) {
            continue;
        }

        printf("received connection from %s\n", inet_ntoa(recv_from.sin_addr));

        int remote_fd = open_send(argv[1]);

        if (remote_fd < 0) {
            close(conn_fd);
            printf("remote connect failed\n");
            continue;
        }

        relay_conn *conn = relay_conn_create(conn_fd, remote_fd, 1 << 13);

        crossbar_add_for_reading(&client_crossbar, conn);
        crossbar_add_for_reading(&remote_crossbar, conn);
    }

    quiet_lwip_portaudio_stop_audio_threads(audio_threads);
    free(buf);
    quiet_lwip_portaudio_destroy(interface);

    Pa_Terminate();

    return 0;
}
