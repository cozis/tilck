#include <tilck/common/string_util.h>

#include "ip.h"
#include "icmp.h"
#include "endian.h"

#define ICMP_ECHO_REPLY   0
#define ICMP_ECHO_REQUEST 8

struct icmp_message {
    u8  type;
    u8  code;
    u16 checksum;
    u8  data[];
};

struct icmp_message_echo {
    struct icmp_message base;
    u16 id_no;
    u16 seq_no;
    u8  data[];
};

static u16 calculate_checksum_icmp(void *src, size_t len)
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

static void icmp_process_echo_request(void *src, size_t len, ip_addr sender_addr)
{
    if (len < sizeof(struct icmp_message_echo))
        return; /* Invalid ICMP echo request message. Drop it. */
    struct icmp_message_echo *reply;
    struct icmp_message_echo *request = src;

    if (calculate_checksum_icmp(request, len)) {
        return; /* Invalid checksum */
    }

    size_t dummy;
    reply = ip_send_begin(len, &dummy, true);
    if (!reply) {
        return; /* Out of memory. Drop the message. */
    }

    reply->base.type = ICMP_ECHO_REPLY;
    reply->base.code = 0;
    reply->base.checksum = 0;
    reply->id_no = request->id_no;
    reply->seq_no = request->seq_no;
    memcpy(reply->data, request->data, len - sizeof(*reply));

    reply->base.checksum = calculate_checksum_icmp(reply, len);

    ip_send_complete(sender_addr, IP_PROTO_ICMP);
}

void icmp_process_packet(void *src, size_t len, ip_addr sender_addr)
{
    if (len < sizeof(struct icmp_message))
        return; /* Invalid ICMP message. Drop it. */
    struct icmp_message *message = src;

    if (message->type == ICMP_ECHO_REQUEST) {
        icmp_process_echo_request(src, len, sender_addr);
    } else {
        /* Unsupported ICMP message type. Ignore. */
    }
}