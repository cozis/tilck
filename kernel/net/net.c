#include <tilck/kernel/net.h>
#include <tilck/common/printk.h>

#include "eth.h"
#include "udp.h"
#include "tcp.h"
#include "endian.h"

struct net_driver_funcs net_driver_funcs;

ip_addr self_ip;

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool parse_ip(char *str, size_t len, ip_addr *dst)
{
    size_t i = 0;

    uint32_t value = 0;

    for (size_t k = 0; k < 4; k++) {
        if (i == len || !is_digit(str[i]))
            return false;
        int n = 0; // Used to represent a byte, but it's larger
                   // to detect overflows.
        do {
            // Convert character to number
            int digit = str[i] - '0';
            if (n > (UINT8_MAX - digit)/10)
                // Adding this digit would make the
                // byte overflow, so it can't be part
                // of the octet.
                break;
            n = n * 10 + digit;
            i++;
        } while (i < len && is_digit(str[i]));

        ASSERT(n >= 0 && n <= UINT8_MAX);
        value = (value << 8) | (uint8_t) n;

        // If this isn't the last octet and there is no
        // dot following it, the address is invalid.
        if (k < 3) {
            if (i == len || str[i] != '.')
                return false;
            i++; // Consume the dot.
        }
    }
    if (i < len)
        // source string contains something
        // other than the address in it.
        return false;

    *dst = cpu_to_net_u32(value);
    return true;
}

void init_net(char *str, size_t len)
{
    if (!parse_ip(str, len, &self_ip))
        panic("NET: Invalid IP address\n");
    init_udp();
    init_tcp();
}

void net_process_packet(void *src, size_t len)
{
    printk("NET: Processing packet\n");
    eth_process_frame(src, len);
}
