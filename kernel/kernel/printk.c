/*
 * Obelisk OS - Kernel Printf
 * From Axioms, Order.
 *
 * Provides formatted output for kernel messages.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <stdarg.h>

/* Output function */
extern void uart_putc(char c);
extern void uart_puts(const char *s);

/* VGA text console mirror (for local hardware debugging). */
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_ATTR 0x0F
static volatile uint16_t *vga_buf = (volatile uint16_t *)PHYS_TO_VIRT(0xB8000);
static size_t vga_row = 0;
static size_t vga_col = 0;

static void vga_scroll(void) {
    for (size_t r = 1; r < VGA_HEIGHT; r++) {
        for (size_t c = 0; c < VGA_WIDTH; c++) {
            vga_buf[(r - 1) * VGA_WIDTH + c] = vga_buf[r * VGA_WIDTH + c];
        }
    }
    for (size_t c = 0; c < VGA_WIDTH; c++) {
        vga_buf[(VGA_HEIGHT - 1) * VGA_WIDTH + c] = ((uint16_t)VGA_ATTR << 8) | ' ';
    }
    if (vga_row > 0) vga_row--;
}

static void vga_putc(char c) {
    if (c == '\r') {
        vga_col = 0;
        return;
    }
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
        if (vga_row >= VGA_HEIGHT) {
            vga_scroll();
        }
        return;
    }

    vga_buf[vga_row * VGA_WIDTH + vga_col] = ((uint16_t)VGA_ATTR << 8) | (uint8_t)c;
    vga_col++;
    if (vga_col >= VGA_WIDTH) {
        vga_col = 0;
        vga_row++;
        if (vga_row >= VGA_HEIGHT) {
            vga_scroll();
        }
    }
}

/* Log buffer */
#define LOG_BUF_SIZE    (128 * 1024)    /* 128 KB */
static char log_buffer[LOG_BUF_SIZE];
static size_t log_write_idx = 0;
static size_t log_read_idx = 0;

/* Current log level */
static int console_loglevel = 7;    /* Print everything */
static int default_loglevel = 4;    /* KERN_WARNING */

/* Log level names */

/* Add character to log buffer */
static void log_buffer_add(char c) {
    log_buffer[log_write_idx] = c;
    log_write_idx = (log_write_idx + 1) % LOG_BUF_SIZE;
    
    /* Handle overflow */
    if (log_write_idx == log_read_idx) {
        log_read_idx = (log_read_idx + 1) % LOG_BUF_SIZE;
    }
}

/* Output character */
static void printk_putc(char c) {
    log_buffer_add(c);
    uart_putc(c);
    vga_putc(c);
}

void console_putc(char c) {
    printk_putc(c);
}

void console_write(const char *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        printk_putc(buf[i]);
    }
}

/* Output string */
static void printk_puts(const char *s) {
    while (*s) {
        printk_putc(*s++);
    }
}

/* Print unsigned number */
static void print_number(uint64_t num, int base, int width, char pad,
                         bool uppercase, bool sign, bool is_negative) {
    static const char digits_lower[] = "0123456789abcdef";
    static const char digits_upper[] = "0123456789ABCDEF";
    const char *digits = uppercase ? digits_upper : digits_lower;
    
    char buf[64];
    int i = 0;
    
    if (num == 0) {
        buf[i++] = '0';
    } else {
        while (num > 0) {
            buf[i++] = digits[num % base];
            num /= base;
        }
    }
    
    /* Calculate padding */
    int total_len = i;
    if (sign || is_negative) total_len++;
    
    /* Add padding */
    while (total_len < width) {
        if (pad == '0' && (sign || is_negative)) {
            if (is_negative) {
                printk_putc('-');
                is_negative = false;
            } else if (sign) {
                printk_putc('+');
                sign = false;
            }
        }
        printk_putc(pad);
        total_len++;
    }
    
    /* Print sign */
    if (is_negative) {
        printk_putc('-');
    } else if (sign) {
        printk_putc('+');
    }
    
    /* Print digits in reverse */
    while (i > 0) {
        printk_putc(buf[--i]);
    }
}

/* Print signed number */
static void print_signed(int64_t num, int base, int width, char pad, bool sign) {
    bool is_negative = false;
    
    if (num < 0) {
        is_negative = true;
        num = -num;
    }
    
    print_number((uint64_t)num, base, width, pad, false, sign, is_negative);
}

/* Core printf implementation */
static int do_printf(const char *fmt, va_list args) {
    int count = 0;
    int width;
    char pad;
    bool is_long, is_longlong;
    
    while (*fmt) {
        if (*fmt != '%') {
            printk_putc(*fmt);
            count++;
            fmt++;
            continue;
        }
        
        fmt++;  /* Skip '%' */
        
        /* Check for %% */
        if (*fmt == '%') {
            printk_putc('%');
            count++;
            fmt++;
            continue;
        }
        
        /* Parse flags */
        pad = ' ';
        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        
        /* Parse width */
        width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        
        /* Parse length modifier */
        is_long = false;
        is_longlong = false;
        
        if (*fmt == 'l') {
            is_long = true;
            fmt++;
            if (*fmt == 'l') {
                is_longlong = true;
                fmt++;
            }
        } else if (*fmt == 'z') {
            is_longlong = true;  /* size_t */
            fmt++;
        }
        
        /* Parse conversion specifier */
        switch (*fmt) {
            case 'd':
            case 'i': {
                int64_t val;
                if (is_longlong) {
                    val = va_arg(args, int64_t);
                } else if (is_long) {
                    val = va_arg(args, long);
                } else {
                    val = va_arg(args, int);
                }
                print_signed(val, 10, width, pad, false);
                break;
            }
            
            case 'u': {
                uint64_t val;
                if (is_longlong) {
                    val = va_arg(args, uint64_t);
                } else if (is_long) {
                    val = va_arg(args, unsigned long);
                } else {
                    val = va_arg(args, unsigned int);
                }
                print_number(val, 10, width, pad, false, false, false);
                break;
            }
            
            case 'x': {
                uint64_t val;
                if (is_longlong) {
                    val = va_arg(args, uint64_t);
                } else if (is_long) {
                    val = va_arg(args, unsigned long);
                } else {
                    val = va_arg(args, unsigned int);
                }
                print_number(val, 16, width, pad, false, false, false);
                break;
            }
            
            case 'X': {
                uint64_t val;
                if (is_longlong) {
                    val = va_arg(args, uint64_t);
                } else if (is_long) {
                    val = va_arg(args, unsigned long);
                } else {
                    val = va_arg(args, unsigned int);
                }
                print_number(val, 16, width, pad, true, false, false);
                break;
            }
            
            case 'p': {
                void *ptr = va_arg(args, void *);
                printk_puts("0x");
                print_number((uint64_t)ptr, 16, 16, '0', false, false, false);
                break;
            }
            
            case 's': {
                const char *s = va_arg(args, const char *);
                if (!s) s = "(null)";
                
                int len = strlen(s);
                while (len < width) {
                    printk_putc(' ');
                    len++;
                }
                printk_puts(s);
                break;
            }
            
            case 'c': {
                char c = (char)va_arg(args, int);
                printk_putc(c);
                break;
            }
            
            default:
                printk_putc('%');
                printk_putc(*fmt);
                break;
        }
        
        fmt++;
    }
    
    return count;
}

/*
 * printk - Kernel printf
 * @fmt: Format string (with optional log level prefix)
 * @...: Format arguments
 */
int printk(const char *fmt, ...) {
    va_list args;
    int ret;
    int level = default_loglevel;
    
    /* Parse log level */
    if (fmt[0] == '<' && fmt[1] >= '0' && fmt[1] <= '7' && fmt[2] == '>') {
        level = fmt[1] - '0';
        fmt += 3;
    }
    
    /* Check if we should print this message */
    if (level > console_loglevel) {
        /* Still add to log buffer but don't print */
        va_start(args, fmt);
        /* TODO: Add to buffer without printing */
        va_end(args);
        return 0;
    }
    
    va_start(args, fmt);
    ret = do_printf(fmt, args);
    va_end(args);
    
    return ret;
}

/*
 * vprintk - Kernel vprintf
 * @fmt: Format string
 * @args: va_list arguments
 */
int vprintk(const char *fmt, va_list args) {
    return do_printf(fmt, args);
}

/*
 * snprintf - Print to buffer
 */
int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list args;
    int ret;
    
    va_start(args, fmt);
    ret = vsnprintf(buf, size, fmt, args);
    va_end(args);
    
    return ret;
}

/*
 * vsnprintf - Print to buffer with va_list
 */
static size_t vsnprintf_num(char *buf, size_t pos, size_t size,
                            uint64_t num, int base, bool upper) {
    static const char lo[] = "0123456789abcdef";
    static const char up[] = "0123456789ABCDEF";
    const char *d = upper ? up : lo;
    char tmp[24];
    int n = 0;
    if (num == 0) {
        tmp[n++] = '0';
    } else {
        while (num > 0 && n < (int)sizeof(tmp)) {
            tmp[n++] = d[num % base];
            num /= base;
        }
    }
    while (n > 0 && pos + 1 < size) {
        buf[pos++] = tmp[--n];
    }
    return pos;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list args) {
    size_t i = 0;
    if (size == 0) return 0;

    while (*fmt && i + 1 < size) {
        if (*fmt != '%') {
            buf[i++] = *fmt++;
            continue;
        }
        fmt++;

        if (*fmt == '%') { buf[i++] = '%'; fmt++; continue; }

        /* Parse length modifier. */
        bool is_long = false;
        bool is_longlong = false;
        if (*fmt == 'l') {
            is_long = true; fmt++;
            if (*fmt == 'l') { is_longlong = true; fmt++; }
        } else if (*fmt == 'z') {
            is_longlong = true; fmt++;
        }

        switch (*fmt) {
            case 's': {
                const char *s = va_arg(args, const char *);
                if (!s) s = "(null)";
                while (*s && i + 1 < size) buf[i++] = *s++;
                break;
            }
            case 'd':
            case 'i': {
                int64_t val;
                if (is_longlong)      val = va_arg(args, int64_t);
                else if (is_long)     val = va_arg(args, long);
                else                  val = va_arg(args, int);
                if (val < 0 && i + 1 < size) { buf[i++] = '-'; val = -val; }
                i = vsnprintf_num(buf, i, size, (uint64_t)val, 10, false);
                break;
            }
            case 'u': {
                uint64_t val;
                if (is_longlong)      val = va_arg(args, uint64_t);
                else if (is_long)     val = va_arg(args, unsigned long);
                else                  val = va_arg(args, unsigned int);
                i = vsnprintf_num(buf, i, size, val, 10, false);
                break;
            }
            case 'x': {
                uint64_t val;
                if (is_longlong)      val = va_arg(args, uint64_t);
                else if (is_long)     val = va_arg(args, unsigned long);
                else                  val = va_arg(args, unsigned int);
                i = vsnprintf_num(buf, i, size, val, 16, false);
                break;
            }
            case 'X': {
                uint64_t val;
                if (is_longlong)      val = va_arg(args, uint64_t);
                else if (is_long)     val = va_arg(args, unsigned long);
                else                  val = va_arg(args, unsigned int);
                i = vsnprintf_num(buf, i, size, val, 16, true);
                break;
            }
            case 'p': {
                uint64_t val = (uint64_t)va_arg(args, void *);
                if (i + 2 < size) { buf[i++] = '0'; buf[i++] = 'x'; }
                i = vsnprintf_num(buf, i, size, val, 16, false);
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                buf[i++] = c;
                break;
            }
            default:
                buf[i++] = '%';
                if (*fmt && i + 1 < size) buf[i++] = *fmt;
                break;
        }
        if (*fmt) fmt++;
    }

    buf[i] = '\0';
    return (int)i;
}

/* Get/set log level */
int printk_get_loglevel(void) {
    return console_loglevel;
}

void printk_set_loglevel(int level) {
    if (level >= 0 && level <= 7) {
        console_loglevel = level;
    }
}