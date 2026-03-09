#include <tilck/common/printk.h>
#include <tilck/kernel/timer.h>
#include <tilck/kernel/net.h>

#include "arp.h"
#include "endian.h"

#define ARP_TABLE_CAP 32
#define ARP_ENTRY_EXPIRY_TICKS 1000

#define ARP_HARDWARE_ETH 1
#define ARP_PROTOCOL_IP  0x800

#define ARP_OPER_REQUEST 1
#define ARP_OPER_REPLY   2

struct arp_message {
    u16             hware_type;
    u16             proto_type;
    u8              hware_len;
    u8              proto_len;
    u16             oper_type;
    struct mac_addr sender_hware_addr;
    ip_addr         sender_proto_addr;
    struct mac_addr target_hware_addr;
    ip_addr         target_proto_addr;
} __attribute__((__packed__));

struct arp_entry {
    u64             deadline; /* Absolute deadline in ticks */
    struct mac_addr mac;
    ip_addr         ip;
};

static int arp_table_size = 0;
static struct arp_entry arp_table[ARP_TABLE_CAP];

/*
 * Remove expired ARP entries
 */
void arp_process_time_events(u64 current_time)
{
    for (int i = 0; i < arp_table_size; i++) {
        if (arp_table[i].deadline < current_time) {
            arp_table[i--] = arp_table[--arp_table_size];
        }
    }
}

static int query_entry_by_ip(ip_addr ip)
{
    for (int i = 0; i < arp_table_size; i++) {
        if (arp_table[i].ip == ip)
            return i;
    }
    return -1;
}

bool arp_query_mac_by_ip(ip_addr ip, struct mac_addr *mac)
{
    int idx = query_entry_by_ip(ip);
    if (idx < 0)
        return false;

    *mac = arp_table[idx].mac;
    return true;
}

static bool update_entry(struct mac_addr mac, ip_addr ip, u64 deadline)
{
    int idx = query_entry_by_ip(ip);
    if (idx < 0)
        return false;

    arp_table[idx].mac = mac;
    arp_table[idx].deadline = deadline;
    return true;
}

static bool create_or_update_entry(struct mac_addr mac, ip_addr ip, u64 deadline)
{
    int idx = query_entry_by_ip(ip);
    if (idx < 0) {
        if (arp_table_size == ARP_TABLE_CAP)
            return false;
        idx = arp_table_size++;
    }

    arp_table[idx].ip = ip;
    arp_table[idx].mac = mac;
    arp_table[idx].deadline = deadline;
    return true;
}

/*
 * Returns true if an ARP entry was added or updated
 */
bool arp_process_packet(void *src, size_t len)
{
    printk("ARP: Processing packet\n");

    if (len != sizeof(struct arp_message))
        return false; // Ignore

    struct arp_message *msg = src;

    if (msg->hware_type != cpu_to_net_u16(ARP_HARDWARE_ETH)) {
        /* Level 2 protocol not supported */
        printk("ARP: Hardware type %d not supported", msg->hware_type);
        return false; // Ignore
    }

    if (msg->proto_type != cpu_to_net_u16(ARP_PROTOCOL_IP)) {
        /* Level 3 protocol not supported */
        printk("ARP: Protocol type %d not supported", msg->proto_type);
        return false; // Ignore
    }

    if (msg->hware_len != 6 || msg->proto_len != 4) {
         /* Invalid fields */
        printk("ARP: Invalid hardware or protocol address size %d or %d (expected %d and %d)",
            msg->hware_len, msg->proto_len, 6, 4);
        return false; // Ignore
    }

    u64 deadline = get_ticks() + ARP_ENTRY_EXPIRY_TICKS; /* TODO: overflow? */
    bool merge = update_entry(msg->sender_hware_addr,
                              msg->sender_proto_addr,
                              deadline);

    bool added = false;
    if (msg->target_proto_addr == self_ip) {

        if (!merge) {
            added = create_or_update_entry(msg->sender_hware_addr,
                                           msg->sender_proto_addr,
                                           deadline);
        }

        if (msg->oper_type == cpu_to_net_u16(ARP_OPER_REQUEST)) {

            // Generate the ARP REPLY

            struct arp_message *response = eth_send_begin(sizeof(struct arp_message));
            if (response == NULL) {
                ASSERT(0); // TODO
            }
            *response = (struct arp_message) {
                .hware_type = msg->hware_type,
                .proto_type = msg->proto_type,
                .hware_len  = msg->hware_len,
                .proto_len  = msg->proto_len,
                .oper_type = cpu_to_net_u16(ARP_OPER_REPLY),
                .sender_hware_addr = net_driver_funcs.get_mac_addr(),
                .sender_proto_addr = self_ip,
                .target_hware_addr = msg->sender_hware_addr,
                .target_proto_addr = msg->sender_proto_addr,
            };
            eth_send_complete(msg->sender_hware_addr, ETH_PROTO_ARP);
        }
    } else {
        // Request not for us
    }

    return added;
}
