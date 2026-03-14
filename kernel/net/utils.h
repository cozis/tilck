
#pragma once

#include <tilck/common/basic_defs.h>

#include "ip.h"

struct checksum_slice {
    void  *ptr;
    size_t len;
};

u16 calculate_checksum(struct checksum_slice *slices, int num_slices);

u16 calculate_checksum_l4(ip_addr src_addr,
    ip_addr dst_addr, u8 proto, void *data, size_t len);