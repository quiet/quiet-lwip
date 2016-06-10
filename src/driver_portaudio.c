#include "quiet-lwip/driver_portaudio.h"

// lwip -> quiet: convert tx data frame to audio samples
static err_t quiet_lwip_portaudio_encode_frame(struct netif *netif, struct pbuf *p) {
    portaudio_eth_driver *driver = (portaudio_eth_driver*)netif->state;

    size_t len = pbuf2buf(driver->send_temp, p);

    quiet_portaudio_encoder_send(driver->encoder, driver->send_temp, len);

    // XXX check if this call failed ^^^^

    // TODO figure out if we should set a flag to start audio stream
    // should audio stream just remain on, as long as the link's up?
    // it might be sending lots of 0 packets, but that would be simpler
    // than starting and stopping the stream as data appears

    LINK_STATS_INC(link.xmit);

    return ERR_OK;
}

// quiet -> hw: call user code to send audio samples to hw
ssize_t quiet_lwip_portaudio_get_next_audio_packet(struct netif *netif) {
    portaudio_eth_driver *driver = (portaudio_eth_driver*)netif->state;
    ssize_t written;
    if (driver->tx_in_progress || !atomic_load(&driver->rx_in_progress)) {
        written = quiet_portaudio_encoder_emit(driver->encoder);
        driver->tx_in_progress = (written == driver->encoder_sample_size);
    } else {
        // prevent collision
        written = 0;
        quiet_portaudio_encoder_emit_empty(driver->encoder);
    }
    return written;
}

// quiet -> lwip: pull one received frame out of quiet's receive buffer
static struct pbuf *quiet_lwip_portaudio_fetch_single_frame(struct netif *netif) {
    portaudio_eth_driver *driver = (portaudio_eth_driver*)netif->state;
    ssize_t len = quiet_portaudio_decoder_recv(driver->decoder, driver->recv_temp, driver->recv_temp_len);
    // XXX negative len (quiet errors)
    if (len <= 0) {
        // all done
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
        // this is *very* noisy, but the resulting dump can be turned into a pcap
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
static void quiet_lwip_portaudio_process_audio(struct netif *netif) {
    // this loop will run until all frames are pulled out of
    //    quiet's receive buffer
    while (true) {
        struct pbuf *p = quiet_lwip_portaudio_fetch_single_frame(netif);

        if (!p) {
            // quiet_lwip_fetch_single_frame returns NULL once recv buffer is empty
            break;
        }

        recv_pbuf(netif, p);
    }
}

// hw -> quiet: receive audio samples from user/hw and decode to frame
void quiet_lwip_portaudio_recv_audio_packet(struct netif *netif) {
    portaudio_eth_driver *driver = (portaudio_eth_driver*)netif->state;
    quiet_portaudio_decoder_consume(driver->decoder);
    bool frame_open = quiet_portaudio_decoder_frame_in_progress(driver->decoder);
    if (!frame_open) {
        if (driver->rx_wait_peer_frame < driver->rx_wait_peer_frame_thresh) {
            driver->rx_wait_peer_frame++;
            if (driver->rx_wait_peer_frame == driver->rx_wait_peer_frame_thresh) {
                atomic_store(&driver->rx_in_progress, false);
            }
        }
    } else {
        driver->rx_wait_peer_frame = 0;
        atomic_store(&driver->rx_in_progress, true);
    }
    quiet_lwip_portaudio_process_audio(netif);
}


// TODO add capability for quiet to calculate its bps
// based on frame length, number of samples in frame, sample rate?
unsigned int portaudio_calculate_bps(const quiet_encoder *e) {
    return 4000;
}

static err_t quiet_lwip_portaudio_init(struct netif *netif) {
    quiet_lwip_portaudio_driver_config *conf =
        (quiet_lwip_portaudio_driver_config*)netif->state;

    quiet_portaudio_encoder *e = quiet_portaudio_encoder_create(conf->encoder_opt,
            conf->encoder_device, conf->encoder_latency,
            conf->encoder_sample_rate, conf->encoder_sample_size);

    quiet_portaudio_decoder *d = quiet_portaudio_decoder_create(conf->decoder_opt,
            conf->decoder_device, conf->decoder_latency,
            conf->decoder_sample_rate, conf->decoder_sample_size);

    portaudio_eth_driver *driver = calloc(1, sizeof(portaudio_eth_driver));
    driver->encoder = e;
    driver->decoder = d;

    driver->encoder_sample_size = conf->encoder_sample_size;
    driver->decoder_sample_size = conf->decoder_sample_size;

    // TODO gate this behind half-duplex check
    atomic_init(&driver->rx_in_progress, false);
    driver->tx_in_progress = false;
    driver->rx_wait_peer_frame = 0;
    driver->rx_wait_peer_frame_thresh = 3;

    // TODO acquire this from config
    driver->frame_dump = false;

    netif->state = driver;

    netif->name[0] = 'q';
    netif->name[1] = 'u';

    // just use the default eth arp (we'll pretend to be ethernet)
    netif->output = etharp_output;
    netif->linkoutput = quiet_lwip_portaudio_encode_frame;

    NETIF_INIT_SNMP(netif, snmp_ifType_other, portaudio_calculate_bps(e));

    netif->hwaddr_len = ETHARP_HWADDR_LEN;

    memcpy(netif->hwaddr, conf->hardware_addr, 6);

    size_t frame_len = quiet_portaudio_encoder_get_frame_len(e);

    netif->mtu = frame_len - PBUF_LINK_HLEN;
    driver->send_temp_len = frame_len + ETH_PAD_SIZE;
    driver->send_temp = malloc(driver->send_temp_len * sizeof(uint8_t));
    driver->recv_temp_len = frame_len + ETH_PAD_SIZE;
    driver->recv_temp = malloc(driver->recv_temp_len * sizeof(uint8_t));

    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

    return ERR_OK;
}

quiet_lwip_portaudio_interface *quiet_lwip_portaudio_create(quiet_lwip_portaudio_driver_config *conf,
                                                            quiet_lwip_ipv4_addr local_address,
                                                            quiet_lwip_ipv4_addr netmask,
                                                            quiet_lwip_ipv4_addr gateway) {
    static bool has_lwip_init = false;

    if (!has_lwip_init) {
        tcpip_init(NULL, NULL);
        has_lwip_init = true;
    }

    quiet_lwip_portaudio_interface *interface = malloc(1 * sizeof(quiet_lwip_portaudio_interface));
    struct ip_addr addr, nm, gw;
    addr.addr = local_address;
    nm.addr = netmask;
    gw.addr = gateway;
    netif_add(interface, &addr, &nm,
              &gw, conf, quiet_lwip_portaudio_init,
              tcpip_input);
    netif_set_up(interface);
    return interface;
}

void quiet_lwip_portaudio_destroy(quiet_lwip_portaudio_interface *interface) {
    netif_set_down(interface);
    netif_remove(interface);
    free(interface);
}

typedef struct {
    quiet_lwip_portaudio_interface *interface;
    _Atomic bool shutdown;
} audio_loop_args;

static void *emit_loop(void *args_v) {
    audio_loop_args *args = (audio_loop_args*)args_v;
    quiet_lwip_portaudio_interface *interface = args->interface;
    for (;;) {
        if (!quiet_lwip_portaudio_get_next_audio_packet(interface)) {
            if (atomic_load(&args->shutdown)) {
                pthread_exit(NULL);
            }
        }
    }
}

static void *recv_loop(void *args_v) {
    audio_loop_args *args = (audio_loop_args*)args_v;
    quiet_lwip_portaudio_interface *interface = args->interface;
    for (;;) {
        if (atomic_load(&args->shutdown)) {
            pthread_exit(NULL);
        }
        quiet_lwip_portaudio_recv_audio_packet(interface);
    }
}

static pthread_t start_emit_thread(audio_loop_args *args) {
    pthread_t emit_thread;
    pthread_attr_t thread_attr;

    pthread_attr_init(&thread_attr);
    pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE);

    pthread_create(&emit_thread, &thread_attr, emit_loop, args);

    pthread_attr_destroy(&thread_attr);

    return emit_thread;
}

static pthread_t start_recv_thread(audio_loop_args *args) {
    pthread_t recv_thread;
    pthread_attr_t thread_attr;

    pthread_attr_init(&thread_attr);
    pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE);

    pthread_create(&recv_thread, &thread_attr, recv_loop, args);

    pthread_attr_destroy(&thread_attr);

    return recv_thread;
}

struct quiet_lwip_portaudio_audio_threads {
    pthread_t emit_thread;
    pthread_t recv_thread;
    audio_loop_args emit_args;
    audio_loop_args recv_args;
};

quiet_lwip_portaudio_audio_threads *quiet_lwip_portaudio_start_audio_threads(quiet_lwip_portaudio_interface *interface) {
    quiet_lwip_portaudio_audio_threads *threads = calloc(1, sizeof(quiet_lwip_portaudio_audio_threads));

    threads->recv_args.interface = interface;
    atomic_init(&threads->recv_args.shutdown, false);
    threads->recv_thread = start_recv_thread(&threads->recv_args);

    threads->emit_args.interface = interface;
    atomic_init(&threads->emit_args.shutdown, false);
    threads->emit_thread = start_emit_thread(&threads->emit_args);

    return threads;
}

void quiet_lwip_portaudio_stop_audio_threads(quiet_lwip_portaudio_audio_threads *threads) {
    atomic_store(&threads->emit_args.shutdown, true);
    atomic_store(&threads->recv_args.shutdown, true);

    pthread_join(threads->emit_thread, NULL);
    pthread_join(threads->recv_thread, NULL);

    free(threads);
}
