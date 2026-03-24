/*
 * Obelisk OS - Interrupt Descriptor Table
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <arch/cpu.h>
#include <arch/regs.h>
#include <mm/vmm.h>
#include <proc/process.h>

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __packed;

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __packed;

#define IDT_ENTRIES 256

#define IDT_INTERRUPT_GATE  0x8E
#define IDT_USER_INTERRUPT  0xEE

static struct idt_entry idt[IDT_ENTRIES] __aligned(16);
static struct idt_ptr idt_desc;

extern void isr_stub_0(void);
extern void isr_stub_1(void);
extern void isr_stub_2(void);
extern void isr_stub_3(void);
extern void isr_stub_4(void);
extern void isr_stub_5(void);
extern void isr_stub_6(void);
extern void isr_stub_7(void);
extern void isr_stub_8(void);
extern void isr_stub_9(void);
extern void isr_stub_10(void);
extern void isr_stub_11(void);
extern void isr_stub_12(void);
extern void isr_stub_13(void);
extern void isr_stub_14(void);
extern void isr_stub_15(void);
extern void isr_stub_16(void);
extern void isr_stub_17(void);
extern void isr_stub_18(void);
extern void isr_stub_19(void);
extern void isr_stub_20(void);
extern void isr_stub_21(void);
extern void isr_stub_22(void);
extern void isr_stub_23(void);
extern void isr_stub_24(void);
extern void isr_stub_25(void);
extern void isr_stub_26(void);
extern void isr_stub_27(void);
extern void isr_stub_28(void);
extern void isr_stub_29(void);
extern void isr_stub_30(void);
extern void isr_stub_31(void);

extern void irq_stub_0(void);
extern void irq_stub_1(void);
extern void irq_stub_2(void);
extern void irq_stub_3(void);
extern void irq_stub_4(void);
extern void irq_stub_5(void);
extern void irq_stub_6(void);
extern void irq_stub_7(void);
extern void irq_stub_8(void);
extern void irq_stub_9(void);
extern void irq_stub_10(void);
extern void irq_stub_11(void);
extern void irq_stub_12(void);
extern void irq_stub_13(void);
extern void irq_stub_14(void);
extern void irq_stub_15(void);

extern void syscall_stub(void);

static const char *exception_names[] = {
    "Division Error", "Debug", "Non-Maskable Interrupt", "Breakpoint",
    "Overflow", "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS", "Segment Not Present",
    "Stack-Segment Fault", "General Protection Fault", "Page Fault", "Reserved",
    "x87 FPU Error", "Alignment Check", "Machine Check", "SIMD Floating-Point",
    "Virtualization", "Control Protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor Injection", "VMM Communication", "Security Exception", "Reserved"
};

static void idt_set_entry(int index, uint64_t handler, uint16_t selector,
                          uint8_t type_attr, uint8_t ist) {
    idt[index].offset_low = handler & 0xFFFF;
    idt[index].offset_mid = (handler >> 16) & 0xFFFF;
    idt[index].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[index].selector = selector;
    idt[index].ist = ist;
    idt[index].type_attr = type_attr;
    idt[index].zero = 0;
}

static bool regs_from_user(const struct cpu_regs *regs) {
    return (regs->cs & 0x3) == 0x3;
}

static void idt_dump_regs(const struct cpu_regs *regs) {
    printk(KERN_ERR "RAX: 0x%016lx  RBX: 0x%016lx\n", regs->rax, regs->rbx);
    printk(KERN_ERR "RCX: 0x%016lx  RDX: 0x%016lx\n", regs->rcx, regs->rdx);
    printk(KERN_ERR "RSI: 0x%016lx  RDI: 0x%016lx\n", regs->rsi, regs->rdi);
    printk(KERN_ERR "RBP: 0x%016lx  RSP: 0x%016lx\n", regs->rbp, regs->rsp);
    printk(KERN_ERR "R8:  0x%016lx  R9:  0x%016lx\n", regs->r8, regs->r9);
    printk(KERN_ERR "R10: 0x%016lx  R11: 0x%016lx\n", regs->r10, regs->r11);
    printk(KERN_ERR "R12: 0x%016lx  R13: 0x%016lx\n", regs->r12, regs->r13);
    printk(KERN_ERR "R14: 0x%016lx  R15: 0x%016lx\n", regs->r14, regs->r15);
    printk(KERN_ERR "RIP: 0x%016lx  CS:  0x%04lx\n", regs->rip, regs->cs);
    printk(KERN_ERR "RFLAGS: 0x%016lx  SS: 0x%04lx\n", regs->rflags, regs->ss);
}

static void log_exception_header(uint64_t vector, uint64_t error_code) {
    if (vector == 14) {
        uint64_t fault_addr = read_cr2();
        printk(KERN_ERR "\n=== PAGE FAULT ===\n");
        printk(KERN_ERR "Address: 0x%016lx\n", fault_addr);
        printk(KERN_ERR "Error code: 0x%lx", error_code);
        if (error_code & 0x1) printk(" [P]");
        if (error_code & 0x2) printk(" [W]");
        if (error_code & 0x4) printk(" [U]");
        if (error_code & 0x8) printk(" [RSVD]");
        if (error_code & 0x10) printk(" [I]");
        printk("\n");
        return;
    }

    printk(KERN_ERR "\n=== EXCEPTION: %s ===\n",
           vector < 32 ? exception_names[vector] : "Unknown");
    printk(KERN_ERR "Vector: %lu, Error code: 0x%lx\n", vector, error_code);
}

static bool try_handle_user_page_fault(struct cpu_regs *regs) {
    struct process *p = current;
    uint64_t fault_addr;
    int ret;

    if (!regs_from_user(regs) || regs->vector != 14) {
        return false;
    }
    if (!p || !p->mm) {
        return false;
    }

    fault_addr = read_cr2();
    ret = vmm_handle_page_fault(p->mm, fault_addr, (uint32_t)regs->error_code);
    return ret == 0;
}

void exception_handler(struct cpu_regs *regs) {
    const uint64_t vector = regs->vector;
    const bool user_mode = regs_from_user(regs);

    /* Fast path: recoverable user-space page fault. */
    if (try_handle_user_page_fault(regs)) {
        return;
    }

    log_exception_header(vector, regs->error_code);
    idt_dump_regs(regs);

    /* Debug/breakpoint exceptions are non-fatal by design. */
    if (vector == 1 || vector == 3) {
        return;
    }

    if (user_mode) {
        struct process *p = current;
        printk(KERN_ERR "User exception in pid=%d comm=%s: vector=%lu, terminating task\n",
               p ? p->pid : -1, p ? p->comm : "?", vector);
        do_exit(128 + (int)vector);
        __builtin_unreachable();
    }

    panic("Unrecoverable kernel exception");
}

void irq_handler(struct cpu_regs *regs) {
    uint64_t irq = regs->vector - 32;
    static uint64_t irq0_count = 0;

    if (irq == 0) {
        irq0_count++;
        if (irq0_count == 1 || (irq0_count % 5000) == 0) {
            printk(KERN_DEBUG "IRQ 0 received (count=%lu)\n", irq0_count);
        }
    } else {
        printk(KERN_DEBUG "IRQ %lu received\n", irq);
    }

    if (irq >= 8) {
        outb(0xA0, 0x20);
    }
    outb(0x20, 0x20);
}

static void pic_init(void) {
    outb(0x20, 0x11);
    outb(0xA0, 0x11);

    outb(0x21, 0x20);
    outb(0xA1, 0x28);

    outb(0x21, 0x04);
    outb(0xA1, 0x02);

    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
}

void irq_enable(uint8_t irq) {
    uint16_t port;
    uint8_t mask;

    if (irq < 8) {
        port = 0x21;
    } else {
        port = 0xA1;
        irq -= 8;
    }

    mask = inb(port) & ~(1 << irq);
    outb(port, mask);
}

void irq_disable(uint8_t irq) {
    uint16_t port;
    uint8_t mask;

    if (irq < 8) {
        port = 0x21;
    } else {
        port = 0xA1;
        irq -= 8;
    }

    mask = inb(port) | (1 << irq);
    outb(port, mask);
}

void idt_init(void) {
    printk(KERN_INFO "Initializing IDT...\n");
    memset(idt, 0, sizeof(idt));

    idt_set_entry(0, (uint64_t)isr_stub_0, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(1, (uint64_t)isr_stub_1, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(2, (uint64_t)isr_stub_2, 0x08, IDT_INTERRUPT_GATE, 2);
    idt_set_entry(3, (uint64_t)isr_stub_3, 0x08, IDT_USER_INTERRUPT, 0);
    idt_set_entry(4, (uint64_t)isr_stub_4, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(5, (uint64_t)isr_stub_5, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(6, (uint64_t)isr_stub_6, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(7, (uint64_t)isr_stub_7, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(8, (uint64_t)isr_stub_8, 0x08, IDT_INTERRUPT_GATE, 1);
    idt_set_entry(9, (uint64_t)isr_stub_9, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(10, (uint64_t)isr_stub_10, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(11, (uint64_t)isr_stub_11, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(12, (uint64_t)isr_stub_12, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(13, (uint64_t)isr_stub_13, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(14, (uint64_t)isr_stub_14, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(15, (uint64_t)isr_stub_15, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(16, (uint64_t)isr_stub_16, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(17, (uint64_t)isr_stub_17, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(18, (uint64_t)isr_stub_18, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(19, (uint64_t)isr_stub_19, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(20, (uint64_t)isr_stub_20, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(21, (uint64_t)isr_stub_21, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(22, (uint64_t)isr_stub_22, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(23, (uint64_t)isr_stub_23, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(24, (uint64_t)isr_stub_24, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(25, (uint64_t)isr_stub_25, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(26, (uint64_t)isr_stub_26, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(27, (uint64_t)isr_stub_27, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(28, (uint64_t)isr_stub_28, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(29, (uint64_t)isr_stub_29, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(30, (uint64_t)isr_stub_30, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(31, (uint64_t)isr_stub_31, 0x08, IDT_INTERRUPT_GATE, 0);

    idt_set_entry(32, (uint64_t)irq_stub_0, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(33, (uint64_t)irq_stub_1, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(34, (uint64_t)irq_stub_2, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(35, (uint64_t)irq_stub_3, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(36, (uint64_t)irq_stub_4, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(37, (uint64_t)irq_stub_5, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(38, (uint64_t)irq_stub_6, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(39, (uint64_t)irq_stub_7, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(40, (uint64_t)irq_stub_8, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(41, (uint64_t)irq_stub_9, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(42, (uint64_t)irq_stub_10, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(43, (uint64_t)irq_stub_11, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(44, (uint64_t)irq_stub_12, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(45, (uint64_t)irq_stub_13, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(46, (uint64_t)irq_stub_14, 0x08, IDT_INTERRUPT_GATE, 0);
    idt_set_entry(47, (uint64_t)irq_stub_15, 0x08, IDT_INTERRUPT_GATE, 0);

    idt_set_entry(0x80, (uint64_t)syscall_stub, 0x08, IDT_USER_INTERRUPT, 0);

    idt_desc.limit = sizeof(idt) - 1;
    idt_desc.base = (uint64_t)&idt;

    pic_init();
    __asm__ volatile("lidt %0" :: "m"(idt_desc));
    sti();

    printk(KERN_INFO "IDT initialized: %d entries\n", IDT_ENTRIES);
}