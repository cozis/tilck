
#pragma once

#include <tilck/common/basic_defs.h>

#include <tilck/kernel/net.h>
#include <tilck/kernel/sys_types.h>

struct tcp_socket;
struct tcp_segment;

void init_tcp(void);

void tcp_socket_init(struct tcp_socket *s);
void tcp_socket_free(struct tcp_socket *s);
int  tcp_socket_read_ready(struct tcp_socket *s);
int  tcp_socket_write_ready(struct tcp_socket *s);
int  tcp_socket_except_ready(struct tcp_socket *s);

int tcp_listen(struct tcp_socket *s, int backlog);

int tcp_accept(struct tcp_socket *s,
    struct sockaddr *dst_addr, socklen_t *addr_len);

int tcp_connect(struct tcp_socket *s,
    const struct sockaddr *dst_addr, socklen_t addr_len);

int tcp_recvfrom(struct tcp_socket *s, void *buf,
    size_t len, int flags, struct sockaddr *src_addr,
    socklen_t *addrlen);

int tcp_sendto(struct tcp_socket *s, const void *buf,
    size_t len, int flags, const struct sockaddr *dest_addr,
    socklen_t dest_len);

void tcp_process_segment(struct tcp_segment *segment,
    size_t len, ip_addr sender_addr);
