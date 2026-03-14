#include <tilck/common/printk.h>
#include <tilck/kernel/hal.h>

#include "ip.h"
#include "udp.h"
#include "endian.h"
#include "socket.h"

static void*  send_ptr; /* TODO: Race condition probably */
static size_t send_len;

static u16 calculate_checksum_udp(void *src, size_t len)
{
    ASSERT((len & 1) == 0);

    const u16 *src2 = src;

    u32 sum = 0xffff;
    for (size_t i = 0; i < len/2; i++) {
        sum += net_to_cpu_u16(src2[i]);
        if (sum > 0xffff)
            sum -= 0xffff;
    }

    return cpu_to_net_u16(~sum);
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

    if (calculate_checksum_udp(datagram, net_to_cpu_u16(datagram->length)) && !in_hypervisor()) {
        printk("UDP: Datagram checksum invalid. Dropping it.\n");
        return;
    }

    printk("UDP: Dispatching datagram to socket\n");
    dispatch_datagram(sender_addr, datagram);
}

void *udp_send_begin(size_t len)
{
    size_t dummy;
    send_ptr = ip_send_begin(sizeof(struct udp_datagram) + len, &dummy, true);
    if (send_ptr == NULL)
        return NULL;

    send_len = sizeof(struct udp_datagram) + len;
    return (char*) send_ptr + sizeof(struct udp_datagram);
}

void udp_send_complete(ip_addr ip, u16 src_port, u16 dst_port)
{
    ASSERT(send_ptr);
    struct udp_datagram *dgram = send_ptr;
    dgram->src_port = cpu_to_net_u16(src_port);
    dgram->dst_port = cpu_to_net_u16(dst_port);
    dgram->length   = cpu_to_net_u16(send_len);
    dgram->checksum = 0;
    dgram->checksum = calculate_checksum_udp(send_ptr, send_len);
    ip_send_complete(ip, IP_PROTO_UDP);
    send_ptr = NULL;
    send_len = 0;
}
