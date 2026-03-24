/*
 * Obelisk OS - String Functions
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>

/*
 * strlen - Calculate string length
 */
size_t strlen(const char *s) {
    const char *p = s;
    while (*p) p++;
    return p - s;
}

/*
 * strnlen - Calculate string length with limit
 */
size_t strnlen(const char *s, size_t maxlen) {
    const char *p = s;
    while (maxlen-- && *p) p++;
    return p - s;
}

/*
 * strcpy - Copy string
 */
char *strcpy(char *dest, const char *src) {
    char *ret = dest;
    while ((*dest++ = *src++));
    return ret;
}

/*
 * strncpy - Copy string with length limit
 */
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

/*
 * strcat - Concatenate strings
 */
char *strcat(char *dest, const char *src) {
    char *ret = dest;
    
    while (*dest) dest++;
    while ((*dest++ = *src++));
    
    return ret;
}

/*
 * strncat - Concatenate strings with limit
 */
char *strncat(char *dest, const char *src, size_t n) {
    char *ret = dest;
    
    while (*dest) dest++;
    
    while (n-- && (*dest = *src++)) {
        dest++;
    }
    *dest = '\0';
    
    return ret;
}

/*
 * strcmp - Compare strings
 */
int strcmp(const char *s1, const char *s2) {
    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

/*
 * strncmp - Compare strings with limit
 */
int strncmp(const char *s1, const char *s2, size_t n) {
    if (!n) return 0;
    
    while (--n && *s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

/*
 * strcasecmp - Case-insensitive string compare
 */
int strcasecmp(const char *s1, const char *s2) {
    unsigned char c1, c2;
    
    do {
        c1 = *s1++;
        c2 = *s2++;
        
        /* Convert to lowercase */
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        
    } while (c1 && c1 == c2);
    
    return c1 - c2;
}

/*
 * strchr - Find character in string
 */
char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) {
            return (char *)s;
        }
        s++;
    }
    return (c == '\0') ? (char *)s : NULL;
}

/*
 * strrchr - Find last occurrence of character
 */
char *strrchr(const char *s, int c) {
    const char *last = NULL;
    
    while (*s) {
        if (*s == (char)c) {
            last = s;
        }
        s++;
    }
    
    if (c == '\0') return (char *)s;
    return (char *)last;
}

/*
 * strstr - Find substring
 */
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

/*
 * strdup - Duplicate string
 */
char *strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *new = kmalloc(len);
    
    if (new) {
        memcpy(new, s, len);
    }
    
    return new;
}

/*
 * strndup - Duplicate string with limit
 */
char *strndup(const char *s, size_t n) {
    size_t len = strnlen(s, n);
    char *new = kmalloc(len + 1);
    
    if (new) {
        memcpy(new, s, len);
        new[len] = '\0';
    }
    
    return new;
}

/*
 * strtok_r - Thread-safe tokenize string
 */
char *strtok_r(char *str, const char *delim, char **saveptr) {
    char *token;
    
    if (str == NULL) {
        str = *saveptr;
    }
    
    /* Skip leading delimiters */
    while (*str && strchr(delim, *str)) {
        str++;
    }
    
    if (*str == '\0') {
        *saveptr = str;
        return NULL;
    }
    
    /* Find end of token */
    token = str;
    while (*str && !strchr(delim, *str)) {
        str++;
    }
    
    if (*str) {
        *str++ = '\0';
    }
    
    *saveptr = str;
    return token;
}

/*
 * strtok - Tokenize string (not thread-safe)
 */
char *strtok(char *str, const char *delim) {
    static char *saveptr;
    return strtok_r(str, delim, &saveptr);
}

/*
 * memcpy - Copy memory
 */
void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    
    /* Optimized for large copies */
    if (n >= 8 && ((uint64_t)d & 7) == 0 && ((uint64_t)s & 7) == 0) {
        uint64_t *d64 = (uint64_t *)d;
        const uint64_t *s64 = (const uint64_t *)s;
        
        while (n >= 8) {
            *d64++ = *s64++;
            n -= 8;
        }
        
        d = (uint8_t *)d64;
        s = (const uint8_t *)s64;
    }
    
    while (n--) {
        *d++ = *s++;
    }
    
    return dest;
}

/*
 * memmove - Copy memory (handles overlap)
 */
void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    
    if (d < s || d >= s + n) {
        /* No overlap or dest is before src */
        return memcpy(dest, src, n);
    }
    
    /* Copy backwards */
    d += n;
    s += n;
    
    while (n--) {
        *--d = *--s;
    }
    
    return dest;
}

/*
 * memset - Fill memory with byte
 */
void *memset(void *s, int c, size_t n) {
    uint8_t *p = s;
    uint8_t byte = (uint8_t)c;
    
    /* Optimized for large fills */
    if (n >= 8 && ((uint64_t)p & 7) == 0) {
        uint64_t fill = byte;
        fill |= fill << 8;
        fill |= fill << 16;
        fill |= fill << 32;
        
        uint64_t *p64 = (uint64_t *)p;
        while (n >= 8) {
            *p64++ = fill;
            n -= 8;
        }
        p = (uint8_t *)p64;
    }
    
    while (n--) {
        *p++ = byte;
    }
    
    return s;
}

/*
 * memcmp - Compare memory
 */
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

/*
 * memchr - Find byte in memory
 */
void *memchr(const void *s, int c, size_t n) {
    const uint8_t *p = s;
    uint8_t byte = (uint8_t)c;
    
    while (n--) {
        if (*p == byte) {
            return (void *)p;
        }
        p++;
    }
    
    return NULL;
}

/*
 * bzero - Zero memory
 */
void bzero(void *s, size_t n) {
    memset(s, 0, n);
}

/*
 * atoi - Convert string to integer
 */
int atoi(const char *s) {
    int result = 0;
    int sign = 1;
    
    /* Skip whitespace */
    while (*s == ' ' || *s == '\t') s++;
    
    /* Handle sign */
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    
    /* Convert digits */
    while (*s >= '0' && *s <= '9') {
        result = result * 10 + (*s - '0');
        s++;
    }
    
    return result * sign;
}

/*
 * atol - Convert string to long
 */
long atol(const char *s) {
    long result = 0;
    int sign = 1;
    
    while (*s == ' ' || *s == '\t') s++;
    
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    
    while (*s >= '0' && *s <= '9') {
        result = result * 10 + (*s - '0');
        s++;
    }
    
    return result * sign;
}

/*
 * strtoul - Convert string to unsigned long
 */
unsigned long strtoul(const char *s, char **endptr, int base) {
    unsigned long result = 0;
    
    /* Skip whitespace */
    while (*s == ' ' || *s == '\t') s++;
    
    /* Handle optional sign */
    if (*s == '+') s++;
    
    /* Detect base */
    if (base == 0) {
        if (*s == '0') {
            s++;
            if (*s == 'x' || *s == 'X') {
                base = 16;
                s++;
            } else {
                base = 8;
            }
        } else {
            base = 10;
        }
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    
    /* Convert */
    while (*s) {
        int digit;
        
        if (*s >= '0' && *s <= '9') {
            digit = *s - '0';
        } else if (*s >= 'a' && *s <= 'f') {
            digit = *s - 'a' + 10;
        } else if (*s >= 'A' && *s <= 'F') {
            digit = *s - 'A' + 10;
        } else {
            break;
        }
        
        if (digit >= base) break;
        
        result = result * base + digit;
        s++;
    }
    
    if (endptr) *endptr = (char *)s;
    return result;
}