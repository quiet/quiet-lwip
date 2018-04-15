#include <pthread.h>

#include "quiet-lwip.h"

#include "quiet-lwip/util.h"

typedef struct {
    quiet_encoder *encoder;
    quiet_decoder *decoder;
    uint8_t *send_temp;
    size_t send_temp_len;
    uint8_t *recv_temp;
    size_t recv_temp_len;
    bool frame_dump;
    pthread_t read_thread;
} eth_driver;
