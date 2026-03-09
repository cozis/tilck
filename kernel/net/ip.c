#include <tilck/common/printk.h>

#include "ip.h"
#include "udp.h"
#include "eth.h"
#include "icmp.h"
#include "endian.h"

struct ip_packet {
    u8   header_length_or_version1: 4; // Header length when little endian
    u8   header_length_or_version2: 4; // Header length when big endian
    u8   type_of_service;
    u16  total_length;
    u16  id;
    u16  fragment_offset;
    u8   time_to_live;
    u8   protocol;
    u16  checksum;
    u32  src_ip;
    u32  dst_ip;
    char payload[];
};

static u16    next_packet_id;
static void  *send_ptr; /* TODO: Race condition probably */
static size_t send_len;

static u16 calculate_checksum_ip(void *src, size_t len)
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

static bool is_packet_one_of_more_fragments(struct ip_packet *packet)
{
    size_t offset = net_to_cpu_u16(packet->fragment_offset) & 0x1FFF;
    bool more_fragments = net_to_cpu_u16(packet->fragment_offset) & 0x2000;
    return more_fragments || offset;
}

void ip_process_packet(void *src, size_t len)
{
    printk("IP: Processing packet\n");

    if (len < sizeof(struct ip_packet))
        return; /* Not a valid IP packet. Drop it. */
    struct ip_packet *packet = src; /* TODO: Alignment? */

    int version;
    int header_length;

    if (cpu_is_little_endian()) {
        header_length = packet->header_length_or_version1;
        version       = packet->header_length_or_version2;
    } else {
        version       = packet->header_length_or_version1;
        header_length = packet->header_length_or_version2;
    }

    if (version != 4 || header_length < 5) {
        return; /* Only IPv4 packets with no options are supported */
    }

    size_t option_count = header_length - sizeof(struct ip_packet)/4;
    if (option_count > 0) {
        // TODO: Handle IP options
        return;
    }

    if (is_packet_one_of_more_fragments(packet)) {
        return; /* Don't support IP fragmentation */
    }

    if (calculate_checksum_ip((u16*) packet, 4 * header_length)) {
        return; /* Invalid checksum */
    }

    if (packet->dst_ip != self_ip) {
        return; /* Packet wasn't meant for us */
    }

    void  *payload = packet+1;
    size_t payload_len = net_to_cpu_u16(packet->total_length) - sizeof(struct ip_packet);

    switch (packet->protocol) {
    case IP_PROTO_ICMP:
        icmp_process_packet(payload, payload_len, packet->src_ip);
        break;
    case IP_PROTO_UDP:
        udp_process_datagram(payload, payload_len, packet->src_ip);
        break;
    case IP_PROTO_TCP:
        /* Not implemented */
        break;
    default:
        /* Unsupported protocol */
        break;
    }
}

void *ip_send_begin(size_t request_len, size_t *actual_len, bool precise_len)
{
    request_len += sizeof(struct ip_packet);

    char *ptr = eth_send_begin(request_len, actual_len, precise_len);
    if (!ptr)
        return NULL;

    *actual_len -= sizeof(struct ip_packet); /* TODO: underflow? */
    return ptr + sizeof(struct ip_packet);
}

void ip_send_complete(ip_addr dst, int proto)
{
    struct ip_packet *packet = send_ptr;
    int version = 4;
    int header_length = 5;
    if (cpu_is_little_endian()) {
        packet->header_length_or_version1 = header_length;
        packet->header_length_or_version2 = version;
    } else {
        packet->header_length_or_version1 = version;
        packet->header_length_or_version2 = header_length;
    }
    packet->type_of_service = 0; // TODO
    packet->total_length = cpu_to_net_u16(send_len);
    packet->id = next_packet_id++;
    packet->fragment_offset = 0; // TODO
    packet->time_to_live = 32; // TODO
    packet->protocol = proto;
    packet->checksum = 0; /* Temporary value */
    packet->src_ip = self_ip;
    packet->dst_ip = dst;
    eth_send_complete_ip(dst, ETH_PROTO_IP);
}
