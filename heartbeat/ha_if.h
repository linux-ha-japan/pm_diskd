#ifndef _HA_IF_H
#define _HA_IF_H

#include <sys/socket.h>

int if_get_broadaddr(const char *ifn, struct in_addr *broadaddr);

#endif /* _HA_IF_H */
