#ifndef QUIET_LWIP_NETDB_H
#define QUIET_LWIP_NETDB_H

#include "quiet-lwip/lwip-socket.h"

struct lwip_hostent {
  char *h_name;
  char **h_aliases;
  int h_addrtype;
  int h_length;
  char **h_addr_list;
};

struct lwip_addrinfo {
  int ai_flags;
  int ai_family;
  int ai_socktype;
  int ai_protocol;
  lwip_socklen_t ai_addrlen;
  struct lwip_sockaddr  *ai_addr;
  char *ai_canonname;
  struct lwip_addrinfo *ai_next;
};

struct hostent *lwip_gethostbyname(const char *name);
int lwip_gethostbyname_r(const char *name, struct lwip_hostent *ret, char *buf,
                         size_t buflen, struct lwip_hostent **result, int *h_errnop);
void lwip_freeaddrinfo(struct lwip_addrinfo *ai);
int lwip_getaddrinfo(const char *nodename, const char *servname,
                     const struct lwip_addrinfo *hints, struct lwip_addrinfo **res);

uint32_t lwip_inet_addr(const char *cp);
int lwip_inet_aton(const char *cp, struct lwip_in_addr *addr);
char *lwip_inet_ntoa(struct lwip_in_addr *addr);
char *lwip_inet_ntoa_r(struct lwip_in_addr *addr, char *buf, int buflen);

#endif
