#include <tilck/common/printk.h>

#include <tilck/kernel/hal.h>
#include <tilck/kernel/user.h>
#include <tilck/kernel/errno.h>
#include <tilck/kernel/kmalloc.h>

#include "ip.h"
#include "udp.h"
#include "utils.h"
#include "endian.h"

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

static void*  send_ptr; /* TODO: Race condition probably */
static size_t send_len;

static struct list udp_socks;

void init_udp(void)
{
    list_init(&udp_socks);
}

void udp_create(struct udp_socket *s)
{
    list_init(&s->messages);
    s->num_messages = 0;
    kmutex_init(&s->lock, 0);
    kcond_init(&s->message_available);
    list_add_head(&udp_socks, &s->node);
}

void udp_free(struct udp_socket *s)
{
    kmutex_destroy(&s->lock);
    kcond_destroy(&s->message_available);
    list_remove(&s->node);
}

int udp_socket_read_ready(struct udp_socket *s)
{
    return !list_is_empty(&s->messages);
}

int udp_socket_write_ready(struct udp_socket *s)
{
    return 1;
}

int udp_socket_except_ready(struct udp_socket *s)
{
    return 1;
}

int udp_bind(struct udp_socket *s, const struct sockaddr *addr,
    socklen_t addrlen)
{
    // TODO
}

int udp_listen(struct udp_socket *s, int backlog)
{
    return -EINVAL;
}

int udp_accept(struct udp_socket *s,
    struct sockaddr *dst_addr, socklen_t *addr_len)
{
    return -EINVAL;
}

int udp_connect(struct udp_socket *s,
    const struct sockaddr *dst_addr, socklen_t addr_len)
{
    return -EINVAL;
}

int udp_recvfrom(struct udp_socket *s, void *buf, size_t len,
    int flags, struct sockaddr *src_addr, socklen_t *addrlen)
{
    struct message *m;

    if (!s->is_bound) {
        ASSERT(0); // TODO: Block forever
    }

    printk("SOCKET: Retrieving datagram from socket\n");
    kmutex_lock(&s->lock);
    while (list_is_empty(&s->messages) && s->block) {
        printk("SOCKET: Waiting\n");
        kcond_wait(&s->message_available, &s->lock, KCOND_WAIT_FOREVER);
        printk("SOCKET: Woke up\n");
    }
    printk("SOCKET: Retrieved\n");
    if (list_is_empty(&s->messages)) {
        m = NULL;
    } else {
        m = list_first_obj(&s->messages, struct message, node);
        list_remove(&m->node);
        s->num_messages--;
    }
    kmutex_unlock(&s->lock);
    if (!m) return -EAGAIN;

    size_t num = len;
    num = MIN(num, m->size);
    num = MIN(num, (size_t) INT_MAX);
    if (copy_to_user(buf, m->data, num) < 0) {
        //kfree(m); TODO: uncomment
        return -EFAULT;
    }

    if (*addrlen != sizeof(struct sockaddr_in)) {
        //kfree(m); TODO: uncomment
        return -EINVAL;
    }

    struct sockaddr_in addr_buf;
    addr_buf.sin_family      = AF_INET;
    addr_buf.sin_port        = cpu_to_net_u16(m->sender_port);
    addr_buf.sin_addr.s_addr = m->sender_addr;
    if (copy_to_user(src_addr, &addr_buf, sizeof(addr_buf)) < 0) {
        //kfree(m); TODO: uncomment
        return -EFAULT;
    }

    socklen_t addr_len = sizeof(addr_buf);
    if (copy_to_user(addrlen, &addr_len, sizeof(addr_len)) < 0) {
        //kfree(m); TODO: uncomment
        return -EFAULT;
    }

    //kfree(m); TODO: uncomment
    return num;
}

static void *udp_send_begin(size_t len)
{
    size_t dummy;
    send_ptr = ip_send_begin(sizeof(struct udp_datagram) + len, &dummy, true);
    if (send_ptr == NULL)
        return NULL;

    send_len = sizeof(struct udp_datagram) + len;
    return (char*) send_ptr + sizeof(struct udp_datagram);
}

static void udp_send_complete(ip_addr ip, u16 src_port, u16 dst_port)
{
    ASSERT(send_ptr);
    struct udp_datagram *dgram = send_ptr;

    dgram->src_port = cpu_to_net_u16(src_port);
    dgram->dst_port = cpu_to_net_u16(dst_port);
    dgram->length   = cpu_to_net_u16(send_len);
    dgram->checksum = 0;
    dgram->checksum = calculate_checksum_l4(self_ip, ip, IP_PROTO_UDP, dgram, send_len);

    ip_send_complete(ip, IP_PROTO_UDP);
    send_ptr = NULL;
    send_len = 0;
}

int udp_sendto(struct udp_socket *s, const void *buf,
    size_t len, int flags, const struct sockaddr *dest_addr,
    socklen_t dest_len)
{
    ip_addr dst_ip;
    u16     dst_port;
    {
        if (dest_len != sizeof(struct sockaddr_in))
            return -EINVAL; // TODO: Proper error code?

        struct sockaddr_in tmp;
        if (copy_from_user(&tmp, dest_addr, dest_len) < 0)
            return -EFAULT;

        if (tmp.sin_family != AF_INET)
            return -EINVAL;

        dst_ip   = tmp.sin_addr.s_addr;
        dst_port = net_to_cpu_u16(tmp.sin_port);
    }

    void *dst = udp_send_begin(len);
    if (!dst)
        return -EMSGSIZE;

    if (copy_from_user(dst, buf, len) < 0)
        return -EFAULT;

    udp_send_complete(dst_ip, s->port, dst_port);
    return len; /* TODO: What if len>INT_MAX ? */
}

static void dispatch_datagram(ip_addr sender_addr, struct udp_datagram *datagram)
{
    bool found = false;
    struct udp_socket *s;
    list_for_each_ro(s, &udp_socks, node) {
        if (s->port == net_to_cpu_u16(datagram->dst_port)) {

            struct message *m = kmalloc(sizeof(struct message) + net_to_cpu_u16(datagram->length) - sizeof(datagram));
            if (!m) {
                printk("SOCKET: Couldn't allocate message buffer\n");
                return;
            }

            m->sender_addr = sender_addr;
            m->sender_port = net_to_cpu_u16(datagram->src_port);
            m->size = net_to_cpu_u16(datagram->length);
            memcpy(m->data, datagram+1, net_to_cpu_u16(datagram->length));

            printk("SOCKET: Storing datagram into socket\n");
            kmutex_lock(&s->lock);
            list_add_tail(&s->messages, &m->node);
            s->num_messages++;
            kcond_signal_one(&s->message_available);
            kmutex_unlock(&s->lock);

            found = true;
            break;
        }
    }

    if (!found) {
        printk("SOCKET: No socket found for datagram\n");
    }
}

void udp_process_datagram(void *src, size_t len, ip_addr sender_addr)
{
    if (len < sizeof(struct udp_datagram)) {
        printk("UDP: Datagram length too small (%d). Dropping it.\n", len);
        return;
    }
    struct udp_datagram *datagram = src;

    if (len < net_to_cpu_u16(datagram->length)) {
        printk("UDP: Datagram length field invalid (got %d, expected %d)\n", len, net_to_cpu_u16(datagram->length));
        return;
    }

    if (calculate_checksum_l4(sender_addr, self_ip, IP_PROTO_UDP, datagram, net_to_cpu_u16(datagram->length)) && !in_hypervisor()) {
        printk("UDP: Datagram checksum invalid. Dropping it.\n");
        return;
    }

    printk("UDP: Dispatching datagram to socket\n");
    dispatch_datagram(sender_addr, datagram);
}
