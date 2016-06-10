#include "lwip/def.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/stats.h"
#include "netif/etharp.h"
#include "lwip/tcpip.h"

size_t pbuf2buf(uint8_t *buf, struct pbuf *p);

struct pbuf *buf2pbuf(const uint8_t *buf, size_t len);

void recv_pbuf(struct netif *netif, struct pbuf *p);
