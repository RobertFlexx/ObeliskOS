/*
 * Obelisk OS - Basic Time Functions
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <arch/cpu.h>

uint64_t get_ticks(void) {
    /* Approximate milliseconds using TSC on early boot systems. */
    return rdtsc() / 3000000ULL;
}

uint64_t get_time_ns(void) {
    /* Approximate nanoseconds from a nominal 3GHz clock. */
    return (rdtsc() * 1000ULL) / 3ULL;
}

void delay_ms(uint64_t ms) {
    uint64_t start = get_ticks();
    while ((get_ticks() - start) < ms) {
        pause();
    }
}
