#include "quiet-lwip/util.h"

size_t pbuf2buf(uint8_t *buf, struct pbuf *p) {
    pbuf_header(p, -ETH_PAD_SIZE);

    size_t len = 0;
    for(struct pbuf *q = p; q != NULL; q = q->next) {
        memcpy(buf + len, q->payload, q->len);
        // XXX check over send_temp_len?
        len += q->len;
    }

    pbuf_header(p, ETH_PAD_SIZE);

    return len;
}

struct pbuf *buf2pbuf(const uint8_t *buf, size_t len) {
    len += ETH_PAD_SIZE;
    struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
    if (!p) {
        return NULL;
    }
    size_t copied = 0;
    for(struct pbuf *q = p; q != NULL; q = q->next) {
        memcpy(q->payload, buf + copied, q->len);
        copied += q->len;
    }

    pbuf_header(p, ETH_PAD_SIZE);

    return p;
}

void recv_pbuf(struct netif *netif, struct pbuf *p) {
    struct eth_hdr *ethhdr = p->payload;

    switch (htons(ethhdr->type)) {
    /* IP or ARP packet? */
    case ETHTYPE_IP:
    case ETHTYPE_ARP:
#if PPPOE_SUPPORT
    /* PPPoE packet? */
    case ETHTYPE_PPPOEDISC:
    case ETHTYPE_PPPOE:
#endif /* PPPOE_SUPPORT */
        /* full packet send to tcpip_thread to process */
        if (netif->input(p, netif) != ERR_OK) {
            LWIP_DEBUGF(NETIF_DEBUG, ("recv_pbuf: IP input error\n"));
            pbuf_free(p);
        }
        break;
    default:
        pbuf_free(p);
        break;
    }
}
