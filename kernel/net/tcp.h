
#pragma once

#include <tilck/common/basic_defs.h>

#include <tilck/kernel/net.h>
#include <tilck/kernel/sys_types.h>

struct tcp_segment;

/*
 * See RFC 9293, Section 3.3.2. State Machine Overview
 */
enum tcp_state {
    TCP_STATE_CLOSED = 0,   /* represents no connection state at all */
    TCP_STATE_LISTEN,       /* represents waiting for a connection request from any remote TCP peer and port */
    TCP_STATE_SYN_SENT,     /* represents waiting for a matching connection request after having sent a connection request. */
    TCP_STATE_SYN_RECEIVED, /* represents waiting for a confirming connection request acknowledgment after having both received and sent a connection request */
    TCP_STATE_ESTABLISHED,  /* represents an open connection, data received can be delivered to the user.  The normal state for the data transfer phase of the connection */
    TCP_STATE_FIN_WAIT_1,   /* represents waiting for a connection termination request from the remote TCP peer, or an acknowledgment of the connection termination request previously sent */
    TCP_STATE_FIN_WAIT_2,   /* represents waiting for a connection termination request from the remote TCP peer */
    TCP_STATE_CLOSE_WAIT,   /* represents waiting for a connection termination request from the local user */
    TCP_STATE_LAST_ACK,     /* represents waiting for an acknowledgment of the connection termination request previously sent to the remote TCP peer (this termination request sent to the remote TCP peer already included an acknowledgment of the termination request sent from the remote TCP peer) */
    TCP_STATE_TIME_WAIT,    /* represents waiting for enough time to pass to be sure the remote TCP peer received the acknowledgment of its connection termination request and to avoid new connections being impacted by delayed segments from previous connections */
    TCP_STATE_CLOSING,      /* represents waiting for a connection termination request acknowledgment from the remote TCP peer */
};

struct tcp_listener {
};

struct tcp_byte_queue {
    char*  data;
    size_t head;
    size_t used;
    size_t size;
    bool   fin;
};

struct tcp_conn {

    ip_addr peer_ip;
    u16     peer_port;

    enum tcp_state state;

    /*
     * See RFC 9293, Section 3.3.1. Key Connection State Variables
     */

    u32 snd_una; /* send unacknowledged */
    u32 snd_nxt; /* send next */
    u32 snd_wnd; /* send window */
    u32 snd_up;  /* send urgent pointer */
    u32 snd_wl1; /* segment sequence number used for last window update */
    u32 snd_wl2; /* segment acknowledgment number used for last window update */
    u32 iss;     /* initial send sequence number */

    u32 rcv_nxt; /* receive next */
    u32 rcv_wnd; /* receive window */
    u32 rcv_up;  /* receive urgent pointer */
    u32 irs;     /* initial receive sequence number */

    struct tcp_byte_queue input;
    struct tcp_byte_queue output;
};

enum tcp_socktype {
    TCP_TYPE_UNCONFIGURED,
    TCP_TYPE_CONNECTION,
    TCP_TYPE_LISTENER,
};

struct tcp_socket {
    enum tcp_socktype type;
    struct list_node node;
    union {
        struct tcp_listener listener;
        struct tcp_conn conn;
    };
};

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
