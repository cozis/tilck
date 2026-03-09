
#pragma once

#include <tilck/common/basic_defs.h>

struct mac_addr {
    u8 data[6];
};

typedef u32 ip_addr;

struct net_driver_funcs {
    struct mac_addr (*get_mac_addr)(void);
    int (*send_frame)(char *src, int len);
};

/*
 * Filled out by the driver
 */
extern struct net_driver_funcs net_driver_funcs;

/*
 * IP address associated to the stack
 */
extern ip_addr self_ip;

void init_net(char *str, size_t len);

/*
 * Called by the driver
 */
void net_process_packet(void *src, size_t len);