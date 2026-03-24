/*
 * Obelisk OS - String Functions (Userland)
 * From Axioms, Order.
 */

#include <stdint.h>
#include <stddef.h>

void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    
    if (d < s) {
        while (n--) {
            *d++ = *s++;
        }
    } else {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    
    return dest;
}

void *memset(void *s, int c, size_t n) {
    uint8_t *p = s;
    while (n--) {
        *p++ = c;
    }
    return s;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *p1 = s1;
    const uint8_t *p2 = s2;
    
    while (n--) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    
    return 0;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (*s++) len++;
    return len;
}

char *strcpy(char *dest, const char *src) {
    char *ret = dest;
    while ((*dest++ = *src++));
    return ret;
}

char *strncpy(char *dest, const char *src, size_t n) {
    char *ret = dest;
    
    while (n && (*dest++ = *src++)) {
        n--;
    }
    
    while (n--) {
        *dest++ = '\0';
    }
    
    return ret;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    if (!n) return 0;
    
    while (--n && *s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == c) {
            return (char *)s;
        }
        s++;
    }
    return c == '\0' ? (char *)s : NULL;
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    
    while (*s) {
        if (*s == c) {
            last = s;
        }
        s++;
    }
    
    return (char *)(c == '\0' ? s : last);
}

char *strcat(char *dest, const char *src) {
    char *ret = dest;
    
    while (*dest) dest++;
    while ((*dest++ = *src++));
    
    return ret;
}

char *strstr(const char *haystack, const char *needle) {
    size_t needle_len = strlen(needle);
    
    if (!needle_len) return (char *)haystack;
    
    while (*haystack) {
        if (strncmp(haystack, needle, needle_len) == 0) {
            return (char *)haystack;
        }
        haystack++;
    }
    
    return NULL;
}

long strtol(const char *s, char **endptr, int base) {
    long result = 0;
    int sign = 1;
    
    while (*s == ' ' || *s == '\t') s++;
    
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    
    if (base == 0 || base == 16) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            base = 16;
            s += 2;
        } else if (base == 0) {
            base = s[0] == '0' ? 8 : 10;
        }
    }
    
    while (*s) {
        int digit;
        
        if (*s >= '0' && *s <= '9') {
            digit = *s - '0';
        } else if (*s >= 'a' && *s <= 'z') {
            digit = *s - 'a' + 10;
        } else if (*s >= 'A' && *s <= 'Z') {
            digit = *s - 'A' + 10;
        } else {
            break;
        }
        
        if (digit >= base) break;
        
        result = result * base + digit;
        s++;
    }
    
    if (endptr) *endptr = (char *)s;
    
    return result * sign;
}

int atoi(const char *s) {
    return (int)strtol(s, NULL, 10);
}