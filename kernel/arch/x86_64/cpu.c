/*
 * Obelisk OS - Minimal CPU Info
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <arch/cpu.h>
#include <arch/regs.h>

static struct cpu_info boot_cpu = {
    .id = 0,
    .apic_id = 0,
    .tsc_frequency = 3000000000ULL,
};

void cpu_detect_features(struct cpu_info *cpu) {
    uint32_t eax, ebx, ecx, edx;
    char vendor[13];

    if (!cpu) return;

    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    ((uint32_t *)vendor)[0] = ebx;
    ((uint32_t *)vendor)[1] = edx;
    ((uint32_t *)vendor)[2] = ecx;
    vendor[12] = '\0';
    memcpy(cpu->vendor, vendor, 13);

    /* Basic feature bits from CPUID leaf 1 EDX. */
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    cpu->features = edx;
}

bool cpu_has_feature(uint64_t feature) {
    return (boot_cpu.features & feature) != 0;
}

void cpu_print_info(struct cpu_info *cpu) {
    (void)cpu;
}

void cpu_init(void) {
    cpu_detect_features(&boot_cpu);

    /* Enable x87/SSE for userland and kernel C code paths. */
    uint64_t cr0 = read_cr0();
    cr0 &= ~CR0_EM;           /* Disable emulation */
    cr0 |= CR0_MP | CR0_NE;   /* Monitor co-processor + numeric exceptions */
    write_cr0(cr0);

    uint64_t cr4 = read_cr4();
    cr4 |= CR4_OSFXSR | CR4_OSXMMEXCPT;
    write_cr4(cr4);

    fpu_init();

    printk(KERN_INFO "CPU control: CR0=0x%lx CR4=0x%lx\n", read_cr0(), read_cr4());
}

struct cpu_info *cpu_get_current(void) {
    return &boot_cpu;
}
