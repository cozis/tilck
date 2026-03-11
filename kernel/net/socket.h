
#pragma once

#include "udp.h"

void init_socket(void);
void dispatch_datagram(ip_addr sender_addr, struct udp_datagram *datagram);