/*
 * Obelisk OS - x86_64 CPU Definitions
 * From Axioms, Order.
 */

#ifndef _ARCH_CPU_H
#define _ARCH_CPU_H

#include <obelisk/types.h>

/* CPU feature flags (CPUID) */
#define CPU_FEATURE_FPU         BIT(0)
#define CPU_FEATURE_VME         BIT(1)
#define CPU_FEATURE_DE          BIT(2)
#define CPU_FEATURE_PSE         BIT(3)
#define CPU_FEATURE_TSC         BIT(4)
#define CPU_FEATURE_MSR         BIT(5)
#define CPU_FEATURE_PAE         BIT(6)
#define CPU_FEATURE_MCE         BIT(7)
#define CPU_FEATURE_CX8         BIT(8)
#define CPU_FEATURE_APIC        BIT(9)
#define CPU_FEATURE_SEP         BIT(11)
#define CPU_FEATURE_MTRR        BIT(12)
#define CPU_FEATURE_PGE         BIT(13)
#define CPU_FEATURE_MCA         BIT(14)
#define CPU_FEATURE_CMOV        BIT(15)
#define CPU_FEATURE_PAT         BIT(16)
#define CPU_FEATURE_PSE36       BIT(17)
#define CPU_FEATURE_PSN         BIT(18)
#define CPU_FEATURE_CLFLUSH     BIT(19)
#define CPU_FEATURE_DS          BIT(21)
#define CPU_FEATURE_ACPI        BIT(22)
#define CPU_FEATURE_MMX         BIT(23)
#define CPU_FEATURE_FXSR        BIT(24)
#define CPU_FEATURE_SSE         BIT(25)
#define CPU_FEATURE_SSE2        BIT(26)
#define CPU_FEATURE_SS          BIT(27)
#define CPU_FEATURE_HTT         BIT(28)
#define CPU_FEATURE_TM          BIT(29)
#define CPU_FEATURE_PBE         BIT(31)

/* Extended features (CPUID EAX=7) */
#define CPU_FEATURE_FSGSBASE    BIT(0)
#define CPU_FEATURE_TSC_ADJUST  BIT(1)
#define CPU_FEATURE_SGX         BIT(2)
#define CPU_FEATURE_BMI1        BIT(3)
#define CPU_FEATURE_HLE         BIT(4)
#define CPU_FEATURE_AVX2        BIT(5)
#define CPU_FEATURE_SMEP        BIT(7)
#define CPU_FEATURE_BMI2        BIT(8)
#define CPU_FEATURE_ERMS        BIT(9)
#define CPU_FEATURE_INVPCID     BIT(10)
#define CPU_FEATURE_RTM         BIT(11)
#define CPU_FEATURE_MPX         BIT(14)
#define CPU_FEATURE_AVX512F     BIT(16)
#define CPU_FEATURE_RDSEED      BIT(18)
#define CPU_FEATURE_ADX         BIT(19)
#define CPU_FEATURE_SMAP        BIT(20)
#define CPU_FEATURE_CLFLUSHOPT  BIT(23)
#define CPU_FEATURE_SHA         BIT(29)

/* Control registers */
#define CR0_PE          BIT(0)      /* Protected mode enable */
#define CR0_MP          BIT(1)      /* Monitor coprocessor */
#define CR0_EM          BIT(2)      /* Emulation */
#define CR0_TS          BIT(3)      /* Task switched */
#define CR0_ET          BIT(4)      /* Extension type */
#define CR0_NE          BIT(5)      /* Numeric error */
#define CR0_WP          BIT(16)     /* Write protect */
#define CR0_AM          BIT(18)     /* Alignment mask */
#define CR0_NW          BIT(29)     /* Not write-through */
#define CR0_CD          BIT(30)     /* Cache disable */
#define CR0_PG          BIT(31)     /* Paging enable */

#define CR4_VME         BIT(0)      /* Virtual 8086 mode extensions */
#define CR4_PVI         BIT(1)      /* Protected mode virtual interrupts */
#define CR4_TSD         BIT(2)      /* Time stamp disable */
#define CR4_DE          BIT(3)      /* Debugging extensions */
#define CR4_PSE         BIT(4)      /* Page size extension */
#define CR4_PAE         BIT(5)      /* Physical address extension */
#define CR4_MCE         BIT(6)      /* Machine check enable */
#define CR4_PGE         BIT(7)      /* Page global enable */
#define CR4_PCE         BIT(8)      /* Performance monitoring counter enable */
#define CR4_OSFXSR      BIT(9)      /* OS support for FXSAVE/FXRSTOR */
#define CR4_OSXMMEXCPT  BIT(10)     /* OS support for unmasked SIMD exceptions */
#define CR4_UMIP        BIT(11)     /* User-mode instruction prevention */
#define CR4_VMXE        BIT(13)     /* VMX enable */
#define CR4_SMXE        BIT(14)     /* SMX enable */
#define CR4_FSGSBASE    BIT(16)     /* FS/GS base enable */
#define CR4_PCIDE       BIT(17)     /* PCID enable */
#define CR4_OSXSAVE     BIT(18)     /* XSAVE enable */
#define CR4_SMEP        BIT(20)     /* SMEP enable */
#define CR4_SMAP        BIT(21)     /* SMAP enable */
#define CR4_PKE         BIT(22)     /* Protection keys enable */

/* EFLAGS register */
#define EFLAGS_CF       BIT(0)      /* Carry flag */
#define EFLAGS_PF       BIT(2)      /* Parity flag */
#define EFLAGS_AF       BIT(4)      /* Auxiliary carry flag */
#define EFLAGS_ZF       BIT(6)      /* Zero flag */
#define EFLAGS_SF       BIT(7)      /* Sign flag */
#define EFLAGS_TF       BIT(8)      /* Trap flag */
#define EFLAGS_IF       BIT(9)      /* Interrupt enable flag */
#define EFLAGS_DF       BIT(10)     /* Direction flag */
#define EFLAGS_OF       BIT(11)     /* Overflow flag */
#define EFLAGS_IOPL     (BIT(12) | BIT(13)) /* I/O privilege level */
#define EFLAGS_NT       BIT(14)     /* Nested task */
#define EFLAGS_RF       BIT(16)     /* Resume flag */
#define EFLAGS_VM       BIT(17)     /* Virtual 8086 mode */
#define EFLAGS_AC       BIT(18)     /* Alignment check */
#define EFLAGS_VIF      BIT(19)     /* Virtual interrupt flag */
#define EFLAGS_VIP      BIT(20)     /* Virtual interrupt pending */
#define EFLAGS_ID       BIT(21)     /* ID flag */

/* Model Specific Registers */
#define MSR_EFER            0xC0000080
#define MSR_STAR            0xC0000081
#define MSR_LSTAR           0xC0000082
#define MSR_CSTAR           0xC0000083
#define MSR_SFMASK          0xC0000084
#define MSR_FS_BASE         0xC0000100
#define MSR_GS_BASE         0xC0000101
#define MSR_KERNEL_GS_BASE  0xC0000102

#define MSR_EFER_SCE        BIT(0)      /* Syscall enable */
#define MSR_EFER_LME        BIT(8)      /* Long mode enable */
#define MSR_EFER_LMA        BIT(10)     /* Long mode active */
#define MSR_EFER_NXE        BIT(11)     /* No-execute enable */

/* GDT segment selectors */
#define GDT_NULL            0x00
#define GDT_KERNEL_CODE     0x08
#define GDT_KERNEL_DATA     0x10
#define GDT_USER_DATA       0x18
#define GDT_USER_CODE       0x20
#define GDT_TSS             0x28

/* RPL (Ring Privilege Level) */
#define RPL_KERNEL          0
#define RPL_USER            3

/* CPU information structure */
struct cpu_info {
    uint32_t id;                    /* CPU ID */
    uint32_t apic_id;               /* APIC ID */
    uint64_t features;              /* CPUID features */
    uint64_t ext_features;          /* Extended features */
    char vendor[16];                /* Vendor string */
    char brand[64];                 /* Brand string */
    uint32_t family;                /* CPU family */
    uint32_t model;                 /* CPU model */
    uint32_t stepping;              /* CPU stepping */
    uint64_t tsc_frequency;         /* TSC frequency in Hz */
    void *kernel_stack;             /* Per-CPU kernel stack */
    struct process *current_proc;   /* Current process */
};

/* Inline assembly helpers */
static __always_inline void cli(void) {
    __asm__ volatile("cli" ::: "memory");
}

static __always_inline void sti(void) {
    __asm__ volatile("sti" ::: "memory");
}

static __always_inline void hlt(void) {
    __asm__ volatile("hlt");
}

static __always_inline void pause(void) {
    __asm__ volatile("pause");
}

static __always_inline void nop(void) {
    __asm__ volatile("nop");
}

static __always_inline uint64_t read_flags(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0" : "=r"(flags));
    return flags;
}

static __always_inline void write_flags(uint64_t flags) {
    __asm__ volatile("pushq %0; popfq" :: "r"(flags) : "memory");
}

static __always_inline bool interrupts_enabled(void) {
    return (read_flags() & EFLAGS_IF) != 0;
}

static __always_inline uint64_t read_cr0(void) {
    uint64_t val;
    __asm__ volatile("movq %%cr0, %0" : "=r"(val));
    return val;
}

static __always_inline void write_cr0(uint64_t val) {
    __asm__ volatile("movq %0, %%cr0" :: "r"(val) : "memory");
}

static __always_inline uint64_t read_cr2(void) {
    uint64_t val;
    __asm__ volatile("movq %%cr2, %0" : "=r"(val));
    return val;
}

static __always_inline uint64_t read_cr3(void) {
    uint64_t val;
    __asm__ volatile("movq %%cr3, %0" : "=r"(val));
    return val;
}

static __always_inline void write_cr3(uint64_t val) {
    __asm__ volatile("movq %0, %%cr3" :: "r"(val) : "memory");
}

static __always_inline uint64_t read_cr4(void) {
    uint64_t val;
    __asm__ volatile("movq %%cr4, %0" : "=r"(val));
    return val;
}

static __always_inline void write_cr4(uint64_t val) {
    __asm__ volatile("movq %0, %%cr4" :: "r"(val) : "memory");
}

static __always_inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static __always_inline void wrmsr(uint32_t msr, uint64_t val) {
    uint32_t low = val & 0xFFFFFFFF;
    uint32_t high = val >> 32;
    __asm__ volatile("wrmsr" :: "c"(msr), "a"(low), "d"(high) : "memory");
}

static __always_inline uint64_t rdtsc(void) {
    uint32_t low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

static __always_inline void invlpg(void *addr) {
    __asm__ volatile("invlpg (%0)" :: "r"(addr) : "memory");
}

static __always_inline void cpuid(uint32_t leaf, uint32_t subleaf,
                                  uint32_t *eax, uint32_t *ebx,
                                  uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "a"(leaf), "c"(subleaf));
}

static __always_inline void swapgs(void) {
    __asm__ volatile("swapgs" ::: "memory");
}

/* CPUID helpers */
void cpu_detect_features(struct cpu_info *cpu);
bool cpu_has_feature(uint64_t feature);
void cpu_print_info(struct cpu_info *cpu);

/* Initialization */
void cpu_init(void);
struct cpu_info *cpu_get_current(void);

/* Port I/O helpers */
void outb(uint16_t port, uint8_t value);
uint8_t inb(uint16_t port);
void outw(uint16_t port, uint16_t value);
uint16_t inw(uint16_t port);
void outl(uint16_t port, uint32_t value);
uint32_t inl(uint16_t port);
void outsb(uint16_t port, const void *addr, size_t count);
void insb(uint16_t port, void *addr, size_t count);
void outsw(uint16_t port, const void *addr, size_t count);
void insw(uint16_t port, void *addr, size_t count);
void outsl(uint16_t port, const void *addr, size_t count);
void insl(uint16_t port, void *addr, size_t count);
void io_wait(void);

#endif /* _ARCH_CPU_H */