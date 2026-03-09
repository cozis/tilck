
#pragma once

#include <tilck/kernel/net.h>

#define ETH_PROTO_ARP 0x0806
#define ETH_PROTO_IP  0x0800

void *eth_send_begin(size_t len);

/*
 * proto must be one of ETH_PROTO_ARP or ETH_PROTO_IP
 */
void eth_send_complete(struct mac_addr dstmac, int proto);

void eth_process_frame(void *src, size_t len);