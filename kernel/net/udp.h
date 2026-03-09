
#pragma once

#include <tilck/common/basic_defs.h>

#include <tilck/kernel/net.h>

void udp_process_datagram(void *src, size_t len, ip_addr sender_addr);

void *udp_send_begin(size_t len);
void udp_send_complete(ip_addr ip, u16 src_port, u16 dst_port);
