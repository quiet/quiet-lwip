#include <quiet-portaudio.h>

#include <quiet.h>

typedef struct {
    const quiet_encoder_options *encoder_opt;
    const quiet_decoder_options *decoder_opt;
    const char *hostname;
    PaDeviceIndex encoder_device;
    PaDeviceIndex decoder_device;
    PaTime encoder_latency;
    PaTime decoder_latency;
    double encoder_sample_rate;
    double decoder_sample_rate;
    size_t encoder_sample_size;
    size_t decoder_sample_size;
    uint8_t hardware_addr[6];
} quiet_lwip_portaudio_driver_config;

typedef uint32_t quiet_lwip_ipv4_addr;

struct netif;
typedef struct netif quiet_lwip_portaudio_interface;

ssize_t quiet_lwip_portaudio_get_next_audio_packet(quiet_lwip_portaudio_interface *interface);

void quiet_lwip_portaudio_recv_audio_packet(quiet_lwip_portaudio_interface *interface);

quiet_lwip_portaudio_interface *quiet_lwip_portaudio_create(quiet_lwip_portaudio_driver_config *conf,
                                                            quiet_lwip_ipv4_addr local_address,
                                                            quiet_lwip_ipv4_addr netmask,
                                                            quiet_lwip_ipv4_addr gateway);

void quiet_lwip_portaudio_destroy(quiet_lwip_portaudio_interface *interface);

struct quiet_lwip_portaudio_audio_threads;
typedef struct quiet_lwip_portaudio_audio_threads quiet_lwip_portaudio_audio_threads;

quiet_lwip_portaudio_audio_threads *quiet_lwip_portaudio_start_audio_threads(quiet_lwip_portaudio_interface *interface);

void quiet_lwip_portaudio_stop_audio_threads(quiet_lwip_portaudio_audio_threads *threads);
