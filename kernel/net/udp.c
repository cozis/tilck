#include <tilck/common/printk.h>
#include <tilck/kernel/hal.h>

#include "ip.h"
#include "udp.h"
#include "endian.h"
#include "socket.h"

static void*  send_ptr; /* TODO: Race condition probably */
static size_t send_len;

struct checksum_slice {
    void  *ptr;
    size_t len;
};

static u16
calculate_checksum_int(struct checksum_slice *slices, int num_slices)
{
    u32 sum = 0xffff;

    for (int slice_idx = 0; slice_idx < num_slices; slice_idx++) {

        const u16   *ptr = slices[slice_idx].ptr;
        const size_t len = slices[slice_idx].len;

        for (size_t i = 0; i < len/2; i++) {
            sum += net_to_cpu_u16(ptr[i]);
            if (sum > 0xffff)
                sum -= 0xffff;
        }

        if (len & 1) {
            alignas(u16) u8 temp[2];

            temp[0] = ((u8*) slices[slice_idx].ptr)[len-1];
            temp[1] = 0;

            u16 temp2 = *(u16*) temp;
            sum += net_to_cpu_u16(temp2);
            if (sum > 0xffff)
                sum -= 0xffff;
        }
    }

    return cpu_to_net_u16(~sum);
}

static u16 calculate_checksum_udp(ip_addr src_addr,
    ip_addr dst_addr, struct udp_datagram *datagram)
{
    struct pseudoheader {
        ip_addr src_addr;
        ip_addr dst_addr;
        u8      reserved;
        u8      protocol;
        u16     length;
    }; // Ensure packed?

    struct pseudoheader header = {
        .src_addr = src_addr,
        .dst_addr = dst_addr,
        .reserved = 0,
        .protocol = 17, /* UDP */
        .length   = datagram->length,
    };

    struct checksum_slice slices[] = {
        { &header, sizeof(header) },
        { datagram, net_to_cpu_u16(datagram->length) },
    };

    return calculate_checksum_int(slices, 2);
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

    if (calculate_checksum_udp(sender_addr, self_ip, datagram) && !in_hypervisor()) {
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
    dgram->checksum = calculate_checksum_udp(self_ip, ip, dgram);
    ip_send_complete(ip, IP_PROTO_UDP);
    send_ptr = NULL;
    send_len = 0;
}
