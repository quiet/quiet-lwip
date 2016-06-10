#include <stdatomic.h>
#include <pthread.h>

#include "quiet-lwip-portaudio.h"

#include "quiet-lwip/util.h"

typedef struct {
    quiet_portaudio_encoder *encoder;
    quiet_portaudio_decoder *decoder;
    size_t encoder_sample_size;
    size_t decoder_sample_size;
    uint8_t *send_temp;
    size_t send_temp_len;
    uint8_t *recv_temp;
    size_t recv_temp_len;
    _Atomic bool rx_in_progress;
    bool tx_in_progress;
    uint64_t rx_wait_peer_frame;
    uint64_t rx_wait_peer_frame_thresh;
    bool frame_dump;
} portaudio_eth_driver;
