
#pragma once

#include <tilck/common/basic_defs.h>
#include <tilck/kernel/net.h>

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP  6
#define IP_PROTO_UDP  17

/*
 * Same semantics as eth_send_begin
 */
void *ip_send_begin(size_t request_len, size_t *actual_len, bool precise_len);

/*
 * Same semantics as eth_send_complete
 */
void ip_send_complete(ip_addr dst, int proto);

void ip_process_packet(void *src, size_t len);
