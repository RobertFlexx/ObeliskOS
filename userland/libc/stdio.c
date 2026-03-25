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
static int g_stdio_out_fd = STDOUT_FILENO;

/* Put character */
int putchar(int c) {
    char ch = c;
    write(g_stdio_out_fd, &ch, 1);
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

static int print_uint_base(uint64_t value, unsigned base, int width, char pad, int uppercase) {
    char buf[64];
    int i = 0;
    int out = 0;
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";

    if (base < 2 || base > 16) {
        return 0;
    }
    if (value == 0) {
        buf[i++] = '0';
    } else {
        while (value > 0 && i < (int)sizeof(buf)) {
            unsigned d = (unsigned)(value % (uint64_t)base);
            buf[i++] = digits[d];
            value /= (uint64_t)base;
        }
    }
    while (i < width && i < (int)sizeof(buf)) {
        buf[i++] = pad;
    }
    while (i > 0) {
        putchar(buf[--i]);
        out++;
    }
    return out;
}

static int print_int_base(int64_t value, unsigned base, int width, char pad) {
    uint64_t mag;
    int out = 0;
    int num_width = width;
    if (base != 10) {
        return print_uint_base((uint64_t)value, base, width, pad, 0);
    }
    if (value < 0) {
        putchar('-');
        out++;
        if (num_width > 0) num_width--;
        mag = (uint64_t)(-(value + 1)) + 1ULL;
    } else {
        mag = (uint64_t)value;
    }
    out += print_uint_base(mag, base, num_width, pad, 0);
    return out;
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
        
        /* Handle length modifiers */
        int length = 0; /* 0=default,1=l,2=ll,3=z */
        if (*fmt == 'l') {
            length = 1;
            fmt++;
            if (*fmt == 'l') {
                length = 2;
                fmt++;
            }
        } else if (*fmt == 'z') {
            length = 3;
            fmt++;
        }

        /* Handle format */
        switch (*fmt) {
            case 'd':
            case 'i': {
                int64_t v;
                if (length == 2) v = va_arg(args, long long);
                else if (length == 1) v = va_arg(args, long);
                else if (length == 3) v = (int64_t)va_arg(args, ssize_t);
                else v = va_arg(args, int);
                count += print_int_base(v, 10, width, pad);
                break;
            }
            case 'u':
            {
                uint64_t v;
                if (length == 2) v = va_arg(args, unsigned long long);
                else if (length == 1) v = va_arg(args, unsigned long);
                else if (length == 3) v = (uint64_t)va_arg(args, size_t);
                else v = va_arg(args, unsigned int);
                count += print_uint_base(v, 10, width, pad, 0);
                break;
            }
            case 'x':
            case 'X':
            {
                uint64_t v;
                int upper = (*fmt == 'X');
                if (length == 2) v = va_arg(args, unsigned long long);
                else if (length == 1) v = va_arg(args, unsigned long);
                else if (length == 3) v = (uint64_t)va_arg(args, size_t);
                else v = va_arg(args, unsigned int);
                count += print_uint_base(v, 16, width, pad, upper);
                break;
            }
            case 'p':
                putchar('0');
                putchar('x');
                count += 2;
                count += print_uint_base((uint64_t)(uintptr_t)va_arg(args, void *), 16, 2 * (int)sizeof(void *), '0', 0);
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
    g_stdio_out_fd = STDOUT_FILENO;
    int ret = vprintf_impl(fmt, args);
    g_stdio_out_fd = STDOUT_FILENO;
    va_end(args);
    return ret;
}

/* fprintf to stderr */
int fprintf_stderr(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    g_stdio_out_fd = STDERR_FILENO;
    int ret = vprintf_impl(fmt, args);
    g_stdio_out_fd = STDOUT_FILENO;
    va_end(args);
    return ret;
}