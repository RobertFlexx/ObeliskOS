/*
 * Obelisk OS - Standard I/O
 * From Axioms, Order.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

typedef long ssize_t;

extern ssize_t write(int fd, const void *buf, size_t count);
extern ssize_t read(int fd, void *buf, size_t count);

/* Standard file descriptors */
#define STDIN_FILENO    0
#define STDOUT_FILENO   1
#define STDERR_FILENO   2

/* Put character */
int putchar(int c) {
    char ch = c;
    write(STDOUT_FILENO, &ch, 1);
    return c;
}

/* Put string */
int puts(const char *s) {
    while (*s) {
        putchar(*s++);
    }
    putchar('\n');
    return 0;
}

/* Get character */
int getchar(void) {
    char c;
    if (read(STDIN_FILENO, &c, 1) <= 0) {
        return -1;
    }
    return c;
}

/* Print integer */
static void print_int(int value, int base, int width, char pad) {
    char buf[32];
    int i = 0;
    int negative = 0;
    unsigned int magnitude;
    
    if (value < 0 && base == 10) {
        negative = 1;
        magnitude = (unsigned int)(-(value + 1)) + 1U;
    } else {
        magnitude = (unsigned int)value;
    }
    
    if (magnitude == 0) {
        buf[i++] = '0';
    } else {
        while (magnitude > 0) {
            unsigned int digit = magnitude % (unsigned int)base;
            buf[i++] = digit < 10 ? '0' + digit : 'a' + digit - 10;
            magnitude /= (unsigned int)base;
        }
    }
    
    if (negative) {
        buf[i++] = '-';
    }
    
    while (i < width) {
        buf[i++] = pad;
    }
    
    while (i > 0) {
        putchar(buf[--i]);
    }
}

/* Print unsigned integer */
static void print_uint(unsigned long value, int base, int width, char pad) {
    char buf[32];
    int i = 0;
    
    if (value == 0) {
        buf[i++] = '0';
    } else {
        while (value > 0) {
            int digit = value % base;
            buf[i++] = digit < 10 ? '0' + digit : 'a' + digit - 10;
            value /= base;
        }
    }
    
    while (i < width) {
        buf[i++] = pad;
    }
    
    while (i > 0) {
        putchar(buf[--i]);
    }
}

/* Simple printf */
static int vprintf_impl(const char *fmt, va_list args_in) {
    va_list args;
    va_copy(args, args_in);
    
    int count = 0;
    
    while (*fmt) {
        if (*fmt != '%') {
            putchar(*fmt);
            count++;
            fmt++;
            continue;
        }
        
        fmt++;
        
        /* Handle flags */
        char pad = ' ';
        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        
        /* Handle width */
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        
        /* Handle format */
        switch (*fmt) {
            case 'd':
            case 'i':
                print_int(va_arg(args, int), 10, width, pad);
                break;
            case 'u':
                print_uint(va_arg(args, unsigned int), 10, width, pad);
                break;
            case 'x':
                print_uint(va_arg(args, unsigned int), 16, width, pad);
                break;
            case 'p':
                putchar('0');
                putchar('x');
                print_uint(va_arg(args, unsigned long), 16, 16, '0');
                break;
            case 's': {
                const char *s = va_arg(args, const char *);
                if (!s) s = "(null)";
                while (*s) {
                    putchar(*s++);
                    count++;
                }
                break;
            }
            case 'c':
                putchar(va_arg(args, int));
                count++;
                break;
            case '%':
                putchar('%');
                count++;
                break;
            default:
                putchar('%');
                putchar(*fmt);
                count += 2;
        }
        
        fmt++;
    }
    
    va_end(args);
    return count;
}

int printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vprintf_impl(fmt, args);
    va_end(args);
    return ret;
}

/* fprintf to stderr */
int fprintf_stderr(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vprintf_impl(fmt, args);
    va_end(args);
    return ret;
}