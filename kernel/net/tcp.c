#include <tilck/kernel/list.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/kmalloc.h>

#include "tcp.h"
#include "utils.h"
#include "endian.h"

#define TCP_FLAG_FIN  0x01
#define TCP_FLAG_SYN  0x02
#define TCP_FLAG_RST  0x04
#define TCP_FLAG_PUSH 0x08
#define TCP_FLAG_ACK  0x10
#define TCP_FLAG_URG  0x20

struct tcp_segment {
    u16  src_port;
    u16  dst_port;
    u32  seq_no;
    u32  ack_no;
    u8   offset1: 4; // When CPU is big endian
    u8   offset2: 4; // When CPU is little endian
    u8   flags;
    u16  window;
    u16  checksum;
    u16  urgent_pointer;
    char payload[];
} __attribute__((packed));

STATIC_ASSERT(sizeof(struct tcp_segment) == 20);

enum tcp_user_ret {
    TCP_USER_RET_VOID,
    TCP_USER_RET_CLOSE,
    TCP_USER_RET_RESET,
    TCP_USER_RET_REFUSED,
};

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

struct byte_queue {
    char*  data;
    size_t head;
    size_t used;
    size_t size;
    bool   fin;
};

struct tcp_listener {

    struct list_node node; /* tcp_listeners list */

    struct list  accept_queue;
    struct kcond accept_ready;
};

struct tcp_conn {

    struct list_node node; /* tcp_conns list */
    struct list_node accept_queue_node; /* parent listener's accept queue */

    enum tcp_state state;

    ip_addr peer_addr;
    u16     peer_port;

    struct byte_queue input;
    struct byte_queue output;

    enum tcp_user_ret user_ret;
    struct kcond  input_buffered;
    struct kcond  output_flushed;

    /*  See RFC 9293, Section 3.3.1 */
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
};

enum tcp_type {
    TCP_TYPE_UNSPEC,
    TCP_TYPE_CONN,
    TCP_TYPE_LISTENER,
};

struct tcp_socket {
    enum tcp_type type;

    bool is_bound;
    ip_addr bound_addr;
    u16     bound_port;

    struct kmutex mutex;
    union {
        struct tcp_conn conn;
        struct tcp_listener listener;
    };
};

static struct list tcp_conns;
static struct list tcp_listeners;

static u16 next_ephimeral_port = EPHIMERAL_PORT_MIN;

struct tcp_conn *tcp_conn_init(struct tcp_listener *parent,
    ip_addr peer_addr, u16 peer_port)
{
    struct tcp_conn *conn = kmalloc(sizeof(struct tcp_conn));
    if (!conn)
        return NULL;

    /*
     * Initialize the base structure and bound the newly
     * created connection to the same address as the parent
     * listener.
     */

    ASSERT(parent->base.is_bound);
    conn->base.is_conn = true;
    conn->base.is_bound = true;
    conn->base.bound_addr = parent->base.bound_addr;
    conn->base.bound_port = parent->base.bound_port;
    if (kmutex_init(&conn->base.mutex) < 0) {
        kfree(conn);
        return NULL;
    }

    /*
     * Add connection to the global list and the accept queue
     */

    list_insert(&tcp_conns, &conn->node);
    list_insert(&listener->accept_queue, &conn->accept_queue_node);

    /*
     * Other stuff
     */

    if (byte_queue_init(&conn->input, xxx) < 0) {
        kmutex_free(&conn->mutex);
        kfree(conn);
        return NULL;
    }

    if (byte_queue_init(&conn->output, xxx) < 0) {
        byte_queue_free(&conn->input);
        kmutex_free(&conn->mutex);
        kfree(conn);
        return NULL;
    }

    if (kcond_init(&conn->input_buffered) < 0) {
        byte_queue_free(&conn->output);
        byte_queue_free(&conn->input);
        kmutex_free(&conn->mutex);
        kfree(conn);
        return NULL;
    }

    if (kcond_init(&conn->output_flushed) < 0) {
        kcond_destroy(&con->input_buffered);
        byte_queue_free(&conn->output);
        byte_queue_free(&conn->input);
        kmutex_free(&conn->mutex);
        kfree(conn);
        return NULL;
    }

    // TODO: init everything else

    return 0;
}

static void init_segment(struct tcp_segment *seg,
    ip_addr self_ip, ip_addr peer_ip, u16 src_port,
    u16 dst_port, int flags, u32 seq, u32 ack,
    u32 window)
{
    int offset = 5; // No options
    seg->src_port = cpu_to_net_u16(src_port);
    seg->dst_port = cpu_to_net_u16(dst_port);
    seg->flags    = flags;
    seg->seq_no   = cpu_to_net_u32(seq);
    seg->ack_no   = cpu_to_net_u32(ack);
    seg->offset1  = cpu_is_little_endian() ? 0 : offset;
    seg->offset2  = cpu_is_little_endian() ? offset : 0;
    seg->window   = cpu_to_net_u16(window); // Why is a 32 bit integer being backed into a 16 bit?
    seg->checksum = 0; // Will be calculated later
    seg->urgent_pointer = 0;

    seg->checksum = calculate_checksum_l4(self_ip, peer_ip, IP_PROTO_TCP, seg, sizeof(struct tcp_segment));
}

static void send_segment(ip_addr src_addr, ip_addr dst_addr,
                         u16 src_port, u16 dst_port, int flags,
                         u32 seq, u32 ack)
{
    size_t dummy;
    struct tcp_segment *seg = ip_send_begin(sizeof(struct tcp_segment), &dummy, true);
    if (!seg) {
        ASSERT(0); // TODO
    }

    init_segment(seg, src_addr, dst_addr,
        src_port, dst_port, flags,
        seq, ack, window);

    seg->checksum = calculate_checksum_l4(src_addr, dst_addr, IP_PROTO_TCP, seg, sizeof(struct tcp_segment));
    ip_send_complete(dst_ip, IP_PROTO_TCP);
}

static void
send_segment_from_conn(struct tcp_conn *conn, int flags)
{
    send_segment(conn->base.bound_addr, conn->peer_addr,
                 conn->base.bound_port, conn->peer_port,
                 flags, conn->iss, conn->rcv_nxt); /* TODO: should not be conn->iss here */
}


static bool is_fin(struct tcp_segment *seg)
{
    return seg->flags & TCP_FLAG_FIN;
}

static bool is_rst(struct tcp_segment *seg)
{
    return seg->flags & TCP_FLAG_RST;
}

static bool is_ack(struct tcp_segment *seg)
{
    return seg->flags & TCP_FLAG_ACK;
}

static bool is_push(struct tcp_segment *seg)
{
    return seg->flags & TCP_FLAG_PUSH;
}

static bool is_urg(struct tcp_segment *seg)
{
    return seg->flags & TCP_FLAG_URG;
}

static void state_closed(struct tcp_segment *seg)
{
    if (!is_rst(seg)) {
        send_segment(local_addr,
                     sender_addr,
                     seg->dst_port,
                     seg->src_port,
                     TCP_FLAG_RST,
                     seg->ack_no,
                     is_ack(seg) ? seg->ack_no : 0);
    }
}

/*
 * This function processes segments that address a
 * listening socket.
 */
static void state_listen(struct tcp_listener *listener, struct tcp_segment *seg)
{
    /*
     * Since the socket is in the LISTEN state, no previous
     * communication has happened, so RST and ACK segments
     * are invalid.
     */

    if (is_rst(seg))
        return; /* Ignore */

    if (is_ack(seg)) {
        send_rst(seg->ack);
        return;
    }

    /*
     * The only way to advance the state is via a SYN segment.
     */

    if (!is_syn(seg))
        return;

    struct tcp_conn *conn = tcp_conn_init(listener, peer_addr, seg->src_port);
    if (!conn)
        return; /* Error. Drop segment. */

    /*
     * SYN segments may also hold text and other controls (like
     * FIN and UP). Only the SYN must be processed at this time
     * and anything else must be buffered and processed when the
     * handshake is complete.
     *
     * Since this is a simplified implementation, we simply
     * ignore that extra stuff and expect the peer to send it
     * again once the handshake is done.
     */

    conn->rcv_nxt = seg->seq+1; /* Acknowledge the SYN */
    conn->irs     = seg->seq;   /* Remember the first sequence number chosen by the peer */
    conn->snd_nxt = conn->iss+1;
    conn->snd_una = conn->iss;

    conn->state = TCP_STATE_SYN_RECEIVED;
    conn->dont_repeat_syn_ack_processing = true;

    send_segment_from_conn(conn, TCP_FLAG_RST|TCP_FLAG_ACK);
}

static void state_syn_sent(struct tcp_conn *conn,
    struct tcp_segment *seg)
{
    if (is_ack(seg)) {
        if (seg->ack <= conn->iss || seg->ack > conn->snd_nxt) {
            if (!is_rst(seg))
                send_rst(seg->ack);
            return;
        }
    }

    if (is_rst(seg)) {

        // Signal to the user "error: connection reset"
        conn->user_ret = TCP_USER_RET_RESET;
        kcond_signal(&conn->input_buffered);
        kcond_signal(&conn->output_buffered);

        conn->state = TCP_STATE_CLOSED;
        return; /* Drop the segment */
    }

    if (is_syn(seg)) {

        conn->rcv_nxt = seg->seq+1;
        conn->irs = seg->seq;

        if (is_ack(seg)) {
            conn->snd_una = seg->ack;

            // TODO: remove segments in retransmission queue that are acked

            if (conn->snd_una > conn->iss) {
                conn->state = TCP_STATE_ESTABLISHED;
                // TODO: Now that the connection is established, should we send out bytes buffered during the handshake?
                send_segment_from_conn(conn, TCP_FLAG_ACK);
            }

            conn->snd_wnd = seg->wnd;
            conn->snd_wl1 = seg->seq;
            conn->snd_wl2 = seg->ack;

            // TODO: if there are other controls or text in the
            //       segment, queue them for processing after
            //       the ESTABLISHED state has been reached

        } else {

            /* Simultaneous open */
            conn->state = TCP_STATE_SYN_RECEIVED;
            send_segment_from_conn(conn, TCP_FLAG_SYN|TCP_FLAG_ACK);
        }
    }
}

static bool valid_sequence_number(struct tcp_segment *seg)
{
    bool accepted = false;
    if (seg->length == 0) {
        if (receive_window == 0) {
            return (seg->seq == conn->rcv_nxt);
        } else {
            return (conn->rcv_nxt <= seg->seq && seg->seq < conn->rcv_nxt + conn->rcv_wnd);
        }
    } else {
        if (receive_window == 0) {
            return false;
        } else {
            return (conn->rcv_nxt <= seg->seq && conn->rcv_nxt + conn->rcv_wnd)
                || (conn->rcv_nxt <= seg->seq + seg->len - 1 && seg->seq + seg->len - 1 < conn->rcv_nxt + conn->rcv_wnd);
        }
    }
}

static bool state_other_rst(struct tcp_conn *conn,
    struct tcp_segment *seg)
{
    if (!inside_current_recv_window(seg->seq))
        return false; /* Silently drop segment */

    /*
     * RFC 5961 check could be implemented here
     */

    if (conn->state == TCP_STATE_SYN_RECEIVED) {
        if (conn->passive_open) {
            conn->state = TCP_STATE_LISTEN;
        } else {
            // Signal "connection refused" to client
            conn->state = TCP_STATE_CLOSED;
            conn->user_ret = TCP_USER_RET_REFUSED;
            kcond_signal(&conn->input_buffered);
            kcond_signal(&conn->output_buffered);
        }
        flush_retransmission_queue();
        return false;
    }

    if (conn->state == TCP_STATE_ESTABLISHED ||
        conn->state == TCP_STATE_FIN_WAIT_1 ||
        conn->state == TCP_STATE_FIN_WAIT_2 ||
        conn->state == TCP_STATE_CLOSE_WAIT) {
        resolve_pending_calls(conn, PENDING_RECV | PENDING_SEND, ECONNRESET);
        flush_all_segment_queues();
        conn->state = TCP_STATE_CLOSED;
        return false;
    }

    if (conn->state == TCP_STATE_CLOSING ||
        conn->state == TCP_STATE_LAST_ACK ||
        conn->state == TCP_STATE_TIME_WAIT) {
        conn->state = TCP_STATE_CLOSED;
    }

    return true;
}

static bool state_other_syn(struct tcp_conn *conn,
    struct tcp_segment *seg)
{
    if (conn->state == TCP_STATE_SYN_RECEIVED) {
        if (conn->passive_open) {
            conn->state = TCP_STATE_LISTEN;
            return false;
        }
        /* fallthrough */
    }

    if (conn->state == TCP_STATE_ESTABLISHED ||
        conn->state == TCP_STATE_FIN_WAIT_1 ||
        conn->state == TCP_STATE_FIN_WAIT_2 ||
        conn->state == TCP_STATE_CLOSE_WAIT ||
        conn->state == TCP_STATE_CLOSING ||
        conn->state == TCP_STATE_LAST_ACK ||
        conn->state == TCP_STATE_TIME_WAIT) {

        if (inside_current_recv_window(conn, seg)) {
            resolve_pending_calls(conn, PENDING_RECV | PENDING_SEND, ECONNRESET);
            flush_all_segment_queues();
            send_segment_from_conn(conn, TCP_FLAG_RST);
            conn->state = TCP_STATE_CLOSED;
            return false;
        }

        /* Should be unreachable (but not sure) */
        return false;
    }

    return true;
}

static void remove_acked_segments(struct tcp_conn *conn)
{
    // TODO
}

static bool process_ack(struct tcp_conn *conn,
    struct tcp_segment *seg)
{
    if (conn->snd_una < seg->ack && seg->ack <= conn->snd_nxt) {
        conn->snd_una = seg->ack;
        remove_acked_segments(conn); // TODO
    }

    if (seg->ack > conn->snd_nxt)
        return false;

    if (conn->snd_una <= seg->ack && seg->ack <= conn->snd_nxt) {
        /* update send window */
        if (conn->snd_wl1 < seg->seq || (conn->snd_wl1 == seg->seq && conn->snd_wl2 <= seg->ack)) {
            conn->snd_wnd = seg->wnd;
            conn->snd_wl1 = seg->seq;
            conn->snd_wl2 = seg->ack;
        }
    }

    return true;
}

static bool state_other_ack(struct tcp_conn *conn,
    struct tcp_segment *seg)
{
    if (conn->state == TCP_STATE_SYN_RECEIVED) {

        if (conn->snd_una < seg->ack && seg->ack <= conn->snd_nxt) {
            conn->state = TCP_STATE_ESTABLISHED;
            conn->snd_wnd = seg->wnd;
            conn->snd_wl1 = seg->seq;
            conn->snd_wl2 = seg->ack;
            // TODO: Now that the connection is established, should we send out bytes buffered during the handshake?
            /* fallthrough to the established branch */
        } else {
            send_segment_from_conn(conn, TCP_FLAG_RST);
        }
    }

    if (conn->state == TCP_STATE_ESTABLISHED) {
        if (!process_ack(conn, seg))
            return false;
    }

    if (conn->state == TCP_STATE_FIN_WAIT_1) {

        if (!process_ack(conn, seg))
            return false;

        if (fin_was_acked(conn)) {
            conn->state = TCP_STATE_FIN_WAIT_2;
        }

    } else if (conn->state == TCP_STATE_FIN_WAIT_2) {

        if (!process_ack(conn, seg))
            return false;

        if (retransmission_queue_empty(conn)) {
            // TODO: user's CLOSE can be acked "ok" but don't delete the TCB
        }
    }

    if (conn->state == TCP_STATE_CLOSE_WAIT) {
        if (!process_ack(conn, seg))
            return false;
    }

    if (conn->state == TCP_STATE_CLOSING) {

        if (!process_ack(conn, seg))
            return false;

        if (fin_was_acked(conn)) {
            conn->state = TCP_STATE_TIME_WAIT;
        }
    }

    if (conn->state == TCP_STATE_LAST_ACK) {
        if (fin_was_acked(conn)) {
            conn->state = TCP_STATE_CLOSED;
            return false;
        }
    }

    if (conn->state == TCP_STATE_TIME_WAIT) {
        send_segment_from_conn(conn, TCP_FLAG_ACK);
        restart_2_msl_timeout(); // TODO
    }

    return true;
}

static bool state_other_urg(struct tcp_conn *conn,
    struct tcp_segment *seg)
{
    if (conn->state == TCP_STATE_ESTABLISHED ||
        conn->state == TCP_STATE_FIN_WAIT_1 ||
        conn->state == TCP_STATE_FIN_WAIT_2) {
        conn->rcv_up = MAX(conn->rcv_up, seg->up);
        signal_user(URGENT_DATA); // TODO
    }

    if (conn->state == TCP_STATE_CLOSE_WAIT ||
        conn->state == TCP_STATE_CLOSING ||
        conn->state == TCP_STATE_LAST_ACK ||
        conn->state == TCP_STATE_TIME_WAIT) {
        /* do nothing */
    }

    return true;
}

static bool state_other_text(struct tcp_conn *conn,
    struct tcp_segment *seg)
{
    if (conn->state == TCP_STATE_ESTABLISHED ||
        conn->state == TCP_STATE_FIN_WAIT_1 ||
        conn->state == TCP_STATE_FIN_WAIT_2) {

        /*
         * Copy data from the segment into the input queue, then
         * update the receive window by the amount of bytes that
         * were handled.
         */

        u32 moved = tcp_byte_queue_write(&conn->input, (char*) (seg+1), seg->len)
        if (is_push(seg)) {
            signal_user(); // TODO
        }

        conn->rcv_nxt += moved;
        conn->rcv_wnd -= moved;

        send_segment_from_conn(conn, TCP_FLAG_ACK);
    }

    if (conn->state == TCP_STATE_CLOSE_WAIT ||
        conn->state == TCP_STATE_CLOSING ||
        conn->state == TCP_STATE_LAST_ACK ||
        conn->state == TCP_STATE_TIME_WAIT) {
        return false;
    }

    return true;
}

static bool state_other_fin(struct tcp_conn *conn,
    struct tcp_segment *seg)
{
    // Signal to the user "connection closing"
    conn->user_ret = TCP_USER_RET_CLOSE;
    kcond_signal(&conn->input_buffered);
    kcond_signal(&conn->output_buffered);

    conn->rcv_nxt = xxx; // Advance RCV.NXT over the FIN
    send_segment_from_conn(conn, TCP_FLAG_ACK);

    if (conn->state == TCP_STATE_SYN_RECEIVED ||
        conn->state == TCP_STATE_ESTABLISHED) {
        conn->state = TCP_STATE_CLOSE_WAIT;
    }

    if (conn->state == TCP_STATE_FIN_WAIT_1) {
        if (fin_was_acked(conn, seg)) {
            conn->state = TCP_STATE_TIME_WAIT;
        } else {
            conn->state = TCP_STATE_CLOSING;
        }
    }

    if (conn->state == TCP_STATE_TIME_WAIT) {
        restart_2_msl_timeout(conn); // TODO
    }

    return true;
}

static void state_other(struct tcp_conn *conn,
    struct tcp_segment *seg)
{
    if (!valid_sequence_number(seg)) {
        if (!is_rst(seg))
            send_ack();
        return;
    }

    // TODO: trim the segment payload if already received or
    //       outside of the current window

    if (is_rst(seg)) {
        if (!state_other_rst(conn, seg))
            return;
    }

    if (is_syn(seg)) {
        if (!state_other_syn(conn, seg))
            return;
    }

    if (!is_ack(seg)) {
        return;
    } else {
        if (!state_other_ack(conn, seg))
            return;
    }

    if (is_urg(seg)) {
        if (!state_other_urg(conn, seg))
            return;
    }

    if (!state_other_text(conn, seg))
        return;

    if (conn->state == TCP_STATE_CLOSED ||
        conn->state == TCP_STATE_LISTEN ||
        conn->state == TCP_STATE_SYN_SENT) {
        return; /* Drop */
    }

    if (is_fin(seg)) {
        if (!state_other_fin(conn, seg))
            return;
    }
}

/*
 * Reference:
 *   https://www.ietf.org/rfc/rfc9293.html#section-3.10.7
 */
void tcp_process_segment(struct tcp_segment *seg,
    size_t len, ip_addr sender_addr)
{
    // TODO: fix endianess
    // TODO: check checksum

    struct tcp_conn *conn = find_tcp_conn(local_addr, sender_addr, seg->dst_port, seg->src_port);
    if (conn) {
        state_other(&s->conn, seg);
        if (conn->state == TCP_STATE_CLOSED) {
            // TODO: remove the struct
        }
    } else {
        struct tcp_listener *listener = find_tcp_listener(local_addr, seg->dst_port);
        if (listener) {
            state_listen(listener, seg);
        } else {
            state_closed(seg);
        }
    }
}

int tcp_create(struct tcp_socket **p)
{
    struct tcp_socket *s = kmalloc(sizeof(struct tcp_socket));
    if (!s) return -ENOMEM;

    s->type = TCP_TYPE_UNSPEC;
    s->is_bound = false;

    int ret = kmutex_init(&s->mutex);
    if (ret < 0) {
        kfree(s);
        return ret;
    }

    *p = s;
    return 0;
}

void tcp_free(struct tcp_socket *s)
{
    if (s->type == TCP_TYPE_CONN) {
        // TODO
    } else if (s->type == TCP_TYPE_LISTENER) {
        // TODO
    }
    kmutex_free(&s->mutex);
    kfree(s);
}

int tcp_bind(struct tcp_socket *s,
    const struct sockaddr *addr,
    socklen_t addrlen)
{
    if (s->is_bound)
        return -EINVAL; /* Already bound */

    if (addrlen != sizeof(struct sockaddr_in))
        return -EINVAL;

    struct sockaddr_in buf;
    if (copy_from_user(&buf, addr, sizeof(buf)) < 0)
        return -EFAULT;

    if (buf.sin_family != AF_INET)
        return -EINVAL;

    if (net_to_cpu_u32(buf.sin_addr.s_addr) == INADDR_ANY) {
        s->bound_addr = self_ip; /* TODO: Should bind to every interface here */
    } else {
        if (buf.sin_addr.s_addr != self_ip)
            return -EADDRNOTAVAIL;
        s->bound_addr = self_ip;
    }

    if (buf.sin_port == 0) {
        s->bound_port = get_ephimeral_port();
    } else {
        s->bound_port = net_to_cpu_u16(buf.sin_port);
    }

    s->is_bound = true;
    return 0;
}

int tcp_listen(struct tcp_socket *s, int backlog)
{
    if (s->type != TCP_TYPE_UNSPEC)
        return -EINVAL;

    int ret = kcond_init(&s->accept_ready);
    if (ret < 0)
        return ret;

    s->type = TCP_TYPE_LISTENER;
    if (!s->is_bound) {
        s->port = get_ephimeral_port(&next_ephimeral_port);
        s->is_bound = true;
    }
    list_insert(&tcp_listeners, &s->listener.node);
    list_init(&s->listener.accept_queue);
    return 0;
}

int tcp_accept(struct tcp_socket *s, bool block,
    struct sockaddr *dst_addr, socklen_t *addr_len,
    struct tcp_socket **pchild)
{
    if (s->is_conn)
        return -EINVAL;

    mutex_lock(&s->mutex);
    struct tcp_conn *child;
    for (;;) {
        /*
         * Look for a connection in the accept queue that
         * completed the handshake
         */

        list_foreach(xxx) {
            if (child->state == TCP_STATE_ESTABLISHED ||
                child->state == xxx)
                break;
        }

        if (child)
            break;

        if (!block) {
            mutex_unlock(&s->mutex);
            return -EAGAIN;
        }

        kcond_wait(&s->listener.accept_ready, &s->mutex);
    }
    ASSERT(child);
    mutex_unlock(&s->mutex);

    *pchild = (struct tcp_socket*) child;
    return 0;
}

static int unpack_addr(struct sockaddr *sock_addr,
    socklen_t sock_addr_len, ip_addr *addr, u16 *port)
{
    if (sock_addr_len != sizeof(struct sockaddr_in))
        return -EINVAL;
    struct sockaddr_in *tmp = (struct sockaddr_in*) sock_addr;

    if (tmp->sin_family != AF_INET)
        return -EINVAL;

    *addr = tmp->sin_addr.s_addr;
    *port = net_to_cpu_u16(tmp->sin_port);
    return 0;
}

int tcp_connect(struct tcp_socket *s,
    const struct sockaddr *addr, socklen_t addr_len)
{
    if (s->type != TCP_TYPE_UNCONFIGURED)
        return -EINVAL;

    ip_addr peer_addr;
    u16     peer_port;
    int rc = unpack_addr(addr, addr_len, &peer_addr, &peer_port);
    if (rc < 0) return rc;

    if (!s->is_bound) {
        s->port = get_ephimeral_port(&next_ephimeral_port);
        s->is_bound = true;
    }

    send_segment(self_ip, peer_ip, s->port, dst_port, TCP_FLAG_SYN, iss, 0);

    // TODO: wait for completion?
    return 0;
}

int tcp_recvfrom(struct tcp_socket *s, void *buf,
    size_t len, int flags, struct sockaddr *src_addr,
    socklen_t *addrlen)
{
    if (s->type != TCP_TYPE_CONNECTION)
        return -EINVAL;

    int num = -1;
    switch (conn->state) {
    case TCP_STATE_SYN_SENT:
        /* fallthrough */
    case TCP_STATE_SYN_RECEIVED:
        /* Handshake incomplete */
        // TODO: Block until complete?
        break;
    case TCP_STATE_ESTABLISHED:
        /* fallthrough */
    case TCP_STATE_FIN_WAIT_1:
        /* fallthrough */
    case TCP_STATE_FIN_WAIT_2:
        num = tcp_conn_read_out(conn, buf, len);
        // TODO: ack?
        // TODO: block?
        break;
    case TCP_STATE_CLOSE_WAIT:
        num = tcp_conn_read_out(conn, buf, len);
        break;
    case TCP_STATE_CLOSING:
        /* fallthrough */
    case TCP_STATE_LAST_ACK:
        /* fallthrough */
    case TCP_STATE_TIME_WAIT:
        // TODO: return "error: connection closing"
        break;
    default:
        UNREACHABLE;
    }

    return num;
}

int tcp_sendto(struct tcp_socket *s, const void *buf,
    size_t len, int flags, const struct sockaddr *dest_addr,
    socklen_t dest_len)
{
    if (s->type != TCP_TYPE_CONNECTION)
        return -EINVAL;
    struct tcp_conn *conn = &s->conn;

    int num = -1;
    switch (conn->state) {
    case TCP_STATE_SYN_SENT:
        /* fallthrough */
    case TCP_STATE_SYN_RECEIVED:
        // TODO: block maybe?
        num = tcp_byte_queue_write(&conn->output, buf, len);
        // Unlike the ESTABLISHED state, we buffer bytes
        // but don't send them out yet.
        break;
    case TCP_STATE_ESTABLISHED:
        /* fallthrough */
    case TCP_STATE_CLOSE_WAIT:
        num = tcp_byte_queue_write(&conn->output, buf, len);
        if (num == 0) {
            // TODO: block?
        }
        send_segment_from_conn(conn, TCP_FLAG_ACK);
        break;
    case TCP_STATE_FIN_WAIT_1:
        /* fallthrough */
    case TCP_STATE_FIN_WAIT_2:
        /* fallthrough */
    case TCP_STATE_CLOSING:
        /* fallthrough */
    case TCP_STATE_LAST_ACK:
        /* fallthrough */
    case TCP_STATE_TIME_WAIT:
        num = 0;
        break;
    default:
        UNREACHABLE;
    }

    return num;
}
