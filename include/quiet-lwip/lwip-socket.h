#ifndef QUIET_LWIP_SOCKET_H
#define QUIET_LWIP_SOCKET_H
#include <stdint.h>
#include <netinet/in.h>

struct lwip_in_addr {
  uint32_t s_addr;
};

struct lwip_sockaddr_in {
  uint8_t sin_len;
  uint8_t sin_family;
  uint16_t sin_port;
  struct lwip_in_addr sin_addr;
  char sin_zero[8];
};

struct lwip_sockaddr {
  uint8_t sa_len;
  uint8_t sa_family;
  char sa_data[14];
};

typedef uint32_t lwip_socklen_t;

int lwip_accept(int s, struct lwip_sockaddr *addr, lwip_socklen_t *addrlen);
int lwip_bind(int s, const struct lwip_sockaddr *name, lwip_socklen_t namelen);
int lwip_shutdown(int s, int how);
int lwip_getpeername (int s, struct lwip_sockaddr *name, lwip_socklen_t *namelen);
int lwip_getsockname (int s, struct lwip_sockaddr *name, lwip_socklen_t *namelen);
int lwip_getsockopt (int s, int level, int optname, void *optval, lwip_socklen_t *optlen);
int lwip_setsockopt (int s, int level, int optname, const void *optval, lwip_socklen_t optlen);
int lwip_close(int s);
int lwip_connect(int s, const struct lwip_sockaddr *name, lwip_socklen_t namelen);
int lwip_listen(int s, int backlog);
int lwip_recv(int s, void *mem, size_t len, int flags);
int lwip_read(int s, void *mem, size_t len);
int lwip_recvfrom(int s, void *mem, size_t len, int flags,
      struct lwip_sockaddr *from, lwip_socklen_t *fromlen);
int lwip_send(int s, const void *dataptr, size_t size, int flags);
int lwip_sendto(int s, const void *dataptr, size_t size, int flags,
    const struct lwip_sockaddr *to, lwip_socklen_t tolen);
int lwip_socket(int domain, int type, int protocol);
int lwip_write(int s, const void *dataptr, size_t size);
int lwip_select(int maxfdp1, fd_set *readset, fd_set *writeset, fd_set *exceptset,
                struct timeval *timeout);
int lwip_ioctl(int s, long cmd, void *argp);
int lwip_fcntl(int s, int cmd, int val);

/* Socket protocol types (TCP/UDP/RAW) */
#define LWIP_SOCK_STREAM     1
#define LWIP_SOCK_DGRAM      2
#define LWIP_SOCK_RAW        3

/*
 * Option flags per-socket.
 */
#define  LWIP_SO_DEBUG       0x0001 /* Unimplemented: turn on debugging info recording */
#define  LWIP_SO_ACCEPTCONN  0x0002 /* socket has had listen() */
#define  LWIP_SO_REUSEADDR   0x0004 /* Allow local address reuse */
#define  LWIP_SO_KEEPALIVE   0x0008 /* keep connections alive */
#define  LWIP_SO_DONTROUTE   0x0010 /* Unimplemented: just use interface addresses */
#define  LWIP_SO_BROADCAST   0x0020 /* permit to send and to receive broadcast messages (see IP_SOF_BROADCAST option) */
#define  LWIP_SO_USELOOPBACK 0x0040 /* Unimplemented: bypass hardware when possible */
#define  LWIP_SO_LINGER      0x0080 /* linger on close if data present */
#define  LWIP_SO_OOBINLINE   0x0100 /* Unimplemented: leave received OOB data in line */
#define  LWIP_SO_REUSEPORT   0x0200 /* Unimplemented: allow local address & port reuse */

#define  LWIP_SO_DONTLINGER   ((int)(~LWIP_SO_LINGER))

/*
 * Additional options, not kept in so_options.
 */
#define LWIP_SO_SNDBUF    0x1001    /* Unimplemented: send buffer size */
#define LWIP_SO_RCVBUF    0x1002    /* receive buffer size */
#define LWIP_SO_SNDLOWAT  0x1003    /* Unimplemented: send low-water mark */
#define LWIP_SO_RCVLOWAT  0x1004    /* Unimplemented: receive low-water mark */
#define LWIP_SO_SNDTIMEO  0x1005    /* Unimplemented: send timeout */
#define LWIP_SO_RCVTIMEO  0x1006    /* receive timeout */
#define LWIP_SO_ERROR     0x1007    /* get error status and clear */
#define LWIP_SO_TYPE      0x1008    /* get socket type */
#define LWIP_SO_CONTIMEO  0x1009    /* Unimplemented: connect timeout */
#define LWIP_SO_NO_CHECK  0x100a    /* don't create UDP checksum */

#define LWIP_SOL_SOCKET   0xfff    /* options for socket level */

#define LWIP_AF_UNSPEC       0
#define LWIP_AF_INET         2
#define LWIP_PF_INET         LWIP_AF_INET
#define LWIP_PF_UNSPEC       LWIP_AF_UNSPEC

#define LWIP_IPPROTO_IP      0
#define LWIP_IPPROTO_TCP     6
#define LWIP_IPPROTO_UDP     17
#define LWIP_IPPROTO_UDPLITE 136

/* Flags we can use with send and recv. */
#define LWIP_MSG_PEEK       0x01    /* Peeks at an incoming message */
#define LWIP_MSG_WAITALL    0x02    /* Unimplemented: Requests that the function block until the full amount of data requested can be returned */
#define LWIP_MSG_OOB        0x04    /* Unimplemented: Requests out-of-band data. The significance and semantics of out-of-band data are protocol-specific */
#define LWIP_MSG_DONTWAIT   0x08    /* Nonblocking i/o for this operation only */
#define LWIP_MSG_MORE       0x10    /* Sender will send more */

/*
 * Options for level IPPROTO_IP
 */
#define LWIP_IP_TOS             1
#define LWIP_IP_TTL             2

/*
 * Options for level IPPROTO_TCP
 */
#define LWIP_TCP_NODELAY    0x01    /* don't delay send to coalesce packets */
#define LWIP_TCP_KEEPALIVE  0x02    /* send KEEPALIVE probes when idle for pcb->keep_idle milliseconds */
#define LWIP_TCP_KEEPIDLE   0x03    /* set pcb->keep_idle  - Same as TCP_KEEPALIVE, but use seconds for get/setsockopt */
#define LWIP_TCP_KEEPINTVL  0x04    /* set pcb->keep_intvl - Use seconds for get/setsockopt */
#define LWIP_TCP_KEEPCNT    0x05    /* set pcb->keep_cnt   - Use number of probes sent for get/setsockopt */

/*
 * The Type of Service provides an indication of the abstract
 * parameters of the quality of service desired.  These parameters are
 * to be used to guide the selection of the actual service parameters
 * when transmitting a datagram through a particular network.  Several
 * networks offer service precedence, which somehow treats high
 * precedence traffic as more important than other traffic (generally
 * by accepting only traffic above a certain precedence at time of high
 * load).  The major choice is a three way tradeoff between low-delay,
 * high-reliability, and high-throughput.
 * The use of the Delay, Throughput, and Reliability indications may
 * increase the cost (in some sense) of the service.  In many networks
 * better performance for one of these parameters is coupled with worse
 * performance on another.  Except for very unusual cases at most two
 * of these three indications should be set.
 */
#define LWIP_IPTOS_TOS_MASK          0x1E
#define LWIP_IPTOS_TOS(tos)          ((tos) & LWIP_IPTOS_TOS_MASK)
#define LWIP_IPTOS_LOWDELAY          0x10
#define LWIP_IPTOS_THROUGHPUT        0x08
#define LWIP_IPTOS_RELIABILITY       0x04
#define LWIP_IPTOS_LOWCOST           0x02
#define LWIP_IPTOS_MINCOST           LWIP_IPTOS_LOWCOST

/*
 * The Network Control precedence designation is intended to be used
 * within a network only.  The actual use and control of that
 * designation is up to each network. The Internetwork Control
 * designation is intended for use by gateway control originators only.
 * If the actual use of these precedence designations is of concern to
 * a particular network, it is the responsibility of that network to
 * control the access to, and use of, those precedence designations.
 */
#define LWIP_IPTOS_PREC_MASK                 0xe0
#define LWIP_IPTOS_PREC(tos)                ((tos) & LWIP_IPTOS_PREC_MASK)
#define LWIP_IPTOS_PREC_NETCONTROL           0xe0
#define LWIP_IPTOS_PREC_INTERNETCONTROL      0xc0
#define LWIP_IPTOS_PREC_CRITIC_ECP           0xa0
#define LWIP_IPTOS_PREC_FLASHOVERRIDE        0x80
#define LWIP_IPTOS_PREC_FLASH                0x60
#define LWIP_IPTOS_PREC_IMMEDIATE            0x40
#define LWIP_IPTOS_PREC_PRIORITY             0x20
#define LWIP_IPTOS_PREC_ROUTINE              0x00

/*
 * Commands for ioctlsocket(),  taken from the BSD file fcntl.h.
 * lwip_ioctl only supports FIONREAD and FIONBIO, for now
 *
 * Ioctl's have the command encoded in the lower word,
 * and the size of any in or out parameters in the upper
 * word.  The high 2 bits of the upper word are used
 * to encode the in/out status of the parameter; for now
 * we restrict parameters to at most 128 bytes.
 */
#define LWIP_IOCPARM_MASK    0x7fU           /* parameters must be < 128 bytes */
#define LWIP_IOC_VOID        0x20000000UL    /* no parameters */
#define LWIP_IOC_OUT         0x40000000UL    /* copy out parameters */
#define LWIP_IOC_IN          0x80000000UL    /* copy in parameters */
#define LWIP_IOC_INOUT       (LWIP_IOC_IN|LWIP_IOC_OUT)
                                        /* 0x20000000 distinguishes new &
                                           old ioctl's */
#define _LWIP_IO(x,y)        (LWIP_IOC_VOID|((x)<<8)|(y))

#define _LWIP_IOR(x,y,t)     (LWIP_IOC_OUT|(((long)sizeof(t)&LWIP_IOCPARM_MASK)<<16)|((x)<<8)|(y))

#define _LWIP_IOW(x,y,t)     (LWIP_IOC_IN|(((long)sizeof(t)&LWIP_IOCPARM_MASK)<<16)|((x)<<8)|(y))

#define LWIP_FIONREAD    _LWIP_IOR('f', 127, unsigned long) /* get # bytes to read */
#define LWIP_FIONBIO     _LWIP_IOW('f', 126, unsigned long) /* set/clear non-blocking i/o */

/* commands for fnctl */
#define LWIP_F_GETFL 3
#define LWIP_F_SETFL 4

/* File status flags and file access modes for fnctl,
   these are bits in an int. */
#define LWIP_O_NONBLOCK  1 /* nonblocking I/O */
#define LWIP_O_NDELAY    1 /* same as O_NONBLOCK, for compatibility */

#define LWIP_SHUT_RD   0
#define LWIP_SHUT_WR   1
#define LWIP_SHUT_RDWR 2

#endif
