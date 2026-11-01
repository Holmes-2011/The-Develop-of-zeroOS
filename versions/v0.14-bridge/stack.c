#include "stack.h"

extern char stack_bottom[], stack_top[];

unsigned int stack_size(void) { return (unsigned int) (stack_top - stack_bottom); }
unsigned int stack_base(void) { return (unsigned int) stack_bottom; }

unsigned int stack_high_water(void) {
    const volatile unsigned int *p = (const volatile unsigned int *) stack_bottom;
    const volatile unsigned int *end = (const volatile unsigned int *) stack_top;
    while (p < end && *p == STACK_PATTERN) p++;
    return (unsigned int) ((char *) stack_top - (char *) p);
}

int stack_overflowed(void) {
    return *(const volatile unsigned int *) stack_bottom != STACK_PATTERN;
}
