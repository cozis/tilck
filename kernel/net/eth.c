#include <tilck/kernel/net.h>
#include <tilck/kernel/kmalloc.h>
#include <tilck/common/printk.h>

#include "ip.h"
#include "eth.h"
#include "arp.h"
#include "endian.h"

struct eth_frame {
    struct mac_addr dst;
    struct mac_addr src;
    u16             proto;
} __attribute__((packed));

STATIC_ASSERT(sizeof(struct eth_frame) == 14);

struct pending_frame {
    ip_addr dstip;
    int     proto;
    void*   frame;
    size_t  frame_len;
};

/*
 * Ethernet frames are limited to 1514 bytes (plus
 * the CRC, which makes it 1518).
 */
#define ETH_FRAME_SIZE 1514

#define MAX_PENDING 32

static int num_pending;
static struct pending_frame pending[MAX_PENDING];

/*
 * State used by the eth_send_begin/complete functions
 */
static void  *send_buf;
static size_t send_len;

static void
send_out_frames_with_resolved_addrs(void);

void eth_process_frame(void *src, size_t len)
{
    printk("ETH: Processing frame (len=%d)\n", len);

    if (len < sizeof(struct eth_frame)) {
        printk("ETH: Frame is too short. Dropping it.\n");
        return; /* Not a valid frame. Drop it. */
    }
    struct eth_frame *frame = src;

    void *packet = frame+1;
    int   packet_len = len - sizeof(struct eth_frame);

    bool arp_entry_added = false;

    u16 ethertype = net_to_cpu_u16(frame->proto);
    switch (ethertype) {

    case ETH_PROTO_ARP:
        printk("ETH: Forwarding frame to the ARP layer.\n");
        arp_entry_added = arp_process_packet(packet, packet_len);
        break;

    case ETH_PROTO_IP:
        printk("ETH: Forwarding frame to the IP layer.\n");
        ip_process_packet(packet, packet_len);
        break;

    default:
        printk("ETH: Frame has an unsupported ethertype %x.\n", ethertype);
        // Unsupported ethertype
        break;
    }

    if (arp_entry_added) {
        printk("ETH: Processing frames with unresolved MAC addresses.\n");
        send_out_frames_with_resolved_addrs();
    }
}

void *eth_send_begin(size_t request_len, size_t *actual_len, bool precise_len)
{
    ASSERT(!send_buf);

    size_t max_len = ETH_FRAME_SIZE - sizeof(struct eth_frame);
    if (request_len > max_len) {
        if (precise_len)
            return NULL;
        request_len = max_len;
    }

    send_buf = kmalloc(sizeof(struct eth_frame) + request_len); /* TODO: There is probably a race condition here */
    if (!send_buf)
        return NULL;
    send_len = sizeof(struct eth_frame) + request_len;

    *actual_len = request_len;
    return send_buf + sizeof(struct eth_frame);
}

static void send_complete(struct mac_addr dstmac, int proto,
    struct eth_frame *frame, size_t frame_len)
{
    frame->dst = dstmac;
    frame->src = net_driver_funcs.get_mac_addr();
    frame->proto = cpu_to_net_u16(proto);

    printk("Outgoing Frame (%d bytes):\n", send_len);
    printk("  dst %x:%x:%x:%x:%x:%x\n",
        frame->dst.data[0], frame->dst.data[1], frame->dst.data[2],
        frame->dst.data[3], frame->dst.data[4], frame->dst.data[5]);
    printk("  src %x:%x:%x:%x:%x:%x\n",
        frame->src.data[0], frame->src.data[1], frame->src.data[2],
        frame->src.data[3], frame->src.data[4], frame->src.data[5]);
    printk("  proto %x\n", net_to_cpu_u16(frame->proto));

    net_driver_funcs.send_frame(send_buf, send_len);

    kfree(frame);
}

void eth_send_complete(struct mac_addr dstmac, int proto)
{
    ASSERT(send_buf);
    send_complete(dstmac, proto, send_buf, send_len);
    send_buf = NULL;
}

void eth_send_complete_ip(ip_addr dstip, int proto)
{
    struct mac_addr dstmac;
    if (arp_query_mac_by_ip(dstip, &dstmac)) {
        eth_send_complete(dstmac, proto);
    } else {
        if (num_pending == MAX_PENDING) {
            kfree(send_buf);
            send_buf = NULL;
            return; // Drop
        }
        pending[num_pending++] = (struct pending_frame) {
            .dstip = dstip,
            .proto = proto,
            .frame = send_buf,
            .frame_len = send_len,
        };
    }
}

static void
send_out_frames_with_resolved_addrs(void)
{
    for (int i = 0; i < num_pending; i++) {
        struct mac_addr dstmac;
        if (arp_query_mac_by_ip(pending[i].dstip, &dstmac)) {
            send_complete(dstmac, pending[i].proto, pending[i].frame, pending[i].frame_len);
            pending[i--] = pending[--num_pending];
        }
    }
}
