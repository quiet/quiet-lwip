#include "quiet-lwip/driver.h"

// lwip -> quiet: convert tx data frame to audio samples
static err_t quiet_lwip_encode_frame(struct netif *netif, struct pbuf *p) {
    eth_driver *driver = (eth_driver*)netif->state;

    size_t len = pbuf2buf(driver->send_temp, p);

    quiet_encoder_send(driver->encoder, driver->send_temp, len);

    // TODO figure out if we should set a flag to start audio stream
    // should audio stream just remain on, as long as the link's up?
    // it might be sending lots of 0 packets, but that would be simpler
    // than starting and stopping the stream as data appears

    LINK_STATS_INC(link.xmit);

    return ERR_OK;
}

// quiet -> hw: call user code to send audio samples to hw
ssize_t quiet_lwip_get_next_audio_packet(struct netif *netif, quiet_sample_t *buf, size_t samplebuf_len) {
    eth_driver *driver = (eth_driver*)netif->state;
    return quiet_encoder_emit(driver->encoder, buf, samplebuf_len);
}

// quiet -> lwip: pull one received frame out of quiet's receive buffer
static struct pbuf *quiet_lwip_fetch_single_frame(struct netif *netif) {
    eth_driver *driver = (eth_driver*)netif->state;
    ssize_t len = quiet_decoder_recv(driver->decoder, driver->recv_temp, driver->recv_temp_len);
    // XXX negative len (quiet errors)
    if (!len) {
        // all done
        return NULL;
    }
    struct pbuf *p = buf2pbuf(driver->recv_temp, len);
    if (!p) {
        LINK_STATS_INC(link.memerr);
        LINK_STATS_INC(link.drop);
        return NULL;
    }

    LINK_STATS_INC(link.recv);

    return p;
}

// quiet -> lwip: send received frame to lwip
static void quiet_lwip_process_audio(struct netif *netif) {
    // this loop will run until all frames are pulled out of
    //    quiet's receive buffer
    while (true) {
        struct pbuf *p = quiet_lwip_fetch_single_frame(netif);

        if (!p) {
            // quiet_lwip_fetch_single_frame returns NULL once recv buffer is empty
            break;
        }

        recv_pbuf(netif, p);
    }
}

// hw -> quiet: receive audio samples from user/hw and decode to frame
void quiet_lwip_recv_audio_packet(struct netif *netif, quiet_sample_t *buf, size_t samplebuf_len) {
    eth_driver *driver = (eth_driver*)netif->state;
    quiet_decoder_consume(driver->decoder, buf, samplebuf_len);
    quiet_lwip_process_audio(netif);
}


// TODO add capability for quiet to calculate its bps
// based on frame length, number of samples in frame, sample rate?
unsigned int calculate_bps(const quiet_encoder *e) {
    return 4000;
}

static err_t quiet_lwip_init(struct netif *netif) {
    quiet_lwip_driver_config *conf = (quiet_lwip_driver_config*)netif->state;

    quiet_encoder *e = quiet_encoder_create(conf->encoder_opt, conf->encoder_rate);

    quiet_decoder *d = quiet_decoder_create(conf->decoder_opt, conf->decoder_rate);

    eth_driver *driver = calloc(1, sizeof(eth_driver));
    driver->encoder = e;
    driver->decoder = d;

    netif->state = driver;

    netif->name[0] = 'q';
    netif->name[1] = 'u';

    // just use the default eth arp (we'll pretend to be ethernet)
    netif->output = etharp_output;
    netif->linkoutput = quiet_lwip_encode_frame;

    NETIF_INIT_SNMP(netif, snmp_ifType_other, calculate_bps(e));

    netif->hwaddr_len = ETHARP_HWADDR_LEN;

    memcpy(netif->hwaddr, conf->hardware_addr, 6);

    netif->mtu = quiet_encoder_get_frame_len(e);
    driver->send_temp_len = netif->mtu + ETH_PAD_SIZE;
    driver->send_temp = malloc(driver->send_temp_len * sizeof(uint8_t));
    driver->recv_temp_len = netif->mtu + ETH_PAD_SIZE;
    driver->recv_temp = malloc(driver->recv_temp_len * sizeof(uint8_t));

    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

    return ERR_OK;
}

quiet_lwip_interface *quiet_lwip_create(quiet_lwip_driver_config *conf,
                                        quiet_lwip_ipv4_addr local_address,
                                        quiet_lwip_ipv4_addr netmask,
                                        quiet_lwip_ipv4_addr gateway) {
    static bool has_lwip_init = false;

    if (!has_lwip_init) {
        tcpip_init(NULL, NULL);
        has_lwip_init = true;
    }

    quiet_lwip_interface *interface = malloc(1 * sizeof(quiet_lwip_interface));
    struct ip_addr addr, nm, gw;
    addr.addr = local_address;
    nm.addr = netmask;
    gw.addr = gateway;
    netif_add(interface, &addr, &nm,
              &gw, conf, quiet_lwip_init,
              tcpip_input);
    netif_set_up(interface);
    return interface;
}

void quiet_lwip_destroy(quiet_lwip_interface *interface) {
    netif_set_down(interface);
    netif_remove(interface);
    free(interface);
}
