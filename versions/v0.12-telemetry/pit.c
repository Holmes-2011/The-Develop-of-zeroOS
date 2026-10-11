#include "pit.h"
#include "io.h"
#define PIT_CHANNEL0 0x40
#define PIT_COMMAND 0x43
#define PIT_BASE_HZ 1193182U
static volatile unsigned int ticks;
void pit_init(unsigned int frequency) { unsigned int divisor; if (frequency == 0) frequency = 100; divisor = PIT_BASE_HZ / frequency; if (divisor == 0) divisor = 1; if (divisor > 0xffff) divisor = 0xffff; outb(PIT_COMMAND, 0x36); outb(PIT_CHANNEL0, (unsigned char)(divisor & 0xff)); outb(PIT_CHANNEL0, (unsigned char)(divisor >> 8)); ticks = 0; }
unsigned int pit_ticks(void) { return ticks; }
void pit_tick(void) { ++ticks; }
