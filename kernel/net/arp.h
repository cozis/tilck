#pragma once

#include <tilck/common/basic_defs.h>

#include "eth.h"

bool arp_process_packet(void *src, size_t len);
void arp_process_time_events(u64 current_time);
bool arp_query_mac_by_ip(ip_addr ip, struct mac_addr *mac);
