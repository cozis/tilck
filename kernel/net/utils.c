#include "utils.h"
#include "endian.h"

u16 get_ephimeral_port(u16 *next_port)
{
    u16 port = *next_port; /* TODO: Should ensure no conflicts can happen */
    if (*next_port == EPHIMERAL_PORT_MAX) {
        *next_port = EPHIMERAL_PORT_MIN;
    } else {
        (*next_port)++;
    }
    return port;
}

u16 calculate_checksum(struct checksum_slice *slices, int num_slices)
{
    u32 sum = 0xffff;

    for (int slice_idx = 0; slice_idx < num_slices; slice_idx++) {

        const u16   *ptr = slices[slice_idx].ptr;
        const size_t len = slices[slice_idx].len;

        for (size_t i = 0; i < len/2; i++) {
            sum += net_to_cpu_u16(ptr[i]);
            if (sum > 0xffff)
                sum -= 0xffff;
        }

        if (len & 1) {
            alignas(u16) u8 temp[2];

            temp[0] = ((u8*) slices[slice_idx].ptr)[len-1];
            temp[1] = 0;

            u16 temp2 = *(u16*) temp;
            sum += net_to_cpu_u16(temp2);
            if (sum > 0xffff)
                sum -= 0xffff;
        }
    }

    return cpu_to_net_u16(~sum);
}

u16 calculate_checksum_l4(ip_addr src_addr,
    ip_addr dst_addr, u8 proto, void *data, size_t len)
{
    struct pseudoheader {
        ip_addr src_addr;
        ip_addr dst_addr;
        u8      reserved;
        u8      protocol;
        u16     length;
    }; // Ensure packed?

    struct pseudoheader header = {
        .src_addr = src_addr,
        .dst_addr = dst_addr,
        .reserved = 0,
        .protocol = proto,
        .length   = len,
    };

    struct checksum_slice slices[] = {
        { &header, sizeof(header) },
        { data, len },
    };

    return calculate_checksum(slices, 2);
}