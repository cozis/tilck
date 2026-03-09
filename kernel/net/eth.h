
#pragma once

#include <tilck/kernel/net.h>

#define ETH_PROTO_ARP 0x0806
#define ETH_PROTO_IP  0x0800

/*
 * Begins the transmission of an ethernet frame.
 *
 * Callers can request a payload length for the frame via the
 * "request_len" argument. The function may decide to lower it.
 * The chosen payload length for the frame is then returned via
 * the "actual_len" argument.
 *
 * If "precise_len" is true, the function fails if the length
 * is too large.
 *
 * To complete the write, callers must fill out the returned
 * region and call eth_send_complete.
 */
void *eth_send_begin(size_t request_len, size_t *actual_len, bool precise_len);

/*
 * proto must be one of ETH_PROTO_ARP or ETH_PROTO_IP
 */
void eth_send_complete(struct mac_addr dstmac, int proto);

void eth_send_complete_ip(ip_addr dstip, int proto);


void eth_process_frame(void *src, size_t len);