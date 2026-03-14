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

static struct list tcp_socks;

static bool is_syn(struct tcp_segment *seg)
{
    return seg->flags & TCP_FLAG_SYN;
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

static int
tcp_byte_queue_init(struct tcp_byte_queue *queue, size_t size)
{
    queue->head = 0;
    queue->used = 0;
    queue->size = size;
    queue->data = kmalloc(size);
    if (!queue->data)
        return -ENOMEM;
    return 0;
}

static void tcp_byte_queue_free(struct tcp_byte_queue *queue)
{
    kfree(queue->data);
}

static void
tcp_byte_queue_remove_head(struct tcp_byte_queue *queue, size_t num)
{
    ASSERT(num <= queue->used);
    queue->head += num;
}

void init_tcp(void)
{
    list_init(&tcp_socks);
}

int tcp_listener_init(struct tcp_listener *listener)
{
    panic("TODO\n"); // TODO
}

void tcp_listener_free(struct tcp_listener *listener)
{
    panic("TODO\n"); // TODO
}

int tcp_listener_read_ready(struct tcp_listener *listener)
{
    panic("TODO"); // TODO
}

int tcp_listener_write_ready(struct tcp_listener *listener)
{
    panic("TODO"); // TODO
}

int tcp_conn_init(struct tcp_conn *conn, size_t input_size, size_t output_size)
{
    conn->input_used = 0;
    conn->input_size = input_size;
    conn->input_data = kmalloc(input_size);
    if (!conn->input_data)
        return -ENOMEM;

    conn->output_used = 0;
    conn->output_size = output_size;
    conn->output_data = kmalloc(output_size);
    if (!conn->output_data) {
        kfree(conn->input_data);
        return -ENOMEM;
    }

    return 0;
}

void tcp_conn_free(struct tcp_conn *conn)
{
    panic("TODO"); // TODO
}

int tcp_conn_read_out(struct tcp_conn *conn, char *dst, int cap)
{
    panic("TODO"); // TODO
}

int tcp_conn_write_in(struct tcp_conn *conn, char *src, int num)
{
    panic("TODO"); // TODO
}

int tcp_conn_read_ready(struct tcp_conn *conn)
{
    panic("TODO"); // TODO
}

int tcp_conn_write_ready(struct tcp_conn *conn)
{
    panic("TODO"); // TODO
}

void tcp_socket_init(struct tcp_socket *s)
{
    list_add_head(&tcp_socks, &s->node);
    s->type = TCP_TYPE_UNCONFIGURED;
}

void tcp_socket_free(struct tcp_socket *s)
{
    list_remove(&s->node);
    if (s->type == TCP_TYPE_CONNECTION)
        tcp_conn_free(&s->conn);
}

int tcp_socket_read_ready(struct tcp_socket *s)
{
    if (s->type == TCP_TYPE_CONNECTION)
        return tcp_conn_read_ready(&s->conn);

    if (s->type == TCP_TYPE_LISTENER)
        return tcp_listener_read_ready(&s->listener);

    ASSERT(s->type == TCP_TYPE_UNCONFIGURED);
    return 0;
}

int tcp_socket_write_ready(struct tcp_socket *s)
{
    if (s->type == TCP_TYPE_CONNECTION)
        return tcp_conn_write_ready(&s->conn);

    if (s->type == TCP_TYPE_LISTENER)
        return tcp_listener_write_ready(&s->listener);

    ASSERT(s->type == TCP_TYPE_UNCONFIGURED);
    return 0;
}

int tcp_socket_except_ready(struct tcp_socket *s)
{
    return 0; /* TODO */
}

int tcp_listen(struct tcp_socket *s, int backlog)
{
    if (s->type != TCP_TYPE_UNCONFIGURED)
        return -EINVAL;
    s->type = TCP_TYPE_LISTENER;

    tcp_listener_init(&s->listener);
    return 0;
}

int tcp_accept(struct tcp_socket *s,
    struct sockaddr *dst_addr, socklen_t *addr_len)
{
    if (s->type != TCP_TYPE_LISTENER)
        return -EINVAL;

    return tcp_listener_accept(&s->listener, dst_addr, addr_len);
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

int tcp_connect(struct tcp_socket *s,
    const struct sockaddr *dst_addr, socklen_t addr_len)
{
    if (s->type != TCP_TYPE_UNCONFIGURED)
        return -EINVAL;

    ip_addr peer_ip;
    u16     dst_port;
    {
        if (addr_len != sizeof(struct sockaddr_in))
            return -EINVAL;
        struct sockaddr_in *tmp = (struct sockaddr_in*) dst_addr;
        if (tmp->sin_family != AF_INET)
            return -EINVAL;
        peer_ip = tmp->sin_addr.s_addr;
        dst_port = net_to_cpu_u16(tmp->sin_port);
    }

    s->type = TCP_TYPE_CONNECTION;
    size_t input_size = 1<<10;
    size_t output_size = 1<<10;
    if (tcp_conn_init(&s->conn, input_size, output_size) < 0) {
        ASSERT(0); // TODO
    }

    size_t dummy;
    struct tcp_segment *seg = ip_send_begin(sizeof(struct tcp_segment), &dummy, true);
    if (!seg) {
        ASSERT(0); // TODO
    }

    u32 iss = choose_iss();
    u32 window = choose_window();

    init_segment(seg, self_ip, peer_ip,
        s->port, dst_port, TCP_FLAG_SYN,
        iss, 0, window);

    ip_send_complete(peer_ip, IP_PROTO_TCP);
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

    case TCP_STATE_CLOSED:
        return -EINVAL;

    case TCP_STATE_LISTEN:
        /* fallthrough */
    case TCP_STATE_SYN_SENT:
        /* fallthrough */
    case TCP_STATE_SYN_RECEIVED:
        // TODO
        break;

    case TCP_STATE_ESTABLISHED:
        /* fallthrough */
    case TCP_STATE_FIN_WAIT_1:
        /* fallthrough */
    case TCP_STATE_FIN_WAIT_2:
        num = tcp_conn_read_out(conn, buf, len);
        // TODO
        break;

    case TCP_STATE_CLOSE_WAIT:
        // TODO
        break;

    case TCP_STATE_CLOSING:
        /* fallthrough */
    case TCP_STATE_LAST_ACK:
        /* fallthrough */
    case TCP_STATE_TIME_WAIT:
        // TODO
        break;

    default:
        ASSERT(0);
        break;
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

    case TCP_STATE_CLOSED:
        return -EINVAL;

    case TCP_STATE_LISTEN:
        // TODO
        break;

    case TCP_STATE_SYN_SENT:
        /* fallthrough */
    case TCP_STATE_SYN_RECEIVED:
        // TODO: queue data for transmission after entering ESTABLISHED state
        //       if no space to queue, respond with "error: insufficient resources"
        break;

    case TCP_STATE_ESTABLISHED:
        /* fallthrough */
    case TCP_STATE_CLOSE_WAIT:
        num = tcp_byte_queue_write(&conn->output, buf, len);
        // TODO: send out
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
        // TODO
        break;

    default:
        ASSERT(0);
        break;
    }

    return num;
}

static struct tcp_socket *find_tcp_socket_by_port(u16 port)
{
    struct tcp_socket *s;
    list_for_each_ro(s, &tcp_socks, node) {
        if (s->port == port) {
            return s;
        }
    }
    return NULL;
}

static void send_segment(ip_addr src_addr, ip_addr dst_addr,
    u16 src_port, u16 dst_port, int flags, u32 seq, u32 ack)
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
    send_segment(conn->addr, conn->peer_addr,
                 conn->port, conn->peer_port,
                 flags, conn->iss, conn->rcv_nxt); /* TODO: should not be conn->iss here */
}

static void state_closed(struct tcp_segment *seg)
{
    if (!is_rst(seg)) {
        send_segment(local_addr, sender_addr,
                     seg->dst_port, seg->src_port,
                     TCP_FLAG_RST, seg->ack_no,
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

    struct tcp_conn *conn = tcp_listener_add_conn(listener);
    if (!conn)
        return; /* Error. Drop segment. */

    conn->peer_addr = peer_addr;
    conn->peer_port = seg->src_port;

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
        // TODO: Signal to the user "error: connection reset"
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
            // TODO: signal "connection refused" to client
            conn->state = TCP_STATE_CLOSED;
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
    // TODO: signal to the user "connection closing"

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

    struct tcp_conn *conn = find_conn(local_addr, sender_addr, seg->dst_port, seg->src_port);
    if (conn) {
        state_other(&s->conn, seg);
        if (conn->state == TCP_STATE_CLOSED) {
            // TODO: remove the struct
        }
    } else {
        struct tcp_listener *listener = find_listener(local_addr, seg->dst_port);
        if (listener) {
            state_listen(listener, seg);
        } else {
            state_closed(seg);
        }
    }
}
