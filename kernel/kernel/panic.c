/*
 * Obelisk OS - Kernel Panic Handler
 * From Axioms, Order.
 *
 * Handles unrecoverable kernel errors.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <arch/cpu.h>
#include <arch/regs.h>
#include <stdarg.h>

/* Panic state */
static bool panic_in_progress = false;

/* Stack trace */
struct stack_frame {
    struct stack_frame *rbp;
    uint64_t rip;
};

/* Print stack trace */
static void print_stack_trace(void) {
    struct stack_frame *frame;
    int depth = 0;
    const int max_depth = 20;
    
    /* Get current frame pointer */
    __asm__ volatile("movq %%rbp, %0" : "=r"(frame));
    
    printk(KERN_EMERG "Stack trace:\n");
    
    while (frame && depth < max_depth) {
        /* Validate frame pointer */
        if ((uint64_t)frame < KERNEL_VIRT_BASE) {
            break;
        }
        
        printk(KERN_EMERG "  #%d: 0x%016lx\n", depth, frame->rip);
        
        frame = frame->rbp;
        depth++;
    }
    
    if (depth == 0) {
        printk(KERN_EMERG "  (no stack trace available)\n");
    }
}

/* Print CPU state */
static void print_cpu_state(void) {
    uint64_t cr0, cr2, cr3, cr4;
    
    cr0 = read_cr0();
    cr2 = read_cr2();
    cr3 = read_cr3();
    cr4 = read_cr4();
    
    printk(KERN_EMERG "CPU state:\n");
    printk(KERN_EMERG "  CR0: 0x%016lx  CR2: 0x%016lx\n", cr0, cr2);
    printk(KERN_EMERG "  CR3: 0x%016lx  CR4: 0x%016lx\n", cr3, cr4);
    printk(KERN_EMERG "  RFLAGS: 0x%016lx\n", read_flags());
}

/* Halt all CPUs */
static void __noreturn halt_system(void) {
    /* Disable interrupts */
    cli();
    
    /* TODO: Send IPI to halt other CPUs in SMP */
    
    /* Halt this CPU */
    while (1) {
        hlt();
    }
}

/*
 * panic - Halt the system with an error message
 * @fmt: Printf-style format string
 * @...: Format arguments
 *
 * This function never returns.
 */
void __noreturn panic(const char *fmt, ...) {
    va_list args;
    
    /* Prevent recursive panics */
    if (panic_in_progress) {
        printk(KERN_EMERG "PANIC: Recursive panic detected!\n");
        halt_system();
    }
    
    panic_in_progress = true;
    
    /* Disable interrupts immediately */
    cli();
    
    /* Print panic header */
    printk(KERN_EMERG "\n");
    printk(KERN_EMERG "========================================\n");
    printk(KERN_EMERG "        KERNEL PANIC - NOT SYNCING      \n");
    printk(KERN_EMERG "========================================\n");
    printk(KERN_EMERG "\n");
    
    /* Print panic message */
    printk(KERN_EMERG "PANIC: ");
    va_start(args, fmt);
    vprintk(fmt, args);
    va_end(args);
    printk("\n\n");
    
    /* Print diagnostic information */
    print_cpu_state();
    printk(KERN_EMERG "\n");
    print_stack_trace();
    
    /* Print footer */
    printk(KERN_EMERG "\n");
    printk(KERN_EMERG "========================================\n");
    printk(KERN_EMERG "System halted. Please reboot manually.\n");
    printk(KERN_EMERG "========================================\n");
    
    /* Halt the system */
    halt_system();
}

/*
 * oops - Non-fatal kernel error
 * @fmt: Printf-style format string
 * @...: Format arguments
 *
 * Reports a serious error but attempts to continue.
 */
void oops(const char *fmt, ...) {
    va_list args;
    
    printk(KERN_ERR "\n");
    printk(KERN_ERR "========================================\n");
    printk(KERN_ERR "              KERNEL OOPS               \n");
    printk(KERN_ERR "========================================\n");
    printk(KERN_ERR "\n");
    
    printk(KERN_ERR "OOPS: ");
    va_start(args, fmt);
    vprintk(fmt, args);
    va_end(args);
    printk("\n\n");
    
    print_stack_trace();
    
    printk(KERN_ERR "\n");
    printk(KERN_ERR "========================================\n");
    printk(KERN_ERR "Attempting to continue...\n");
    printk(KERN_ERR "========================================\n\n");
    
    /* TODO: Mark system as tainted */
}

/*
 * warn_slowpath - Print warning message
 * @file: Source file name
 * @line: Line number
 * @fmt: Printf-style format string
 * @...: Format arguments
 */
void warn_slowpath(const char *file, int line, const char *fmt, ...) {
    va_list args;
    
    printk(KERN_WARNING "WARNING: at %s:%d\n", file, line);
    
    if (fmt) {
        printk(KERN_WARNING "  ");
        va_start(args, fmt);
        vprintk(fmt, args);
        va_end(args);
        printk("\n");
    }
}