#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>

#include "quiet-lwip/lwip-socket.h"

static const int agent_lwip = 0;
static const int agent_native = 1;

typedef struct {
    // these properties are set at creation and then not changed
    int fds[2];
    uint8_t *bufs[2];
    size_t buflens[2];

    // how many times have we called shutdown here?
    // 0: fully open
    // 1: half closed
    // 2: fully closed (collect this struct)
    _Atomic int close_count;
} relay_conn;

relay_conn *relay_conn_create(int native_fd, int lwip_fd, size_t buf_len);

void relay_conn_destroy(relay_conn *conn);

#define num_relays (256)

typedef struct {
    relay_conn *ready_for_writing[num_relays];
    size_t write_len[num_relays];
    relay_conn *ready_for_reading[num_relays];
    size_t ready_for_writing_len;
    size_t ready_for_reading_len;
    pthread_cond_t cond;
    pthread_mutex_t mutex;
} crossbar;

void crossbar_init(crossbar *c);

void crossbar_add_for_reading(crossbar *c, relay_conn *conn);

void crossbar_add_for_writing(crossbar *c, relay_conn *conn, size_t buflen);

int native_errno();

int lwip_errno();

ssize_t _lwip_read(int desc, void *buf, size_t nbytes);

ssize_t _lwip_write(int desc, const void *buf, size_t nbytes);

typedef struct {
    int agent;
    int other_agent;
    crossbar *incoming;
    crossbar *outgoing;
    ssize_t (*read)(int desc, void *buf, size_t nbytes);
    ssize_t (*write)(int desc, const void *buf, size_t nbytes);
    int (*select)(int num_fds, fd_set *read_fds, fd_set *write_fds,
                  fd_set *except_fds, struct timeval *timeout);
    int (*other_shutdown)(int fd, int how);
    int (*get_errno)();
} relay_t;

pthread_t start_relay_thread(relay_t *relay_obj);
