/*
 * Obelisk OS - UART Serial Driver
 * From Axioms, Order.
 *
 * Provides early console output via COM1 serial port.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <arch/cpu.h>

/* Serial port base addresses */
#define COM1_PORT   0x3F8
#define COM2_PORT   0x2F8
#define COM3_PORT   0x3E8
#define COM4_PORT   0x2E8

/* UART register offsets */
#define UART_DATA       0   /* Data register (R/W) */
#define UART_IER        1   /* Interrupt Enable Register */
#define UART_FCR        2   /* FIFO Control Register (W) */
#define UART_IIR        2   /* Interrupt ID Register (R) */
#define UART_LCR        3   /* Line Control Register */
#define UART_MCR        4   /* Modem Control Register */
#define UART_LSR        5   /* Line Status Register */
#define UART_MSR        6   /* Modem Status Register */
#define UART_SCRATCH    7   /* Scratch Register */

/* When DLAB=1 */
#define UART_DLL        0   /* Divisor Latch Low */
#define UART_DLH        1   /* Divisor Latch High */

/* Line Control Register bits */
#define LCR_DLAB        0x80    /* Divisor Latch Access Bit */
#define LCR_SBC         0x40    /* Set Break Control */
#define LCR_SPAR        0x20    /* Stick Parity */
#define LCR_EPAR        0x10    /* Even Parity */
#define LCR_PARITY      0x08    /* Parity Enable */
#define LCR_STOP        0x04    /* Stop Bits (0=1, 1=2) */
#define LCR_WLEN5       0x00    /* Word Length: 5 bits */
#define LCR_WLEN6       0x01    /* Word Length: 6 bits */
#define LCR_WLEN7       0x02    /* Word Length: 7 bits */
#define LCR_WLEN8       0x03    /* Word Length: 8 bits */

/* Line Status Register bits */
#define LSR_DR          0x01    /* Data Ready */
#define LSR_OE          0x02    /* Overrun Error */
#define LSR_PE          0x04    /* Parity Error */
#define LSR_FE          0x08    /* Framing Error */
#define LSR_BI          0x10    /* Break Interrupt */
#define LSR_THRE        0x20    /* Transmitter Holding Register Empty */
#define LSR_TEMT        0x40    /* Transmitter Empty */
#define LSR_ERR         0x80    /* Error in RCVR FIFO */

/* FIFO Control Register bits */
#define FCR_ENABLE      0x01    /* Enable FIFO */
#define FCR_RCVR_RST    0x02    /* Clear Receive FIFO */
#define FCR_XMIT_RST    0x04    /* Clear Transmit FIFO */
#define FCR_DMA_MODE    0x08    /* DMA Mode Select */
#define FCR_TRIGGER_1   0x00    /* Trigger Level: 1 byte */
#define FCR_TRIGGER_4   0x40    /* Trigger Level: 4 bytes */
#define FCR_TRIGGER_8   0x80    /* Trigger Level: 8 bytes */
#define FCR_TRIGGER_14  0xC0    /* Trigger Level: 14 bytes */

/* Modem Control Register bits */
#define MCR_DTR         0x01    /* Data Terminal Ready */
#define MCR_RTS         0x02    /* Request To Send */
#define MCR_OUT1        0x04    /* Out 1 */
#define MCR_OUT2        0x08    /* Out 2 (enables IRQ) */
#define MCR_LOOP        0x10    /* Loopback Mode */

/* Baud rate divisors (for 115200 base clock) */
#define BAUD_115200     1
#define BAUD_57600      2
#define BAUD_38400      3
#define BAUD_19200      6
#define BAUD_9600       12
#define BAUD_4800       24
#define BAUD_2400       48
#define BAUD_1200       96

/* Current serial port configuration */
static uint16_t uart_port = COM1_PORT;
static bool uart_initialized = false;

/* Initialize UART */
int uart_init(void) {
    uint16_t port = uart_port;
    
    /* Disable interrupts */
    outb(port + UART_IER, 0x00);
    
    /* Enable DLAB to set baud rate */
    outb(port + UART_LCR, LCR_DLAB);
    
    /* Set baud rate to 115200 */
    outb(port + UART_DLL, BAUD_115200);
    outb(port + UART_DLH, 0x00);
    
    /* 8 bits, no parity, 1 stop bit */
    outb(port + UART_LCR, LCR_WLEN8);
    
    /* Enable FIFO, clear buffers, 14-byte threshold */
    outb(port + UART_FCR, FCR_ENABLE | FCR_RCVR_RST | FCR_XMIT_RST | FCR_TRIGGER_14);
    
    /* Enable IRQs, RTS/DSR set */
    outb(port + UART_MCR, MCR_DTR | MCR_RTS | MCR_OUT2);
    
    /* Test the serial port */
    outb(port + UART_MCR, MCR_LOOP);  /* Loopback mode */
    outb(port + UART_DATA, 0xAE);     /* Send test byte */
    
    if (inb(port + UART_DATA) != 0xAE) {
        /* Serial port failed test */
        return -1;
    }
    
    /* Disable loopback, set normal operation */
    outb(port + UART_MCR, MCR_DTR | MCR_RTS | MCR_OUT2);
    
    uart_initialized = true;
    
    return 0;
}

/* Check if transmitter is ready */
static inline bool uart_tx_ready(void) {
    return (inb(uart_port + UART_LSR) & LSR_THRE) != 0;
}

/* Check if data is available */
static inline bool uart_rx_ready(void) {
    return (inb(uart_port + UART_LSR) & LSR_DR) != 0;
}

/* Write a single character */
void uart_putc(char c) {
    if (!uart_initialized) {
        return;
    }
    
    /* Wait for transmitter to be ready */
    while (!uart_tx_ready()) {
        pause();
    }
    
    outb(uart_port + UART_DATA, c);
}

/* Read a single character (blocking) */
char uart_getc(void) {
    if (!uart_initialized) {
        return 0;
    }
    
    /* Wait for data to be available */
    while (!uart_rx_ready()) {
        pause();
    }
    
    return inb(uart_port + UART_DATA);
}

/* Read a character (non-blocking) */
int uart_getc_nonblock(void) {
    if (!uart_initialized || !uart_rx_ready()) {
        return -1;
    }
    
    return inb(uart_port + UART_DATA);
}

/* Write a string */
void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') {
            uart_putc('\r');
        }
        uart_putc(*s++);
    }
}

/* Write a buffer */
void uart_write(const void *buf, size_t len) {
    const char *p = buf;
    while (len--) {
        uart_putc(*p++);
    }
}

/* Read into a buffer */
size_t uart_read(void *buf, size_t len) {
    char *p = buf;
    size_t count = 0;
    
    while (count < len) {
        int c = uart_getc_nonblock();
        if (c < 0) break;
        *p++ = c;
        count++;
    }
    
    return count;
}

/* Print a hexadecimal number */
void uart_puthex(uint64_t val) {
    static const char hex[] = "0123456789abcdef";
    char buf[17];
    int i;
    
    for (i = 15; i >= 0; i--) {
        buf[i] = hex[val & 0xF];
        val >>= 4;
    }
    buf[16] = '\0';
    
    uart_puts("0x");
    uart_puts(buf);
}

/* Print a decimal number */
void uart_putdec(uint64_t val) {
    char buf[21];
    int i = 20;
    
    buf[i] = '\0';
    
    if (val == 0) {
        uart_putc('0');
        return;
    }
    
    while (val > 0) {
        buf[--i] = '0' + (val % 10);
        val /= 10;
    }
    
    uart_puts(&buf[i]);
}

/* Set serial port */
void uart_set_port(int port_num) {
    switch (port_num) {
        case 1: uart_port = COM1_PORT; break;
        case 2: uart_port = COM2_PORT; break;
        case 3: uart_port = COM3_PORT; break;
        case 4: uart_port = COM4_PORT; break;
        default: uart_port = COM1_PORT; break;
    }
    
    if (uart_initialized) {
        uart_init();
    }
}