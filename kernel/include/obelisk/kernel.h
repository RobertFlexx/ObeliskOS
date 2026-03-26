/*
 * Obelisk OS - Kernel Core Definitions
 * From Axioms, Order.
 */

#ifndef _OBELISK_KERNEL_H
#define _OBELISK_KERNEL_H

#include <obelisk/types.h>
#include <obelisk/errno.h>
#include <stdarg.h>

struct cpu_regs;
typedef void (*irq_handler_fn_t)(uint8_t irq, struct cpu_regs *regs, void *ctx);

/* Kernel version */
#define OBELISK_VERSION_MAJOR   0
#define OBELISK_VERSION_MINOR   1
#define OBELISK_VERSION_PATCH   0
#define OBELISK_VERSION_STRING  "0.1.0"
#define OBELISK_CODENAME        "Axiom"

/* Kernel configuration */
#define CONFIG_MAX_CPUS         64
#define CONFIG_MAX_PROCESSES    4096
#define CONFIG_MAX_FILES        65536
#define CONFIG_MAX_MOUNTS       256
#define CONFIG_KERNEL_STACK_SIZE    (16 * 1024)
#define CONFIG_USER_STACK_SIZE      (8 * 1024 * 1024)

/* Virtual address space layout (higher-half kernel) */
#define KERNEL_VIRT_BASE        0xFFFFFFFF80000000ULL
#define KERNEL_PHYS_MAP_BASE    0xFFFF888000000000ULL
#define KERNEL_HEAP_BASE        0xFFFF800000000000ULL
#define USER_STACK_TOP          0x00007FFFFFFFE000ULL
#define USER_HEAP_BASE          0x0000000010000000ULL
#define USER_SPACE_END          0x0000800000000000ULL

/* Convert between physical and virtual addresses */
#define PHYS_TO_VIRT(addr)      ((void *)((uint64_t)(addr) + KERNEL_PHYS_MAP_BASE))
#define VIRT_TO_PHYS(addr)      ((uint64_t)(addr) - KERNEL_PHYS_MAP_BASE)

/* Page frame number conversions */
#define PFN_TO_PHYS(pfn)        ((uint64_t)(pfn) << PAGE_SHIFT)
#define PHYS_TO_PFN(phys)       ((uint64_t)(phys) >> PAGE_SHIFT)

/* Printk log levels */
#define KERN_EMERG      "<0>"   /* System is unusable */
#define KERN_ALERT      "<1>"   /* Action must be taken immediately */
#define KERN_CRIT       "<2>"   /* Critical conditions */
#define KERN_ERR        "<3>"   /* Error conditions */
#define KERN_WARNING    "<4>"   /* Warning conditions */
#define KERN_NOTICE     "<5>"   /* Normal but significant condition */
#define KERN_INFO       "<6>"   /* Informational */
#define KERN_DEBUG      "<7>"   /* Debug-level messages */

/* Printk function */
int printk(const char *fmt, ...);
int vprintk(const char *fmt, va_list args);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list args);
void console_putc(char c);
void console_write(const char *buf, size_t len);
void console_fb_init(void);
void console_poll(void);
int devfs_console_getc_nonblock(void);
int devfs_console_peekc_nonblock(void);
char devfs_console_getc(void);
void devfs_console_flush_input(void);

/* Panic function */
__noreturn void panic(const char *fmt, ...);

/* Bug/warning macros */
#define BUG()           do { panic("BUG at %s:%d", __FILE__, __LINE__); } while(0)
#define BUG_ON(cond)    do { if (unlikely(cond)) BUG(); } while(0)
#define WARN_ON(cond)   ({                                              \
int __ret = !!(cond);                                               \
if (unlikely(__ret))                                                \
    printk(KERN_WARNING "WARNING at %s:%d\n", __FILE__, __LINE__);  \
    __ret;                                                              \
})

/* Assertion */
#define ASSERT(cond)    BUG_ON(!(cond))

/* Build-time assertion */
#define BUILD_BUG_ON(cond)      _Static_assert(!(cond), "Build-time assertion failed")

/* Time functions */
uint64_t get_ticks(void);
uint64_t get_time_ns(void);
void delay_ms(uint64_t ms);

/* Memory functions (from mm/) */
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void *kcalloc(size_t n, size_t size);
void *krealloc(void *ptr, size_t size);
void kfree(void *ptr);

/* String functions (from lib/) */
size_t strlen(const char *s);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcat(char *dest, const char *src);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strdup(const char *s);
char *strtok(char *str, const char *delim);

void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
void *memchr(const void *s, int c, size_t n);

/* Initialization functions */
void gdt_init(void);
void idt_init(void);
void pmm_init(uint64_t multiboot_info);
void vmm_init(void);
void kmalloc_init(void);
void process_init(void);
void scheduler_init(void);
void vfs_init(void);
void axiomfs_init(void);
void devfs_init(void);
void ipc_init(void);
void sysctl_init(void);
int net_init(void);
void net_tick(void);
bool net_is_ready(void);
void pci_init(void);
void virtio_net_init(void);
void e1000_init(void);
unsigned long devfs_input_kbd_drop_count(void);
unsigned long devfs_input_mouse_drop_count(void);
unsigned long devfs_input_mice_drop_count(void);
void irq_enable(uint8_t irq);
void irq_disable(uint8_t irq);
int irq_register_handler(uint8_t irq, irq_handler_fn_t fn, void *ctx);
int uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);
char uart_getc(void);
int uart_getc_nonblock(void);

#endif /* _OBELISK_KERNEL_H */
