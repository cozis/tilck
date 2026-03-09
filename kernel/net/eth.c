#include <tilck/kernel/net.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/common/printk.h>

#include "eth.h"
#include "arp.h"
#include "endian.h"

struct eth_frame {
    struct mac_addr dst;
    struct mac_addr src;
    u16             proto;
} __attribute__((packed));

static void
send_out_packets_with_resolved_addrs(void)
{
    // TODO
}

void eth_process_frame(void *src, size_t len)
{
    if (len < sizeof(struct eth_frame))
        return; /* Not a valid frame. Drop it. */
    struct eth_frame *frame = src;

    void *packet = frame+1;
    int   packet_len = len - sizeof(struct eth_frame);

    bool arp_entry_added = false;
    switch (net_to_cpu_u16(frame->proto)) {

    case ETH_PROTO_ARP:
        arp_entry_added = arp_process_packet(packet, packet_len);
        break;

    case ETH_PROTO_IP:
        // TODO
        break;

    default:
        // Unsupported ethertype
        break;
    }

    if (arp_entry_added) {
        send_out_packets_with_resolved_addrs();
    }
}

static void  *send_buf;
static size_t send_len;

void *eth_send_begin(size_t len)
{
    ASSERT(!send_buf);
    send_buf = kmalloc(sizeof(struct eth_frame) + len);
    return send_buf + sizeof(struct eth_frame);
}

void eth_send_complete(struct mac_addr dstmac, int proto)
{
    ASSERT(send_buf);

    struct eth_frame *frame = send_buf;

    frame->dst = dstmac;
    frame->src = net_driver_funcs.get_mac_addr();
    frame->proto = proto;

    net_driver_funcs.send_frame(send_buf, send_len);

    kfree(send_buf);
    send_buf = NULL;
}
