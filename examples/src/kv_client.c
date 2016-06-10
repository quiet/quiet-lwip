#include <math.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>

#include "quiet-lwip-portaudio.h"

#include "lwip/sockets.h"

const int remote_port = 7173;

const uint8_t mac[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
const quiet_lwip_ipv4_addr ipaddr = (uint32_t)0xc0a80002;   // 192.168.0.2
const char *ipaddr_s = "192.168.0.2";
const quiet_lwip_ipv4_addr netmask = (uint32_t)0xffffff00;  // 255.255.255.0
const quiet_lwip_ipv4_addr gateway = (uint32_t)0xc0a80001;  // 192.168.0.1

int open_send(const char *addr) {
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (socket_fd < 0) {
        printf("socket failed\n");
        return -1;
    }

    struct sockaddr_in remote;
    remote.sin_family = AF_INET;
    remote.sin_addr.s_addr = inet_addr(addr);
    remote.sin_port = htons(remote_port);
    int res = connect(socket_fd, (struct sockaddr*)&remote, sizeof(remote));

    if (res < 0) {
        printf("connect failed\n");
    }

    return socket_fd;
}

int main(int argc, char **argv) {
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        printf("failed to initialize port audio, %s\n", Pa_GetErrorText(err));
        return 1;
    }

    quiet_lwip_portaudio_driver_config *conf =
        calloc(1, sizeof(quiet_lwip_portaudio_driver_config));
    const char *encoder_key = "audible-7k-channel-0";
    const char *decoder_key = "audible-7k-channel-1";
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

    size_t buf_len = 4096;
    uint8_t *buf = calloc(buf_len, sizeof(uint8_t));

    int send_socket = open_send(argv[1]);

    char *request = argv[2];

    memcpy(buf, request, strlen(request));

    ssize_t write_len = write(send_socket, buf, strlen(request));

    if (write_len < 0) {
        printf("write to socket failed\n");
    }

    memset(buf, 0, buf_len);

    ssize_t recv_len = read(send_socket, buf, buf_len);
    printf("%.*s\n", (int)recv_len, buf);
    close(send_socket);

    sleep(1);

    quiet_lwip_portaudio_stop_audio_threads(audio_threads);
    free(buf);
    quiet_lwip_portaudio_destroy(interface);

    Pa_Terminate();

    return 0;
}
