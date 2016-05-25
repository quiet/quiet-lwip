#include "quiet-lwip.h"

#include "lwip/def.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/stats.h"
#include "netif/etharp.h"
#include "lwip/tcpip.h"

typedef struct {
    quiet_encoder *encoder;
    quiet_decoder *decoder;
    uint8_t *send_temp;
    size_t send_temp_len;
    uint8_t *recv_temp;
    size_t recv_temp_len;
} eth_driver;
