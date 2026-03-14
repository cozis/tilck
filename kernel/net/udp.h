
#pragma once

#include <tilck/common/basic_defs.h>

#include <tilck/kernel/net.h>
#include <tilck/kernel/list.h>
#include <tilck/kernel/sync.h>
#include <tilck/kernel/sys_types.h>

struct udp_datagram {
    u16 src_port;
    u16 dst_port;
    u16 length;
    u16 checksum;
};

struct message {
    struct list_node node;
    ip_addr sender_addr;
    u16     sender_port;
    size_t  size;
    char    data[];
};

struct udp_socket {
    struct list_node node;
    struct list messages;
    int num_messages;
    struct kmutex lock;
    struct kcond  message_available;
};

void init_udp(void);

void udp_socket_init(struct udp_socket *s);
void udp_socket_free(struct udp_socket *s);
int  udp_socket_read_ready(struct udp_socket *s);
int  udp_socket_write_ready(struct udp_socket *s);
int  udp_socket_except_ready(struct udp_socket *s);

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

void udp_process_datagram(void *src, size_t len, ip_addr sender_addr);
