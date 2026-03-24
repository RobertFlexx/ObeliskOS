/*
 * Obelisk OS - I/O Port Access
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <arch/cpu.h>

/* 8-bit I/O */
void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* 16-bit I/O */
void outw(uint16_t port, uint16_t value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* 32-bit I/O */
void outl(uint16_t port, uint32_t value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* String I/O operations */
void outsb(uint16_t port, const void *addr, size_t count) {
    __asm__ volatile("rep outsb"
                     : "+S"(addr), "+c"(count)
                     : "d"(port)
                     : "memory");
}

void insb(uint16_t port, void *addr, size_t count) {
    __asm__ volatile("rep insb"
                     : "+D"(addr), "+c"(count)
                     : "d"(port)
                     : "memory");
}

void outsw(uint16_t port, const void *addr, size_t count) {
    __asm__ volatile("rep outsw"
                     : "+S"(addr), "+c"(count)
                     : "d"(port)
                     : "memory");
}

void insw(uint16_t port, void *addr, size_t count) {
    __asm__ volatile("rep insw"
                     : "+D"(addr), "+c"(count)
                     : "d"(port)
                     : "memory");
}

void outsl(uint16_t port, const void *addr, size_t count) {
    __asm__ volatile("rep outsl"
                     : "+S"(addr), "+c"(count)
                     : "d"(port)
                     : "memory");
}

void insl(uint16_t port, void *addr, size_t count) {
    __asm__ volatile("rep insl"
                     : "+D"(addr), "+c"(count)
                     : "d"(port)
                     : "memory");
}

/* I/O delay (for slow devices) */
void io_wait(void) {
    /* Write to unused port 0x80 to create a small delay */
    outb(0x80, 0);
}