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
#include <errno.h>
#include <fcntl.h>

#include "quiet-lwip-portaudio.h"

#include "quiet-lwip/lwip-socket.h"

#include "relay.h"

const int local_port = 1080;

const uint8_t mac[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x07};
const quiet_lwip_ipv4_addr ipaddr = (uint32_t)0xc0a80008;   // 192.168.0.8
const char *ipaddr_s = "192.168.0.8";
const quiet_lwip_ipv4_addr netmask = (uint32_t)0xffffff00;  // 255.255.255.0
const quiet_lwip_ipv4_addr gateway = (uint32_t)0xc0a80001;  // 192.168.0.1

const uint8_t SOCKS_VERSION = 5;
const uint8_t SOCKS_AUTH_NOAUTH = 0;
const uint8_t SOCKS_CMD_CONNECT = 1;
const uint8_t SOCKS_ADDR_IPV4 = 1;
const uint8_t SOCKS_ADDR_DOMAINNAME = 3;

const uint8_t SOCKS_CONN_SUCCEEDED = 0;
const uint8_t SOCKS_CONN_FAILED = 1;

int socks_auth(int socket_fd, uint8_t *buf, size_t buf_len) {
    ssize_t nread = lwip_read(socket_fd, buf, 2);

    if (nread < 2) {
        return -1;
    }

    if (buf[0] != SOCKS_VERSION) {
        return -1;
    }

    uint8_t nmethods = buf[1];

    nread = lwip_read(socket_fd, buf, nmethods);

    if (nread < nmethods) {
        return -1;
    }

    for (size_t i = 0; i < nmethods; i++) {
        if (buf[i] == SOCKS_AUTH_NOAUTH) {
            return 0;
        }
    }

    return -1;
}

int socks_auth_reply(int socket_fd, uint8_t *buf, size_t buf_len) {
    buf[0] = SOCKS_VERSION;
    buf[1] = SOCKS_AUTH_NOAUTH;
    ssize_t nwritten = lwip_write(socket_fd, buf, 2);

    return (nwritten == 2) ? 0 : -1;
}

int socks_request_connect_ipv4(int socket_fd, uint8_t *buf, size_t buf_len) {
    int remote_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (remote_fd < 0) {
        return -1;
    }

    ssize_t nread = lwip_read(socket_fd, buf, 6);

    if (nread < 6) {
        return -1;
    }

    uint32_t ipv4_addr = ((uint32_t*)buf)[0];
    uint16_t port = ((uint16_t*)buf)[2];

    struct sockaddr_in remote;
    remote.sin_family = AF_INET;
    remote.sin_addr.s_addr = ipv4_addr;
    remote.sin_port = port;

    printf("connecting to %s:%d\n", inet_ntoa(remote.sin_addr), ntohs(remote.sin_port));

    int res = connect(remote_fd, (struct sockaddr*)&remote, sizeof(remote));

    if (res < 0) {
        return -1;
    }

    return remote_fd;
}

int socks_request_connect_domainname(int socket_fd, uint8_t *buf, size_t buf_len) {
    ssize_t nread = lwip_read(socket_fd, buf, 1);

    if (nread < 1) {
        return -1;
    }

    uint8_t len = buf[0];

    nread = lwip_read(socket_fd, buf, len + 2);

    if (nread < len) {
        return -1;
    }

    char *domainname = calloc(len + 1, sizeof(char));
    memcpy(domainname, buf, len);

    uint16_t *buf16 = (uint16_t*)(buf + len);
    uint16_t remote_port = buf16[0];
    char port_s[6];
    memset(port_s, 0, 6);
    snprintf(port_s, 5, "%d", ntohs(remote_port));

    printf("connecting to %s:%s\n", domainname, port_s);

    struct addrinfo hints, *results;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int res = getaddrinfo(domainname, port_s, &hints, &results);
    free(domainname);

    if (res != 0) {
        printf("getaddrinfo failed: %s\n", gai_strerror(res));
        return -1;
    }

    int remote_fd = -1;
    for(struct addrinfo *i = results; i != NULL; i = i->ai_next) {
        remote_fd = socket(i->ai_family, i->ai_socktype, i->ai_protocol);
        if (remote_fd < 0) {
            continue;
        }

        printf("connecting to %s:%d\n", inet_ntoa(((struct sockaddr_in*)(i->ai_addr))->sin_addr), ntohs(((struct sockaddr_in*)(i->ai_addr))->sin_port));

        if (connect(remote_fd, i->ai_addr, i->ai_addrlen) == 0) {
            break;
        }

        close(remote_fd);
        remote_fd = -1;
    }

    freeaddrinfo(results);

    return remote_fd;
}

int socks_request(int socket_fd, uint8_t *buf, size_t buf_len) {
    ssize_t nread = lwip_read(socket_fd, buf, 4);

    if (nread < 4) {
        return -1;
    }

    if (buf[0] != SOCKS_VERSION) {
        return -1;
    }

    if (buf[1] != SOCKS_CMD_CONNECT) {
        return -1;
    }

    // buf[2] is a reserved byte

    if (buf[3] == SOCKS_ADDR_IPV4) {
        return socks_request_connect_ipv4(socket_fd, buf, buf_len);
    }

    if (buf[3] == SOCKS_ADDR_DOMAINNAME) {
        return socks_request_connect_domainname(socket_fd, buf, buf_len);
    }

    return -1;
}

int socks_request_reply(int client_fd, int remote_fd, uint8_t *buf, size_t buf_len) {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    int res = getsockname(remote_fd, (struct sockaddr*)&addr, &len);

    if (res < 0) {
        return -1;
    }

    uint32_t *buf32 = (uint32_t*)buf;
    uint16_t *buf16 = (uint16_t*)buf;

    buf[0] = SOCKS_VERSION;
    buf[1] = SOCKS_CONN_SUCCEEDED;
    buf[2] = 0;
    buf[3] = SOCKS_ADDR_IPV4;

    buf32[1] = htonl(addr.sin_addr.s_addr);
    buf16[4] = htons(addr.sin_port);

    ssize_t nwritten = lwip_write(client_fd, buf, 10);

    if (nwritten < 10) {
        return -1;
    }

    return 0;
}

int socks_request_reply_failed(int client_fd, uint8_t *buf, size_t buf_len) {
    uint32_t *buf32 = (uint32_t*)buf;
    uint16_t *buf16 = (uint16_t*)buf;

    buf[0] = SOCKS_VERSION;
    buf[1] = SOCKS_CONN_FAILED;
    buf[2] = 0;
    buf[3] = SOCKS_ADDR_IPV4;
    buf32[1] = 0;
    buf16[5] = 0;

    ssize_t nwritten = lwip_write(client_fd, buf, 10);

    if (nwritten < 10) {
        return -1;
    }

    return 0;
}

int open_recv(const char *addr) {
    int socket_fd = lwip_socket(AF_INET, SOCK_STREAM, 0);

    if (socket_fd < 0) {
        printf("socket failed\n");
        return -1;
    }

    struct sockaddr_in *local_addr = calloc(1, sizeof(struct sockaddr_in));
    local_addr->sin_family = AF_INET;
    local_addr->sin_addr.s_addr = inet_addr(addr);
    local_addr->sin_port = htons(local_port);

    int res = lwip_bind(socket_fd, (struct sockaddr *)local_addr, sizeof(struct sockaddr_in));
    free(local_addr);

    if (res < 0) {
        printf("bind failed\n");
        return -1;
    }

    res = lwip_listen(socket_fd, 1);

    if (res < 0) {
        printf("listen failed\n");
        return -1;
    }

    return socket_fd;
}

int recv_connection(int socket_fd, struct sockaddr_in *recv_from) {
    socklen_t recv_from_len = sizeof(recv_from);
    return lwip_accept(socket_fd, (struct sockaddr *)recv_from, &recv_from_len);
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
        .agent = agent_lwip,
        .other_agent = agent_native,
        .incoming = &client_crossbar,
        .outgoing = &remote_crossbar,
        .read = _lwip_read,
        .write = _lwip_write,
        .select = lwip_select,
        .other_shutdown = shutdown,
        .get_errno = lwip_errno,
    };

    relay_t remote_relay = {
        .agent = agent_native,
        .other_agent = agent_lwip,
        .incoming = &remote_crossbar,
        .outgoing = &client_crossbar,
        .read = read,
        .write = write,
        .select = select,
        .other_shutdown = lwip_shutdown,
        .get_errno = native_errno,
    };

    start_relay_thread(&client_relay);
    start_relay_thread(&remote_relay);

    int recv_socket = open_recv(ipaddr_s);
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

        int res = socks_auth(conn_fd, buf, buf_len);

        if (res < 0) {
            printf("socks auth failed\n");
            lwip_close(conn_fd);
            continue;
        }

        res = socks_auth_reply(conn_fd, buf, buf_len);

        if (res < 0) {
            printf("socks auth reply failed\n");
            lwip_close(conn_fd);
            continue;
        }

        int remote_fd = socks_request(conn_fd, buf, buf_len);

        if (remote_fd < 0) {
            socks_request_reply_failed(conn_fd, buf, buf_len);
            lwip_close(conn_fd);
            printf("socks request failed\n");
            continue;
        }

        res = socks_request_reply(conn_fd, remote_fd, buf, buf_len);

        if (res < 0) {
            printf("socks request reply failed\n");
            lwip_close(conn_fd);
            continue;
        }

        relay_conn *conn = relay_conn_create(remote_fd, conn_fd, 1 << 13);

        crossbar_add_for_reading(&client_crossbar, conn);
        crossbar_add_for_reading(&remote_crossbar, conn);
    }

    quiet_lwip_portaudio_stop_audio_threads(audio_threads);
    free(buf);
    quiet_lwip_portaudio_destroy(interface);

    Pa_Terminate();

    return 0;
}
