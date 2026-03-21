
#pragma once

#include <tilck/common/basic_defs.h>

#include <tilck/kernel/net.h>
#include <tilck/kernel/list.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/sys_types.h>

struct udp_socket;

void init_udp(void);

void udp_socket_init(struct udp_socket *s);
void udp_socket_free(struct udp_socket *s);
int  udp_socket_read_ready(struct udp_socket *s);
int  udp_socket_write_ready(struct udp_socket *s);
int  udp_socket_except_ready(struct udp_socket *s);

void udp_process_datagram(void *src, size_t len, ip_addr sender_addr);

int udp_create(struct udp_socket **s);

void udp_free(struct udp_socket *s);

int udp_bind(struct udp_socket *s, const struct sockaddr *addr,
    socklen_t addrlen);

int udp_listen(struct udp_socket *s, int backlog);

int udp_accept(struct udp_socket *s,
    struct sockaddr *dst_addr, socklen_t *addr_len);

int udp_connect(struct udp_socket *s,
    const struct sockaddr *dst_addr, socklen_t addr_len);

int udp_recvfrom(struct udp_socket *s, void *buf,
    size_t len, int flags, struct sockaddr *src_addr,
    socklen_t *addrlen);

int udp_sendto(struct udp_socket *s, const void *buf,
    size_t len, int flags, const struct sockaddr *dest_addr,
    socklen_t dest_len);
