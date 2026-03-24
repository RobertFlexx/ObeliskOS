/*
 * Obelisk OS - Bitmap Utilities
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>

#define BITS_PER_LONG   64
#define BIT_WORD(nr)    ((nr) / BITS_PER_LONG)
#define BIT_MASK(nr)    (1UL << ((nr) % BITS_PER_LONG))

/*
 * set_bit - Set a bit in a bitmap
 */
void set_bit(unsigned long nr, volatile unsigned long *addr) {
    addr[BIT_WORD(nr)] |= BIT_MASK(nr);
}

/*
 * clear_bit - Clear a bit in a bitmap
 */
void clear_bit(unsigned long nr, volatile unsigned long *addr) {
    addr[BIT_WORD(nr)] &= ~BIT_MASK(nr);
}

/*
 * test_bit - Test if a bit is set
 */
bool test_bit(unsigned long nr, const volatile unsigned long *addr) {
    return (addr[BIT_WORD(nr)] & BIT_MASK(nr)) != 0;
}

/*
 * test_and_set_bit - Test and set a bit
 */
bool test_and_set_bit(unsigned long nr, volatile unsigned long *addr) {
    unsigned long mask = BIT_MASK(nr);
    unsigned long *p = (unsigned long *)addr + BIT_WORD(nr);
    unsigned long old = *p;
    
    *p = old | mask;
    return (old & mask) != 0;
}

/*
 * test_and_clear_bit - Test and clear a bit
 */
bool test_and_clear_bit(unsigned long nr, volatile unsigned long *addr) {
    unsigned long mask = BIT_MASK(nr);
    unsigned long *p = (unsigned long *)addr + BIT_WORD(nr);
    unsigned long old = *p;
    
    *p = old & ~mask;
    return (old & mask) != 0;
}

/*
 * change_bit - Toggle a bit
 */
void change_bit(unsigned long nr, volatile unsigned long *addr) {
    addr[BIT_WORD(nr)] ^= BIT_MASK(nr);
}

/*
 * ffs - Find first set bit
 * Returns bit position + 1, or 0 if no bits set
 */
int ffs(unsigned long x) {
    if (!x) return 0;
    return __builtin_ffsl(x);
}

/*
 * fls - Find last set bit
 * Returns bit position + 1, or 0 if no bits set
 */
int fls(unsigned long x) {
    if (!x) return 0;
    return BITS_PER_LONG - __builtin_clzl(x);
}

/*
 * ffz - Find first zero bit
 */
unsigned long ffz(unsigned long x) {
    return ffs(~x) - 1;
}

/*
 * find_first_bit - Find first set bit in memory
 */
unsigned long find_first_bit(const unsigned long *addr, unsigned long size) {
    unsigned long idx;
    
    for (idx = 0; idx * BITS_PER_LONG < size; idx++) {
        if (addr[idx]) {
            return MIN(idx * BITS_PER_LONG + __builtin_ffsl(addr[idx]) - 1, size);
        }
    }
    
    return size;
}

/*
 * find_first_zero_bit - Find first zero bit in memory
 */
unsigned long find_first_zero_bit(const unsigned long *addr, unsigned long size) {
    unsigned long idx;
    
    for (idx = 0; idx * BITS_PER_LONG < size; idx++) {
        if (~addr[idx]) {
            unsigned long bit = idx * BITS_PER_LONG + ffz(addr[idx]);
            return MIN(bit, size);
        }
    }
    
    return size;
}

/*
 * find_next_bit - Find next set bit after offset
 */
unsigned long find_next_bit(const unsigned long *addr, unsigned long size,
                            unsigned long offset) {
    unsigned long tmp;
    
    if (offset >= size) return size;
    
    tmp = addr[BIT_WORD(offset)];
    tmp &= ~0UL << (offset % BITS_PER_LONG);
    
    if (tmp) {
        return MIN(BIT_WORD(offset) * BITS_PER_LONG + __builtin_ffsl(tmp) - 1, size);
    }
    
    offset = (BIT_WORD(offset) + 1) * BITS_PER_LONG;
    
    while (offset < size) {
        tmp = addr[BIT_WORD(offset)];
        if (tmp) {
            return MIN(offset + __builtin_ffsl(tmp) - 1, size);
        }
        offset += BITS_PER_LONG;
    }
    
    return size;
}

/*
 * find_next_zero_bit - Find next zero bit after offset
 */
unsigned long find_next_zero_bit(const unsigned long *addr, unsigned long size,
                                 unsigned long offset) {
    unsigned long tmp;
    
    if (offset >= size) return size;
    
    tmp = ~addr[BIT_WORD(offset)];
    tmp &= ~0UL << (offset % BITS_PER_LONG);
    
    if (tmp) {
        return MIN(BIT_WORD(offset) * BITS_PER_LONG + __builtin_ffsl(tmp) - 1, size);
    }
    
    offset = (BIT_WORD(offset) + 1) * BITS_PER_LONG;
    
    while (offset < size) {
        tmp = ~addr[BIT_WORD(offset)];
        if (tmp) {
            return MIN(offset + __builtin_ffsl(tmp) - 1, size);
        }
        offset += BITS_PER_LONG;
    }
    
    return size;
}

/*
 * bitmap_set - Set a range of bits
 */
void bitmap_set(unsigned long *map, unsigned int start, unsigned int nbits) {
    unsigned long *p = map + BIT_WORD(start);
    int bits_to_set = BITS_PER_LONG - (start % BITS_PER_LONG);
    unsigned long mask_to_set = ~0UL << (start % BITS_PER_LONG);
    
    while (nbits >= (unsigned int)bits_to_set) {
        *p |= mask_to_set;
        nbits -= bits_to_set;
        bits_to_set = BITS_PER_LONG;
        mask_to_set = ~0UL;
        p++;
    }
    
    if (nbits) {
        mask_to_set &= ~(~0UL << nbits);
        *p |= mask_to_set;
    }
}

/*
 * bitmap_clear - Clear a range of bits
 */
void bitmap_clear(unsigned long *map, unsigned int start, unsigned int nbits) {
    unsigned long *p = map + BIT_WORD(start);
    int bits_to_clear = BITS_PER_LONG - (start % BITS_PER_LONG);
    unsigned long mask_to_clear = ~0UL << (start % BITS_PER_LONG);
    
    while (nbits >= (unsigned int)bits_to_clear) {
        *p &= ~mask_to_clear;
        nbits -= bits_to_clear;
        bits_to_clear = BITS_PER_LONG;
        mask_to_clear = ~0UL;
        p++;
    }
    
    if (nbits) {
        mask_to_clear &= ~(~0UL << nbits);
        *p &= ~mask_to_clear;
    }
}

/*
 * bitmap_weight - Count set bits
 */
unsigned int bitmap_weight(const unsigned long *bitmap, unsigned int bits) {
    unsigned int k, w = 0;
    
    for (k = 0; k < BITS_TO_LONGS(bits); k++) {
        unsigned long v = bitmap[k];
        while (v) {
            w += (unsigned int)(v & 1UL);
            v >>= 1;
        }
    }
    
    return w;
}

/*
 * bitmap_zero - Clear all bits
 */
void bitmap_zero(unsigned long *bitmap, unsigned int bits) {
    memset(bitmap, 0, BITS_TO_LONGS(bits) * sizeof(unsigned long));
}

/*
 * bitmap_fill - Set all bits
 */
void bitmap_fill(unsigned long *bitmap, unsigned int bits) {
    memset(bitmap, 0xFF, BITS_TO_LONGS(bits) * sizeof(unsigned long));
}