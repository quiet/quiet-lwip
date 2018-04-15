#include "quiet-lwip/driver.h"

void quiet_lwip_init() {
    static bool has_lwip_init = false;

    if (!has_lwip_init) {
        tcpip_init(NULL, NULL);
        has_lwip_init = true;
    }
}

// lwip -> quiet: convert tx data frame to audio samples
static err_t quiet_lwip_encode_frame(struct netif *netif, struct pbuf *p) {
    eth_driver *driver = (eth_driver *)netif->state;

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
ssize_t quiet_lwip_get_next_audio_packet(struct netif *netif,
                                         quiet_sample_t *buf,
                                         size_t samplebuf_len) {
    eth_driver *driver = (eth_driver *)netif->state;
    return quiet_encoder_emit(driver->encoder, buf, samplebuf_len);
}

// quiet -> lwip: pull one received frame out of quiet's receive buffer
static struct pbuf *quiet_lwip_fetch_single_frame(struct netif *netif) {
    eth_driver *driver = (eth_driver *)netif->state;
    ssize_t len = quiet_decoder_recv(driver->decoder, driver->recv_temp,
                                     driver->recv_temp_len);
    if (len <= 0) {
        // all done
        // this assumes that the decoder is set to blocking mode
        return NULL;
    }
    struct pbuf *p = buf2pbuf(driver->recv_temp, len);
    if (!p) {
        LINK_STATS_INC(link.memerr);
        LINK_STATS_INC(link.drop);
        return NULL;
    }

    if (driver->frame_dump) {
        // emit the entire received frame with a timestamp and in hex format
        // this is *very* noisy, but the resulting dump can be turned into a
        // pcap
        struct timeval now;
        gettimeofday(&now, NULL);
        char fmt[64], buf[64];
        struct tm *tminfo = localtime(&now.tv_sec);
        strftime(fmt, sizeof(fmt), "%Y-%m-%d %H:%M:%S.%%006u %z", tminfo);
        snprintf(buf, sizeof(buf), fmt, now.tv_usec);
        printf("received frame @ %s: ", buf);
        for (size_t i = 0; i < p->tot_len; i++) {
            printf("%02x", driver->recv_temp[i]);
        }
        printf("\n");
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
            // quiet_lwip_fetch_single_frame returns NULL once recv buffer is
            // empty
            break;
        }

        recv_pbuf(netif, p);
    }
}

static void *read_loop(void *interface_v) {
    quiet_lwip_interface *interface = (quiet_lwip_interface *)interface_v;
    quiet_lwip_process_audio(interface);  // runs until ring closed
    pthread_exit(NULL);
}

static pthread_t start_read_thread(void *arg) {
    pthread_t read_thread;
    pthread_attr_t thread_attr;

    pthread_attr_init(&thread_attr);
    pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE);

    pthread_create(&read_thread, &thread_attr, read_loop, arg);

    pthread_attr_destroy(&thread_attr);

    return read_thread;
}

void quiet_lwip_start_threads(quiet_lwip_interface *interface) {
    eth_driver *driver = interface->state;
    quiet_decoder_set_blocking(driver->decoder, 0, 0);

    driver->read_thread = start_read_thread(interface);
}

void quiet_lwip_join_threads(quiet_lwip_interface *interface) {
    eth_driver *driver = interface->state;

    pthread_join(driver->read_thread, NULL);
}

// hw -> quiet: receive audio samples from user/hw and decode to frame
void quiet_lwip_recv_audio_packet(struct netif *netif, quiet_sample_t *buf,
                                  size_t samplebuf_len) {
    eth_driver *driver = (eth_driver *)netif->state;
    quiet_decoder_consume(driver->decoder, buf, samplebuf_len);
}

// TODO add capability for quiet to calculate its bps
// based on frame length, number of samples in frame, sample rate?
unsigned int calculate_bps(const quiet_encoder *e) { return 4000; }

static err_t quiet_lwip_interface_init(struct netif *netif) {
    quiet_lwip_driver_config *conf = (quiet_lwip_driver_config *)netif->state;

    quiet_encoder *e =
        quiet_encoder_create(conf->encoder_opt, conf->encoder_rate);

    if (!e) {
        return ERR_IF;
    }

    quiet_decoder *d =
        quiet_decoder_create(conf->decoder_opt, conf->decoder_rate);

    if (!d) {
        quiet_encoder_destroy(e);
        return ERR_IF;
    }

    eth_driver *driver = calloc(1, sizeof(eth_driver));
    driver->encoder = e;
    driver->decoder = d;

    driver->frame_dump = false;

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

    netif->flags =
        NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

    quiet_lwip_start_threads(netif);

    return ERR_OK;
}

quiet_lwip_interface *quiet_lwip_create(quiet_lwip_driver_config *conf,
                                        quiet_lwip_ipv4_addr local_address,
                                        quiet_lwip_ipv4_addr netmask,
                                        quiet_lwip_ipv4_addr gateway) {
    quiet_lwip_init();
    quiet_lwip_interface *interface = malloc(1 * sizeof(quiet_lwip_interface));
    struct ip_addr *addr_ptr, *nm_ptr, *gw_ptr;
    struct ip_addr addr, nm, gw;
    if (local_address) {
        addr.addr = local_address;
        addr_ptr = &addr;

        nm.addr = netmask;
        nm_ptr = &nm;

        gw.addr = gateway;
        gw_ptr = &gw;
    } else {
        addr_ptr = nm_ptr = gw_ptr = NULL;
    }
    interface = netif_add(interface, addr_ptr, nm_ptr, gw_ptr, conf, quiet_lwip_interface_init,
                          tcpip_input);
    if (!interface) {
        free(interface);
        return NULL;
    }
    if (local_address) {
        netif_set_up(interface);
    }

    return interface;
}

quiet_lwip_interface *quiet_lwip_autoip(quiet_lwip_interface *interface) {
    err_t err = autoip_start(interface);
    if (err) {
        netif_remove(interface);
        free(interface);
        return NULL;
    }
    return interface;
}

void quiet_lwip_close(quiet_lwip_interface *interface) {
    eth_driver *driver = (eth_driver *)interface->state;
    // close the decoder. this prevents new frames from being inserted
    // into lwip/our interface
    quiet_decoder_close(driver->decoder);
    // join our read thread. this thread does the actual passing
    // from the decoder to lwip. it will exit once the decoder is closed
    // we join it here so that later the decoder can be safely destroyed
    quiet_lwip_join_threads(interface);
    // down the interface. we're no longer trying to write to it
    netif_set_down(interface);
    // close the encoder. the interface won't try to write to it,
    // and if there are still frames in it, they could be flushed
    // out if desired
    quiet_encoder_close(driver->encoder);
}

void quiet_lwip_destroy(quiet_lwip_interface *interface) {
    // close first in order to prevent invalid state
    quiet_lwip_close(interface);
    netif_remove(interface);
    eth_driver *driver = (eth_driver *)interface->state;
    quiet_encoder_destroy(driver->encoder);
    quiet_decoder_destroy(driver->decoder);
    free(interface);
}
