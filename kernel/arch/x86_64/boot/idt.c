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

extern int64_t syscall_dispatch(uint64_t syscall_num, struct cpu_regs *regs);

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
#define PF_VECTOR           14
#define IRQ_BASE            32
#define IRQ_COUNT           16

/* Minimal auxv constants for targeted loader diagnostics. */
#define AT_NULL         0
#define AT_PHDR         3
#define AT_PHENT        4
#define AT_PHNUM        5
#define AT_PAGESZ       6
#define AT_BASE         7
#define AT_ENTRY        9
#define AT_UID          11
#define AT_EUID         12
#define AT_GID          13
#define AT_EGID         14
#define AT_RANDOM       25
#define AT_HWCAP        16
#define AT_HWCAP2       26
#define AT_SECURE       23

/* ELF dynamic tags (subset) — for interpreting faulting %rdi as glibc struct link_map *. */
#define DYN_DT_NULL       0
#define DYN_DT_HASH       4
#define DYN_DT_STRTAB     5
#define DYN_DT_SYMTAB     6
#define DYN_DT_GNU_HASH   0x6ffffef5ULL

/* glibc struct link_map: l_info[] starts at offset 0x40 (see glibc include/link.h). */
#define LINK_MAP_L_INFO_OFF 0x40ULL

/* Kernel ET_DYN load bias for main PIE (must match kernel/proc/exec.c ET_DYN_MAIN_BIAS). */
#define KERNEL_MAIN_PIE_BIAS 0x0000555555554000ULL

static struct idt_entry idt[IDT_ENTRIES] __aligned(16);
static struct idt_ptr idt_desc;
static irq_handler_fn_t irq_handlers[IRQ_COUNT];
static void *irq_handler_ctx[IRQ_COUNT];

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

static void *const isr_stubs[32] = {
    isr_stub_0, isr_stub_1, isr_stub_2, isr_stub_3,
    isr_stub_4, isr_stub_5, isr_stub_6, isr_stub_7,
    isr_stub_8, isr_stub_9, isr_stub_10, isr_stub_11,
    isr_stub_12, isr_stub_13, isr_stub_14, isr_stub_15,
    isr_stub_16, isr_stub_17, isr_stub_18, isr_stub_19,
    isr_stub_20, isr_stub_21, isr_stub_22, isr_stub_23,
    isr_stub_24, isr_stub_25, isr_stub_26, isr_stub_27,
    isr_stub_28, isr_stub_29, isr_stub_30, isr_stub_31
};

static void *const irq_stubs[IRQ_COUNT] = {
    irq_stub_0, irq_stub_1, irq_stub_2, irq_stub_3,
    irq_stub_4, irq_stub_5, irq_stub_6, irq_stub_7,
    irq_stub_8, irq_stub_9, irq_stub_10, irq_stub_11,
    irq_stub_12, irq_stub_13, irq_stub_14, irq_stub_15
};

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

static bool valid_exception_vector(uint64_t vector) {
    return vector < 32;
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

static bool diag_read_user_byte(struct process *p, uint64_t uaddr, uint8_t *out) {
    if (!p || !p->mm || !p->mm->pt) {
        return false;
    }
    uint64_t phys = mmu_resolve(p->mm->pt, uaddr);
    if (!phys) {
        return false;
    }
    *out = *(const volatile uint8_t *)PHYS_TO_VIRT(phys);
    return true;
}

static bool diag_read_user_u64(struct process *p, uint64_t uaddr, uint64_t *out) {
    uint64_t v = 0;
    for (size_t i = 0; i < 8; i++) {
        uint8_t b = 0;
        if (!diag_read_user_byte(p, uaddr + i, &b)) {
            return false;
        }
        v |= ((uint64_t)b) << (i * 8);
    }
    *out = v;
    return true;
}

static bool streq(const char *a, const char *b) {
    if (!a || !b) {
        return false;
    }
    for (;;) {
        if (a[0] != b[0]) return false;
        if (a[0] == '\0') return true;
        a++;
        b++;
    }
}

static void maybe_diag_ldso_page_fault(const struct cpu_regs *regs) {
    struct process *p = current;
    if (!regs || !p) {
        return;
    }
    if (regs->vector != PF_VECTOR) {
        return;
    }

    /* Only for the known dynamic startup repros (targeted, not broad). */
    if (!streq(p->comm, "xinit") && !streq(p->comm, "xdm")) {
        return;
    }

    /* Parse auxv from the initial stack pointer recorded at exec-time. */
    uint64_t sp0 = p->mm ? p->mm->start_stack : 0;
    if (!sp0) {
        return;
    }

    uint64_t argc = 0;
    if (!diag_read_user_u64(p, sp0, &argc)) {
        return;
    }

    uint64_t cur = sp0 + 8; /* argv[0] pointer */
    uint64_t ptr = 0;

    /* Skip argv pointers until NULL. */
    for (size_t guard = 0; guard < 256; guard++) {
        if (!diag_read_user_u64(p, cur, &ptr)) return;
        cur += 8;
        if (ptr == 0) break;
    }
    /* Skip envp pointers until NULL. */
    for (size_t guard = 0; guard < 256; guard++) {
        if (!diag_read_user_u64(p, cur, &ptr)) return;
        cur += 8;
        if (ptr == 0) break;
    }

    uint64_t at_base = 0, at_entry = 0, at_phdr = 0, at_phent = 0, at_phnum = 0;
    uint64_t at_pagesz = 0, at_uid = 0, at_euid = 0, at_gid = 0, at_egid = 0;
    uint64_t at_secure = 0, at_hwcap = 0, at_hwcap2 = 0;
    uint64_t at_random = 0;

    /* Auxv is a (type,value) stream ending in AT_NULL. */
    for (size_t guard = 0; guard < 64; guard++) {
        uint64_t type = 0, val = 0;
        if (!diag_read_user_u64(p, cur, &type)) return;
        if (!diag_read_user_u64(p, cur + 8, &val)) return;
        cur += 16;
        if (type == AT_NULL) break;
        switch (type) {
        case AT_BASE:   at_base = val; break;
        case AT_ENTRY:  at_entry = val; break;
        case AT_PHDR:   at_phdr = val; break;
        case AT_PHENT:  at_phent = val; break;
        case AT_PHNUM:  at_phnum = val; break;
        case AT_PAGESZ: at_pagesz = val; break;
        case AT_UID:    at_uid = val; break;
        case AT_EUID:   at_euid = val; break;
        case AT_GID:    at_gid = val; break;
        case AT_EGID:   at_egid = val; break;
        case AT_SECURE: at_secure = val; break;
        case AT_HWCAP:  at_hwcap = val; break;
        case AT_HWCAP2: at_hwcap2 = val; break;
        case AT_RANDOM: at_random = val; break;
        default: break;
        }
    }

    /* If we can locate the interpreter base, only dump when RIP is in it. */
    if (at_base) {
        uint64_t rip = regs->rip;
        if (rip < at_base || rip >= at_base + 0x4000000ULL) {
            return;
        }
    }

    /*
     * ld.so may fault with: mov 0x68(%rdi),%rdx ; mov 0x8(%rdx),%rbx when %rdi is
     * struct link_map * (see glibc include/link.h): offset 0x68 is l_info[DT_STRTAB]
     * (DT_STRTAB == 5), not pthread::rtld_catch. fs_base+0x68 is tcbhead_t::__private_tm[3]
     * (glibc sysdeps/x86_64/nptl/tls.h), unrelated.
     */
    uint64_t rdi = regs->rdi;

    uint64_t tls_self = 0, tls_tcb = 0;
    if (p->fs_base) {
        /* x86-64 glibc: tcbhead_t at fs base — tcb @0, dtv @8, self @0x10 (THREAD_SELF). */
        diag_read_user_u64(p, p->fs_base + 0x10, &tls_self);
        diag_read_user_u64(p, p->fs_base + 0x0, &tls_tcb);
    }

    /* +0x68 in tcbhead_t is __private_tm[3] (TM ABI reservation), not rtld_catch. */
    uint64_t tls_private_tm3 = 0;
    if (p->fs_base) {
        diag_read_user_u64(p, p->fs_base + 0x68, &tls_private_tm3);
    }

    uint64_t at_rdi_0 = 0, at_rdi_8 = 0, at_rdi_10 = 0, at_rdi_18 = 0, at_rdi_20 = 0, at_rdi_28 = 0;
    uint64_t at_rdi_60 = 0;
    diag_read_user_u64(p, rdi + 0x0, &at_rdi_0);
    diag_read_user_u64(p, rdi + 0x8, &at_rdi_8);
    diag_read_user_u64(p, rdi + 0x10, &at_rdi_10);
    diag_read_user_u64(p, rdi + 0x18, &at_rdi_18);
    diag_read_user_u64(p, rdi + 0x20, &at_rdi_20);
    diag_read_user_u64(p, rdi + 0x28, &at_rdi_28);
    diag_read_user_u64(p, rdi + 0x60, &at_rdi_60);

    /* l_info[DT_*] at 8-byte slots indexed by tag for small tags (glibc link_map). */
    uint64_t li0 = 0, li1 = 0, li2 = 0, li3 = 0, li4 = 0, li5 = 0, li6 = 0, li7 = 0;
    diag_read_user_u64(p, rdi + LINK_MAP_L_INFO_OFF + 0 * 8, &li0);
    diag_read_user_u64(p, rdi + LINK_MAP_L_INFO_OFF + 1 * 8, &li1);
    diag_read_user_u64(p, rdi + LINK_MAP_L_INFO_OFF + 2 * 8, &li2);
    diag_read_user_u64(p, rdi + LINK_MAP_L_INFO_OFF + 3 * 8, &li3);
    diag_read_user_u64(p, rdi + LINK_MAP_L_INFO_OFF + 4 * 8, &li4);
    diag_read_user_u64(p, rdi + LINK_MAP_L_INFO_OFF + 5 * 8, &li5);
    diag_read_user_u64(p, rdi + LINK_MAP_L_INFO_OFF + 6 * 8, &li6);
    diag_read_user_u64(p, rdi + LINK_MAP_L_INFO_OFF + 7 * 8, &li7);

    uint64_t l_ld = at_rdi_10;
    bool dyn_has_strtab = false, dyn_has_symtab = false, dyn_has_gnu_hash = false;
    uint64_t dyn_d_strtab = 0, dyn_d_symtab = 0;
    size_t dyn_nz = 0;

    if (l_ld) {
        for (size_t i = 0; i < 48; i++) {
            uint64_t tag = 0, val = 0;
            uint64_t ent = l_ld + (uint64_t)i * 16;
            if (!diag_read_user_u64(p, ent, &tag)) {
                break;
            }
            if (!diag_read_user_u64(p, ent + 8, &val)) {
                break;
            }
            if (tag == DYN_DT_NULL) {
                dyn_nz = i;
                break;
            }
            if (tag == DYN_DT_STRTAB) {
                dyn_has_strtab = true;
                dyn_d_strtab = val;
            } else if (tag == DYN_DT_SYMTAB) {
                dyn_has_symtab = true;
                dyn_d_symtab = val;
            } else if (tag == DYN_DT_GNU_HASH) {
                dyn_has_gnu_hash = true;
            }
        }
    }

    printk(KERN_ERR "\n=== LOADER DIAG (link_map vs .dynamic) ===\n");
    printk(KERN_ERR "comm=%s exec=%s\n", p->comm, p->exec_path);
    printk(KERN_ERR "fault rip=0x%lx cr2=0x%lx fs_base=0x%lx\n",
           regs->rip, read_cr2(), p->fs_base);
    printk(KERN_ERR "link_map@%lx: l_addr=0x%lx l_name=0x%lx l_ld=0x%lx l_next=0x%lx l_prev=0x%lx l_real=0x%lx\n",
           rdi, at_rdi_0, at_rdi_8, at_rdi_10, at_rdi_18, at_rdi_20, at_rdi_28);
    printk(KERN_ERR "l_info[0..7] @+0x40..0x77: [0]=0x%lx [1]=0x%lx [2]=0x%lx [3]=0x%lx [4]=0x%lx [5]=0x%lx [6]=0x%lx [7]=0x%lx\n",
           li0, li1, li2, li3, li4, li5, li6, li7);
    printk(KERN_ERR "l_info[4] duplicate read +0x60 -> 0x%lx (should match [4] above)\n", at_rdi_60);
    printk(KERN_ERR "l_info[DT_STRTAB] slot +0x68 -> 0x%lx  l_info[DT_SYMTAB] slot +0x70 -> 0x%lx\n", li5, li6);
    printk(KERN_ERR "auxv: AT_BASE(ld.so)=0x%lx AT_ENTRY(main)=0x%lx AT_PHDR=0x%lx AT_PHENT=0x%lx AT_PHNUM=0x%lx\n",
           at_base, at_entry, at_phdr, at_phent, at_phnum);
    printk(KERN_ERR "auxv: AT_PAGESZ=0x%lx AT_SECURE=0x%lx uid/euid=%lu/%lu gid/egid=%lu/%lu AT_HWCAP=0x%lx AT_HWCAP2=0x%lx AT_RANDOM=0x%lx\n",
           at_pagesz, at_secure, at_uid, at_euid, at_gid, at_egid, at_hwcap, at_hwcap2, at_random);
    if (at_entry >= at_rdi_0) {
        printk(KERN_ERR "main map check: AT_ENTRY-l_addr=0x%lx (expect PIE e_entry e.g. 0x2140 for xinit)\n",
               at_entry - at_rdi_0);
    }
    printk(KERN_ERR "main map check: l_addr==KERNEL_MAIN_PIE_BIAS(0x%lx) -> %s\n",
           (unsigned long)KERNEL_MAIN_PIE_BIAS, (at_rdi_0 == KERNEL_MAIN_PIE_BIAS) ? "yes" : "no");
    printk(KERN_ERR "main map check: l_ld in main object high VA [l_addr..l_addr+8MiB) -> %s\n",
           (at_rdi_0 && l_ld >= at_rdi_0 && l_ld < at_rdi_0 + (8ULL << 20)) ? "yes" : "no");
    printk(KERN_ERR "in-memory .dynamic@%lx: DT_STRTAB present=%d d_ptr=0x%lx  DT_SYMTAB present=%d d_ptr=0x%lx  GNU_HASH=%d  nz~%lu\n",
           l_ld, dyn_has_strtab ? 1 : 0, dyn_d_strtab, dyn_has_symtab ? 1 : 0, dyn_d_symtab,
           dyn_has_gnu_hash ? 1 : 0, (unsigned long)dyn_nz);
    if (at_rdi_0 && dyn_d_strtab && dyn_d_strtab < (1ULL << 24)) {
        printk(KERN_ERR "dyn DT_STRTAB d_ptr looks like file-relative; l_addr+d_ptr=0x%lx\n",
               at_rdi_0 + dyn_d_strtab);
    }
    if (at_rdi_0 && dyn_d_symtab && dyn_d_symtab < (1ULL << 24)) {
        printk(KERN_ERR "dyn DT_SYMTAB d_ptr looks like file-relative; l_addr+d_ptr=0x%lx\n",
               at_rdi_0 + dyn_d_symtab);
    }

    /* First 14 dynamic entries for context (tag, val). */
    if (l_ld) {
        for (size_t i = 0; i < 14; i++) {
            uint64_t tag = 0, val = 0;
            uint64_t ent = l_ld + (uint64_t)i * 16;
            if (!diag_read_user_u64(p, ent, &tag)) {
                break;
            }
            if (!diag_read_user_u64(p, ent + 8, &val)) {
                break;
            }
            if (tag == DYN_DT_NULL) {
                printk(KERN_ERR "  .dynamic[%lu] DT_NULL\n", (unsigned long)i);
                break;
            }
            printk(KERN_ERR "  .dynamic[%lu] tag=0x%lx val=0x%lx\n", (unsigned long)i, tag, val);
        }
    }

    if (dyn_has_strtab && dyn_d_strtab != 0 && li5 == 0) {
        printk(KERN_ERR "VERDICT: .dynamic has DT_STRTAB d_ptr!=0 but l_info[DT_STRTAB]==NULL -> ld.so has not filled link_map (or wrong map)\n");
    } else if (!dyn_has_strtab || dyn_d_strtab == 0) {
        printk(KERN_ERR "VERDICT: .dynamic missing/zero DT_STRTAB in mapped memory -> suspect kernel mapping/copy of RW segment\n");
    } else if (li5 != 0 && li5 == dyn_d_strtab) {
        printk(KERN_ERR "VERDICT: l_info[5] matches .dynamic DT_STRTAB (unexpected at this fault)\n");
    } else {
        printk(KERN_ERR "VERDICT: inconclusive; compare l_info vs .dynamic above\n");
    }

    printk(KERN_ERR "fs_base: tcb=0x%lx self=0x%lx tcbhead+0x68(__private_tm[3])=0x%lx\n",
           tls_tcb, tls_self, tls_private_tm3);
}

static void log_exception_header(uint64_t vector, uint64_t error_code) {
    if (vector == PF_VECTOR) {
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

    if (!regs_from_user(regs) || regs->vector != PF_VECTOR) {
        return false;
    }
    if (!p || !p->mm) {
        return false;
    }

    fault_addr = read_cr2();
    ret = vmm_handle_page_fault(p->mm, fault_addr, (uint32_t)regs->error_code);
    return ret == 0;
}

static bool try_emulate_user_syscall_ud(struct cpu_regs *regs) {
    uint8_t op0, op1;
    int64_t ret;

    /* When fast SYSCALL is disabled, 'syscall' (0x0f 0x05) traps as #UD.
     * Emulate it through the existing INT 0x80-compatible dispatcher. */
    if (!regs || !regs_from_user(regs) || regs->vector != 6) {
        return false;
    }

    op0 = *(const uint8_t *)(uintptr_t)regs->rip;
    op1 = *(const uint8_t *)(uintptr_t)(regs->rip + 1);
    if (op0 != 0x0f || op1 != 0x05) {
        return false;
    }

    ret = syscall_dispatch(regs->rax, regs);
    regs->rax = (uint64_t)ret;
    regs->rip += 2;  /* Skip emulated syscall instruction. */
    return true;
}

void exception_handler(struct cpu_regs *regs) {
    const uint64_t vector = regs ? regs->vector : (uint64_t)-1;
    bool user_mode;
    static int exception_depth = 0;

    if (!regs) {
        panic("exception_handler: null register frame");
    }
    user_mode = regs_from_user(regs);
    exception_depth++;
    if (exception_depth > 1 && !user_mode) {
        log_exception_header(vector, regs->error_code);
        idt_dump_regs(regs);
        panic("Nested kernel exception");
    }

    /* Fast path: recoverable user-space page fault. */
    if (try_handle_user_page_fault(regs)) {
        exception_depth--;
        return;
    }

    /* Compat path: emulate user-mode 'syscall' while fast path is disabled. */
    if (try_emulate_user_syscall_ud(regs)) {
        exception_depth--;
        return;
    }

    if (!valid_exception_vector(vector)) {
        printk(KERN_ERR "\n=== EXCEPTION: Invalid vector %lu ===\n", vector);
        printk(KERN_ERR "Error code: 0x%lx\n", regs->error_code);
    } else {
        log_exception_header(vector, regs->error_code);
    }
    maybe_diag_ldso_page_fault(regs);
    idt_dump_regs(regs);

    /* Debug/breakpoint exceptions are non-fatal by design. */
    if (vector == 1 || vector == 3) {
        exception_depth--;
        return;
    }

    if (user_mode) {
        struct process *p = current;
        printk(KERN_ERR "User exception in pid=%d comm=%s: vector=%lu, terminating task\n",
               p ? p->pid : -1, p ? p->comm : "?", vector);
        exception_depth--;
        do_exit(128 + (int)vector);
        __builtin_unreachable();
    }

    exception_depth--;
    panic("Unrecoverable kernel exception");
}

void irq_handler(struct cpu_regs *regs) {
    uint64_t irq;
    static uint64_t irq0_count = 0;
    irq_handler_fn_t fn;
    void *ctx;
    if (!regs) {
        panic("irq_handler: null register frame");
    }
    if (regs->vector < IRQ_BASE || regs->vector >= (IRQ_BASE + IRQ_COUNT)) {
        printk(KERN_ERR "Spurious IRQ vector %lu\n", regs->vector);
        outb(0x20, 0x20);
        return;
    }
    irq = regs->vector - IRQ_BASE;
    fn = irq_handlers[irq];
    ctx = irq_handler_ctx[irq];

    if (irq == 0) {
        irq0_count++;
        net_tick();
        if (irq0_count == 1 || (irq0_count % 5000) == 0) {
            printk(KERN_DEBUG "IRQ 0 received (count=%lu)\n", irq0_count);
        }
    } else if (!fn) {
        printk(KERN_DEBUG "IRQ %lu received\n", irq);
    }

    if (fn) {
        fn((uint8_t)irq, regs, ctx);
    }

    if (irq >= 8) {
        outb(0xA0, 0x20);
    }
    outb(0x20, 0x20);
}

int irq_register_handler(uint8_t irq, irq_handler_fn_t fn, void *ctx) {
    if (irq >= IRQ_COUNT || !fn) {
        return -EINVAL;
    }
    irq_handlers[irq] = fn;
    irq_handler_ctx[irq] = ctx;
    return 0;
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
    if (irq >= IRQ_COUNT) {
        return;
    }

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
    if (irq >= IRQ_COUNT) {
        return;
    }

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
    int i;
    printk(KERN_INFO "Initializing IDT...\n");
    memset(idt, 0, sizeof(idt));
    memset(irq_handlers, 0, sizeof(irq_handlers));
    memset(irq_handler_ctx, 0, sizeof(irq_handler_ctx));

    for (i = 0; i < 32; i++) {
        uint8_t gate = (i == 3) ? IDT_USER_INTERRUPT : IDT_INTERRUPT_GATE;
        uint8_t ist = 0;
        if (i == 2) ist = 2;
        if (i == 8) ist = 1;
        idt_set_entry(i, (uint64_t)isr_stubs[i], 0x08, gate, ist);
    }

    for (i = 0; i < IRQ_COUNT; i++) {
        idt_set_entry(IRQ_BASE + i, (uint64_t)irq_stubs[i], 0x08, IDT_INTERRUPT_GATE, 0);
    }

    idt_set_entry(0x80, (uint64_t)syscall_stub, 0x08, IDT_USER_INTERRUPT, 0);

    idt_desc.limit = sizeof(idt) - 1;
    idt_desc.base = (uint64_t)&idt;

    pic_init();
    __asm__ volatile("lidt %0" :: "m"(idt_desc));
    sti();

    printk(KERN_INFO "IDT initialized: %d entries\n", IDT_ENTRIES);
}