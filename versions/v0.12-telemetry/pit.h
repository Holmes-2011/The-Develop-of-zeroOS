#ifndef PIT_H
#define PIT_H
void pit_init(unsigned int frequency);
unsigned int pit_ticks(void);
void pit_tick(void);
#endif
