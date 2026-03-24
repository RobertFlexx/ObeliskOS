/*
 * Obelisk OS - Global Descriptor Table
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <arch/cpu.h>

/* GDT entry structure (8 bytes) */
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __packed;

/* System segment descriptor (16 bytes for TSS in 64-bit mode) */
struct gdt_system_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __packed;

/* GDT pointer */
struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __packed;

/* Task State Segment (64-bit) */
struct tss {
    uint32_t reserved0;
    uint64_t rsp0;              /* Kernel stack for privilege level 0 */
    uint64_t rsp1;              /* Stack for privilege level 1 */
    uint64_t rsp2;              /* Stack for privilege level 2 */
    uint64_t reserved1;
    uint64_t ist1;              /* Interrupt Stack Table entries */
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;       /* I/O Permission Bitmap offset */
} __packed;

/* GDT entries */
#define GDT_ENTRIES 7   /* null, kcode, kdata, udata, ucode, tss_low, tss_high */

static struct gdt_entry gdt[GDT_ENTRIES] __aligned(16);
static struct gdt_ptr gdt_ptr;
static struct tss tss __aligned(16);

/* Kernel stack for interrupts (allocated later) */
static uint8_t kernel_interrupt_stack[8192] __aligned(16);
static uint8_t double_fault_stack[4096] __aligned(16);
static uint8_t nmi_stack[4096] __aligned(16);

/* Set a regular GDT entry */
static void gdt_set_entry(int index, uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t granularity) {
    gdt[index].base_low = base & 0xFFFF;
    gdt[index].base_mid = (base >> 16) & 0xFF;
    gdt[index].base_high = (base >> 24) & 0xFF;
    
    gdt[index].limit_low = limit & 0xFFFF;
    gdt[index].granularity = ((limit >> 16) & 0x0F) | (granularity & 0xF0);
    
    gdt[index].access = access;
}

/* Set TSS entry (requires 16 bytes in 64-bit mode) */
static void gdt_set_tss(int index, uint64_t base, uint32_t limit) {
    struct gdt_system_entry *entry = (struct gdt_system_entry *)&gdt[index];
    
    entry->limit_low = limit & 0xFFFF;
    entry->base_low = base & 0xFFFF;
    entry->base_mid = (base >> 16) & 0xFF;
    entry->access = 0x89;           /* Present, 64-bit TSS available */
    entry->granularity = ((limit >> 16) & 0x0F) | 0x00;
    entry->base_high = (base >> 24) & 0xFF;
    entry->base_upper = (base >> 32) & 0xFFFFFFFF;
    entry->reserved = 0;
}

/* Initialize TSS */
static void tss_init(void) {
    memset(&tss, 0, sizeof(tss));
    
    /* RSP0: Kernel stack pointer when transitioning from user to kernel mode */
    tss.rsp0 = (uint64_t)&kernel_interrupt_stack[sizeof(kernel_interrupt_stack)];
    
    /* IST1: Used for double fault handler */
    tss.ist1 = (uint64_t)&double_fault_stack[sizeof(double_fault_stack)];
    
    /* IST2: Used for NMI handler */
    tss.ist2 = (uint64_t)&nmi_stack[sizeof(nmi_stack)];
    
    /* I/O permission bitmap offset (set to limit to disable) */
    tss.iopb_offset = sizeof(tss);
}

/* Load GDT */
static inline void gdt_load(void) {
    __asm__ volatile(
        "lgdt %0\n\t"
        "pushq $0x08\n\t"           /* Kernel code segment */
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        "movw $0x10, %%ax\n\t"      /* Kernel data segment */
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        "movw $0x00, %%ax\n\t"      /* Clear FS and GS for now */
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        :
        : "m"(gdt_ptr)
        : "rax", "memory"
    );
}

/* Load TSS */
static inline void tss_load(uint16_t selector) {
    __asm__ volatile("ltr %0" :: "r"(selector));
}

/* Initialize GDT */
void gdt_init(void) {
    printk(KERN_INFO "Initializing GDT...\n");
    
    /* Clear GDT */
    memset(gdt, 0, sizeof(gdt));
    
    /* Entry 0: Null descriptor */
    gdt_set_entry(0, 0, 0, 0, 0);
    
    /* Entry 1: Kernel code segment (selector 0x08) */
    /* Access: Present, DPL=0, Code, Execute/Read */
    /* Granularity: Long mode */
    gdt_set_entry(1, 0, 0, 0x9A, 0x20);
    
    /* Entry 2: Kernel data segment (selector 0x10) */
    /* Access: Present, DPL=0, Data, Read/Write */
    gdt_set_entry(2, 0, 0, 0x92, 0x00);
    
    /* Entry 3: User data segment (selector 0x18 | 3 = 0x1B) */
    /* Access: Present, DPL=3, Data, Read/Write */
    gdt_set_entry(3, 0, 0, 0xF2, 0x00);
    
    /* Entry 4: User code segment (selector 0x20 | 3 = 0x23) */
    /* Access: Present, DPL=3, Code, Execute/Read */
    /* Granularity: Long mode */
    gdt_set_entry(4, 0, 0, 0xFA, 0x20);
    
    /* Initialize TSS */
    tss_init();
    
    /* Entry 5-6: TSS (16 bytes, selector 0x28) */
    gdt_set_tss(5, (uint64_t)&tss, sizeof(tss) - 1);
    
    /* Set up GDT pointer */
    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base = (uint64_t)&gdt;
    
    /* Load GDT */
    gdt_load();
    
    /* Load TSS */
    tss_load(0x28);
    
    printk(KERN_INFO "GDT initialized: %d entries, TSS at %p\n",
           GDT_ENTRIES, &tss);
}

/* Update TSS RSP0 (called during context switch) */
void tss_set_rsp0(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}

/* Get TSS RSP0 */
uint64_t tss_get_rsp0(void) {
    return tss.rsp0;
}