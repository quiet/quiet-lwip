#include "relay.h"

relay_conn *relay_conn_create(int native_fd, int lwip_fd, size_t buf_len) {
    relay_conn *conn = calloc(1, sizeof(relay_conn));

    conn->fds[agent_native] = native_fd;
    fcntl(native_fd, F_SETFL, O_NONBLOCK);
    conn->fds[agent_lwip] = lwip_fd;
    lwip_fcntl(lwip_fd, F_SETFL, O_NONBLOCK);

    for (size_t i = 0; i < 2; i++) {
        conn->bufs[i] = malloc(buf_len * sizeof(uint8_t));
        conn->buflens[i] = buf_len;
    }

    atomic_init(&conn->close_count, 0);

    return conn;
}

int relay_conn_fd(const relay_conn *conn, int agent) {
    return conn->fds[agent];
}

uint8_t *relay_conn_read_buf(const relay_conn *conn, int agent) {
    return conn->bufs[agent];
}

size_t relay_conn_read_buflen(const relay_conn *conn, int agent) {
    return conn->buflens[agent];
}

uint8_t *relay_conn_write_buf(const relay_conn *conn, int agent) {
    // this is *not* the same as read buf because we write to the buf the other reads
    agent = (agent + 1) % 2;
    return conn->bufs[agent];
}

size_t relay_conn_write_buflen(const relay_conn *conn, int agent) {
    agent = (agent + 1) % 2;
    return conn->buflens[agent];
}

void relay_conn_destroy(relay_conn *conn) {
    printf("destroying relay connection\n");
    for (size_t i = 0; i < 2; i++) {
        free(conn->bufs[i]);
    }

    lwip_close(conn->fds[agent_lwip]);
    close(conn->fds[agent_native]);

    free(conn);
}

void relay_conn_close(relay_conn *conn) {
    int expected = 0;
    bool exchanged = atomic_compare_exchange_strong(&conn->close_count, &expected, 1);

    if (exchanged) {
        return;
    }

    // we weren't able to swap in a 0, so it must already be a 1
    // collect this item
    relay_conn_destroy(conn);
}

void crossbar_init(crossbar *c) {
    pthread_mutex_init(&c->mutex, NULL);
    pthread_cond_init(&c->cond, NULL);
    c->ready_for_reading_len = 0;
    c->ready_for_writing_len = 0;
}

void crossbar_add_for_reading(crossbar *c, relay_conn *conn) {
    pthread_mutex_lock(&c->mutex);
    c->ready_for_reading[c->ready_for_reading_len] = conn;
    c->ready_for_reading_len++;
    pthread_cond_signal(&c->cond);
    pthread_mutex_unlock(&c->mutex);
}

void crossbar_add_for_writing(crossbar *c, relay_conn *conn, size_t buflen) {
    pthread_mutex_lock(&c->mutex);
    c->ready_for_writing[c->ready_for_writing_len] = conn;
    c->write_len[c->ready_for_writing_len] = buflen;
    c->ready_for_writing_len++;
    pthread_cond_signal(&c->cond);
    pthread_mutex_unlock(&c->mutex);
}

int native_errno() {
    return errno;
}

int lwip_errno() {
    // lwip can provide errno, but it doesn't namespace (e.g. lwip_errno) if it does
    // since that would clobber our native errno, we just have to make do with nothing
    return 0;
}

ssize_t _lwip_read(int desc, void *buf, size_t nbytes) {
    // for some reason, lwip returns int instead of ssize_t
    return (ssize_t)(lwip_read(desc, buf, nbytes));
}

ssize_t _lwip_write(int desc, const void *buf, size_t nbytes) {
    // for some reason, lwip returns int instead of ssize_t
    return (ssize_t)(lwip_write(desc, buf, nbytes));
}

void *relay(void *v_relay) {
    relay_t *relay = (relay_t*)v_relay;

    relay_conn *relay_read[num_relays];
    relay_conn *relay_write[num_relays];
    size_t relay_read_len = 0;
    size_t relay_write_len = 0;
    size_t write_len[num_relays];

    fd_set read_fds;
    fd_set write_fds;

    struct timeval tv;

    for (;;) {
        // read our crossbar
        pthread_mutex_lock(&relay->incoming->mutex);
        if (relay_read_len == 0 && relay_write_len == 0) {
            // relay_read_len and relay_write_len are the length of our extant connection arrays
            //   if they're both empty, then we need to wait for work
            //   we use a cond var to prevent spinning here while we wait
            while (relay->incoming->ready_for_writing_len == 0 && \
                    relay->incoming->ready_for_reading_len == 0) {
                pthread_cond_wait(&relay->incoming->cond, &relay->incoming->mutex);
            }
        }
        // copy the work passed to us on top of our extant connections
        memcpy(relay_read + relay_read_len, relay->incoming->ready_for_reading, relay->incoming->ready_for_reading_len * sizeof(relay_conn*));
        memcpy(relay_write + relay_write_len, relay->incoming->ready_for_writing, relay->incoming->ready_for_writing_len * sizeof(relay_conn*));
        memcpy(write_len + relay_write_len, relay->incoming->write_len, relay->incoming->ready_for_writing_len * sizeof(size_t));

        relay_read_len += relay->incoming->ready_for_reading_len;
        relay_write_len += relay->incoming->ready_for_writing_len;

        // now that we've copied, release the space in this crossbar
        relay->incoming->ready_for_reading_len = 0;
        relay->incoming->ready_for_writing_len = 0;

        pthread_mutex_unlock(&relay->incoming->mutex);

        // build fdsets for select()
        FD_ZERO(&read_fds);
        int max_fd = 0;
        for (size_t i = 0; i < relay_read_len; i++) {
            int fd = relay_conn_fd(relay_read[i], relay->agent);
            if (fd > max_fd) {
                max_fd = fd;
            }
            FD_SET(fd, &read_fds);
        }

        FD_ZERO(&write_fds);
        for (size_t i = 0; i < relay_write_len; i++) {
            int fd = relay_conn_fd(relay_write[i], relay->agent);
            if (fd > max_fd) {
                max_fd = fd;
            }
            FD_SET(fd, &write_fds);
        }

        max_fd++;  // select() asks for n+1

        // we set a timeout here so that we can read the crossbar periodically
        tv.tv_usec = 1000;
        tv.tv_sec = 0;
        int select_res = relay->select(max_fd, &read_fds, &write_fds, NULL, &tv);

        if (select_res < 0) {
            printf("error in select()\n");
            // XXX what do?
            continue;
        } else if (select_res == 0) {
            // timeout
            // we don't need to do anything
            continue;
        }

        // now we test all of our read fds
        // if they're not set, we'll write them back into our buffer
        // we do this in place, which is ok since we can't write past the read iterator
        size_t new_read_len = 0;
        for (size_t i = 0; i < relay_read_len; i++) {
            int fd = relay_conn_fd(relay_read[i], relay->agent);
            if (FD_ISSET(fd, &read_fds)) {
                ssize_t read_res = relay->read(fd, relay_conn_read_buf(relay_read[i], relay->agent),
                                               relay_conn_read_buflen(relay_read[i], relay->agent));
                if (read_res > 0) {
                    crossbar_add_for_writing(relay->outgoing, relay_read[i], read_res);
                } else if (read_res == 0) {
                    // eof
                    relay->other_shutdown(relay_conn_fd(relay_read[i], relay->other_agent), SHUT_WR);
                    relay_conn_close(relay_read[i]);
                } else {
                    int _errno = relay->get_errno();
                    if (_errno == EAGAIN || _errno == EWOULDBLOCK) {
                        relay_read[new_read_len] = relay_read[i];
                        new_read_len++;
                    } else {
                        relay->other_shutdown(relay_conn_fd(relay_read[i], relay->other_agent), SHUT_WR);
                        relay_conn_close(relay_read[i]);
                    }
                }
            } else {
                relay_read[new_read_len] = relay_read[i];
                new_read_len++;
            }
        }
        relay_read_len = new_read_len;

        // test write fds. same idea here as read fds.
        size_t new_write_len = 0;
        for (size_t i = 0; i < relay_write_len; i++) {
            int fd = relay_conn_fd(relay_write[i], relay->agent);
            if (FD_ISSET(fd, &write_fds)) {
                ssize_t write_res = relay->write(fd, relay_conn_write_buf(relay_write[i], relay->agent),
                                                 write_len[i]);
                if (write_res > 0) {
                    if (write_res < write_len[i]) {
                        // wrote less than full message
                        // copy remaining back to start of buffer and run again next time
                        size_t remaining = write_len[i] - write_res;
                        uint8_t *buf = relay_conn_write_buf(relay_write[i], relay->agent);
                        memmove(buf, buf + write_res, remaining);

                        // now insert this connection back into the queue
                        relay_write[new_write_len] = relay_write[i];
                        write_len[new_write_len] = remaining;
                        new_write_len++;
                    } else {
                        // write completed
                        // pass buffer back to reader now that it's empty
                        crossbar_add_for_reading(relay->outgoing, relay_write[i]);
                    }
                } else if (write_res == 0) {
                    // eof
                    relay->other_shutdown(relay_conn_fd(relay_write[i], relay->other_agent), SHUT_RD);
                    relay_conn_close(relay_write[i]);
                } else {
                    int _errno = relay->get_errno();
                    if (_errno == EAGAIN || _errno == EWOULDBLOCK) {
                        relay_write[new_write_len] = relay_write[i];
                        write_len[new_write_len] = write_len[i];
                        new_write_len++;
                    } else {
                        relay->other_shutdown(relay_conn_fd(relay_write[i], relay->other_agent), SHUT_RD);
                        relay_conn_close(relay_write[i]);
                    }
                }
            } else {
                relay_write[new_write_len] = relay_write[i];
                write_len[new_write_len] = write_len[i];
                new_write_len++;
            }
        }
        relay_write_len = new_write_len;
    }
}

pthread_t start_relay_thread(relay_t *relay_obj) {
    pthread_t relay_thread;
    pthread_create(&relay_thread, NULL, relay, relay_obj);

    return relay_thread;
}
