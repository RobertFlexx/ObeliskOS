/*
 * Obelisk OS - Printf Implementation
 * From Axioms, Order.
 *
 * Standalone printf implementation for various contexts.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <stdarg.h>

int vsprintf(char *buf, const char *fmt, va_list args);
int vsnprintf_full(char *buf, size_t size, const char *fmt, va_list args);

/* Output callback type */
typedef void (*printf_output_fn)(char c, void *arg);

/* Internal printf state */
struct printf_state {
    printf_output_fn output;
    void *arg;
    int count;
};

/* Output a character */
static void output_char(struct printf_state *state, char c) {
    state->output(c, state->arg);
    state->count++;
}

/* Output a string */
static void output_string(struct printf_state *state, const char *s) {
    while (*s) {
        output_char(state, *s++);
    }
}

/* Output padding */
static void output_padding(struct printf_state *state, int count, char pad) {
    while (count-- > 0) {
        output_char(state, pad);
    }
}

/* Output an unsigned integer */
static void output_unsigned(struct printf_state *state, uint64_t value,
                            int base, int width, char pad, bool uppercase) {
    static const char digits_lower[] = "0123456789abcdef";
    static const char digits_upper[] = "0123456789ABCDEF";
    const char *digits = uppercase ? digits_upper : digits_lower;
    
    char buf[64];
    int len = 0;
    
    /* Generate digits in reverse order */
    if (value == 0) {
        buf[len++] = '0';
    } else {
        while (value > 0) {
            buf[len++] = digits[value % base];
            value /= base;
        }
    }
    
    /* Pad if necessary */
    output_padding(state, width - len, pad);
    
    /* Output digits in correct order */
    while (len > 0) {
        output_char(state, buf[--len]);
    }
}

/* Output a signed integer */
static void output_signed(struct printf_state *state, int64_t value,
                          int width, char pad) {
    bool negative = false;
    
    if (value < 0) {
        negative = true;
        value = -value;
    }
    
    /* Calculate length */
    uint64_t temp = value;
    int len = negative ? 1 : 0;
    
    if (temp == 0) {
        len = 1;
    } else {
        while (temp > 0) {
            len++;
            temp /= 10;
        }
    }
    
    /* Pad */
    if (pad == '0' && negative) {
        output_char(state, '-');
        negative = false;
    }
    output_padding(state, width - len, pad);
    
    if (negative) {
        output_char(state, '-');
    }
    
    output_unsigned(state, value, 10, 0, ' ', false);
}

/* Core printf implementation */
static int do_vprintf(struct printf_state *state, const char *fmt, va_list args) {
    while (*fmt) {
        if (*fmt != '%') {
            output_char(state, *fmt++);
            continue;
        }
        
        fmt++;  /* Skip '%' */
        
        if (*fmt == '%') {
            output_char(state, '%');
            fmt++;
            continue;
        }
        
        /* Parse flags */
        bool zero_pad = false;
        bool left_justify = false;
        bool show_sign = false;
        bool space_sign = false;
        bool alt_form = false;
        
        while (1) {
            switch (*fmt) {
                case '0': zero_pad = true; fmt++; continue;
                case '-': left_justify = true; fmt++; continue;
                case '+': show_sign = true; fmt++; continue;
                case ' ': space_sign = true; fmt++; continue;
                case '#': alt_form = true; fmt++; continue;
            }
            break;
        }
        
        /* Parse width */
        int width = 0;
        if (*fmt == '*') {
            width = va_arg(args, int);
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }
        }
        
        /* Parse precision */
        int precision = -1;
        if (*fmt == '.') {
            fmt++;
            precision = 0;
            if (*fmt == '*') {
                precision = va_arg(args, int);
                fmt++;
            } else {
                while (*fmt >= '0' && *fmt <= '9') {
                    precision = precision * 10 + (*fmt - '0');
                    fmt++;
                }
            }
        }
        
        /* Parse length modifier */
        bool is_long = false;
        bool is_longlong = false;
        bool is_short = false;
        bool is_char = false;
        
        switch (*fmt) {
            case 'h':
                fmt++;
                if (*fmt == 'h') {
                    is_char = true;
                    fmt++;
                } else {
                    is_short = true;
                }
                break;
            case 'l':
                fmt++;
                if (*fmt == 'l') {
                    is_longlong = true;
                    fmt++;
                } else {
                    is_long = true;
                }
                break;
            case 'z':
            case 't':
                is_longlong = true;
                fmt++;
                break;
        }
        
        char pad = (zero_pad && !left_justify) ? '0' : ' ';
        
        /* Parse conversion specifier */
        switch (*fmt) {
            case 'd':
            case 'i': {
                int64_t val;
                if (is_longlong) {
                    val = va_arg(args, int64_t);
                } else if (is_long) {
                    val = va_arg(args, long);
                } else if (is_short) {
                    val = (short)va_arg(args, int);
                } else if (is_char) {
                    val = (signed char)va_arg(args, int);
                } else {
                    val = va_arg(args, int);
                }
                if (val >= 0 && (show_sign || space_sign)) {
                    output_char(state, show_sign ? '+' : ' ');
                    if (width > 0) {
                        width--;
                    }
                }
                output_signed(state, val, width, pad);
                break;
            }
            
            case 'u': {
                uint64_t val;
                if (is_longlong) {
                    val = va_arg(args, uint64_t);
                } else if (is_long) {
                    val = va_arg(args, unsigned long);
                } else if (is_short) {
                    val = (unsigned short)va_arg(args, unsigned int);
                } else if (is_char) {
                    val = (unsigned char)va_arg(args, unsigned int);
                } else {
                    val = va_arg(args, unsigned int);
                }
                output_unsigned(state, val, 10, width, pad, false);
                break;
            }
            
            case 'x':
            case 'X': {
                uint64_t val;
                if (is_longlong) {
                    val = va_arg(args, uint64_t);
                } else if (is_long) {
                    val = va_arg(args, unsigned long);
                } else if (is_short) {
                    val = (unsigned short)va_arg(args, unsigned int);
                } else if (is_char) {
                    val = (unsigned char)va_arg(args, unsigned int);
                } else {
                    val = va_arg(args, unsigned int);
                }
                if (alt_form && val != 0) {
                    output_string(state, *fmt == 'X' ? "0X" : "0x");
                    width -= 2;
                }
                output_unsigned(state, val, 16, width, pad, *fmt == 'X');
                break;
            }
            
            case 'o': {
                uint64_t val;
                if (is_longlong) {
                    val = va_arg(args, uint64_t);
                } else if (is_long) {
                    val = va_arg(args, unsigned long);
                } else if (is_short) {
                    val = (unsigned short)va_arg(args, unsigned int);
                } else if (is_char) {
                    val = (unsigned char)va_arg(args, unsigned int);
                } else {
                    val = va_arg(args, unsigned int);
                }
                if (alt_form && val != 0) {
                    output_char(state, '0');
                    width--;
                }
                output_unsigned(state, val, 8, width, pad, false);
                break;
            }
            
            case 'p': {
                void *ptr = va_arg(args, void *);
                output_string(state, "0x");
                output_unsigned(state, (uint64_t)ptr, 16, 16, '0', false);
                break;
            }
            
            case 's': {
                const char *s = va_arg(args, const char *);
                if (!s) s = "(null)";
                
                int len = strlen(s);
                if (precision >= 0 && precision < len) {
                    len = precision;
                }
                
                if (!left_justify) {
                    output_padding(state, width - len, ' ');
                }
                
                for (int i = 0; i < len; i++) {
                    output_char(state, s[i]);
                }
                
                if (left_justify) {
                    output_padding(state, width - len, ' ');
                }
                break;
            }
            
            case 'c': {
                char c = (char)va_arg(args, int);
                if (!left_justify) {
                    output_padding(state, width - 1, ' ');
                }
                output_char(state, c);
                if (left_justify) {
                    output_padding(state, width - 1, ' ');
                }
                break;
            }
            
            case 'n': {
                int *ptr = va_arg(args, int *);
                *ptr = state->count;
                break;
            }
            
            default:
                output_char(state, '%');
                output_char(state, *fmt);
                break;
        }
        
        fmt++;
    }
    
    return state->count;
}

/* Buffer output callback */
struct buffer_state {
    char *buf;
    size_t size;
    size_t pos;
};

static void buffer_output(char c, void *arg) {
    struct buffer_state *bs = arg;
    if (bs->pos < bs->size - 1) {
        bs->buf[bs->pos] = c;
    }
    bs->pos++;
}

/* sprintf implementation */
int sprintf(char *buf, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vsprintf(buf, fmt, args);
    va_end(args);
    return ret;
}

/* vsprintf implementation */
int vsprintf(char *buf, const char *fmt, va_list args) {
    struct buffer_state bs = { buf, SIZE_MAX, 0 };
    struct printf_state state = { buffer_output, &bs, 0 };
    
    int ret = do_vprintf(&state, fmt, args);
    buf[bs.pos] = '\0';
    return ret;
}

/* snprintf implementation (already in printk.c but full version here) */
int snprintf_full(char *buf, size_t size, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf_full(buf, size, fmt, args);
    va_end(args);
    return ret;
}

/* vsnprintf implementation */
int vsnprintf_full(char *buf, size_t size, const char *fmt, va_list args) {
    if (size == 0) return 0;
    
    struct buffer_state bs = { buf, size, 0 };
    struct printf_state state = { buffer_output, &bs, 0 };
    
    int ret = do_vprintf(&state, fmt, args);
    
    if (bs.pos < size) {
        buf[bs.pos] = '\0';
    } else {
        buf[size - 1] = '\0';
    }
    
    return ret;
}