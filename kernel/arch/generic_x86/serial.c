
#include <common/basic_defs.h>
#include <exos/serial.h>
#include <exos/hal.h>

#define COM1 0x3f8

void init_serial_port(void)
{
   outb(COM1 + 1, 0x00);    // Disable all interrupts
   outb(COM1 + 3, 0x80);    // Enable DLAB (set baud rate divisor)
   outb(COM1 + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
   outb(COM1 + 1, 0x00);    //                  (hi byte)
   outb(COM1 + 3, 0x03);    // 8 bits, no parity, one stop bit
   outb(COM1 + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
   outb(COM1 + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

int serial_received(void) {
   return inb(COM1 + 5) & 1;
}

char read_serial(void) {
   while (serial_received() == 0);
   return inb(COM1);
}

int is_transmit_empty(void) {
   return inb(COM1 + 5) & 0x20;
}

void write_serial(char a) {
   while (is_transmit_empty() == 0);
   outb(COM1, a);
}
