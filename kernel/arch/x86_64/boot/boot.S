/*
 * Obelisk OS - Multiboot2 Entry Point
 * From Axioms, Order.
 *
 * This file implements the boot sequence:
 * 1. Multiboot2 header for GRUB
 * 2. Initial page tables for higher-half kernel
 * 3. Switch to 64-bit long mode
 * 4. Jump to kernel_main()
 */

.section .multiboot, "a"
.align 8

/* Multiboot2 header */
multiboot_header_start:
    .long 0xe85250d6                                /* Magic number */
    .long 0                                          /* Architecture: i386 */
    .long multiboot_header_end - multiboot_header_start  /* Header length */
    .long -(0xe85250d6 + 0 + (multiboot_header_end - multiboot_header_start))  /* Checksum */

    /* Information request tag */
    .align 8
info_request_tag_start:
    .short 1                                         /* Type: information request */
    .short 0                                         /* Flags */
    .long info_request_tag_end - info_request_tag_start
    .long 6                                          /* Request: memory map */
    .long 8                                          /* Request: framebuffer info */
    .long 14                                         /* Request: ACPI 1.0 RSDP */
    .long 15                                         /* Request: ACPI 2.0+ RSDP */
info_request_tag_end:

    /* Framebuffer tag */
    .align 8
framebuffer_tag_start:
    .short 5                                         /* Type: framebuffer */
    .short 0                                         /* Flags */
    .long framebuffer_tag_end - framebuffer_tag_start
    /* 0 = let the bootloader pick any mode that works (required for UEFI/GOP laptops). */
    .long 0                                          /* Width */
    .long 0                                          /* Height */
    .long 0                                          /* Depth */
framebuffer_tag_end:

    /* Module alignment tag */
    .align 8
module_align_tag_start:
    .short 6                                         /* Type: module alignment */
    .short 0                                         /* Flags */
    .long module_align_tag_end - module_align_tag_start
module_align_tag_end:

    /* End tag */
    .align 8
    .short 0                                         /* Type: end */
    .short 0                                         /* Flags */
    .long 8                                          /* Size */
multiboot_header_end:

/* ============================================================================ */

.section .bss.boot, "aw", @nobits
.align 4096

/* Initial page tables (identity map + higher half) */
boot_pml4:
    .skip 4096
boot_pdpt_low:
    .skip 4096
boot_pdpt_high:
    .skip 4096
boot_pd:
    .skip 4096

/* Bootstrap stack */
.align 16
boot_stack_bottom:
    .skip 32768                                      /* 32 KB stack */
boot_stack_top:

/* Saved multiboot handoff values */
boot_mb_magic:
    .long 0
boot_mb_info:
    .long 0

/* ============================================================================ */

.section .rodata.boot, "a"
.align 8

/* GDT for 64-bit mode */
gdt64:
    .quad 0                                          /* Null descriptor */
gdt64_code:
    .quad 0x00209A0000000000                         /* Code: 64-bit, present, DPL=0 */
gdt64_data:
    .quad 0x0000920000000000                         /* Data: present, DPL=0 */
gdt64_user_data:
    .quad 0x0000F20000000000                         /* User data: present, DPL=3 */
gdt64_user_code:
    .quad 0x0020FA0000000000                         /* User code: 64-bit, present, DPL=3 */
gdt64_tss:
    .quad 0                                          /* TSS low (filled at runtime) */
    .quad 0                                          /* TSS high */
gdt64_end:

gdt64_ptr:
    .word gdt64_end - gdt64 - 1                      /* GDT limit */
    .quad gdt64                                      /* GDT base (updated later) */

/* Error messages */
err_no_long_mode:
    .asciz "ERROR: CPU does not support long mode\r\n"
err_no_cpuid:
    .asciz "ERROR: CPU does not support CPUID\r\n"

/* ============================================================================ */

.section .text.boot, "ax"
.code32

.global _start
.type _start, @function

_start:
    /* Disable interrupts */
    cli
    /* String ops in this path assume DF=0 (forward). */
    cld
    
    /* Save multiboot info */
    movl %eax, boot_mb_magic
    movl %ebx, boot_mb_info
    
    /* Set up a minimal stack for 32-bit operations */
    movl $boot_stack_top, %esp
    
    /* Check for CPUID support */
    pushfl
    popl %eax
    movl %eax, %ecx
    xorl $0x200000, %eax                             /* Flip ID bit */
    pushl %eax
    popfl
    pushfl
    popl %eax
    pushl %ecx
    popfl
    xorl %ecx, %eax
    jz .no_cpuid
    
    /* Check for long mode support */
    movl $0x80000000, %eax
    cpuid
    cmpl $0x80000001, %eax
    jb .no_long_mode
    
    movl $0x80000001, %eax
    cpuid
    testl $(1 << 29), %edx                           /* Check LM bit */
    jz .no_long_mode
    
    /* Set up initial page tables */
    call setup_page_tables
    
    /* Enable PAE */
    movl %cr4, %eax
    orl $0x20, %eax                                  /* CR4.PAE */
    movl %eax, %cr4
    
    /* Load PML4 address */
    movl $boot_pml4, %eax
    movl %eax, %cr3
    
    /* Enable long mode and NXE in EFER MSR */
    movl $0xC0000080, %ecx                           /* EFER MSR */
    rdmsr
    orl $0x900, %eax                                 /* EFER.LME | EFER.NXE */
    wrmsr
    
    /* Enable paging (this activates long mode) and write-protect in supervisor mode */
    movl %cr0, %eax
    orl $0x10000, %eax                               /* CR0.WP */
    orl $0x80000000, %eax                            /* CR0.PG */
    movl %eax, %cr0
    
    /* Load 64-bit GDT */
    lgdt gdt64_ptr
    
    /* Far jump to 64-bit code */
    ljmp $0x08, $long_mode_entry

.no_cpuid:
    movl $err_no_cpuid, %esi
    jmp .error_halt

.no_long_mode:
    movl $err_no_long_mode, %esi
    jmp .error_halt

.error_halt:
    /* Print error message to serial port (COM1) */
    movw $0x3F8, %dx
.print_loop:
    lodsb
    testb %al, %al
    jz .halt_loop
    outb %al, %dx
    jmp .print_loop
.halt_loop:
    hlt
    jmp .halt_loop

/* ----------------------------------------------------------------------------
 * setup_page_tables: Create initial page tables
 * Identity maps first 2GB and maps them to higher half
 */
setup_page_tables:
    /* Zero out page tables */
    movl $boot_pml4, %edi
    movl $(4 * 4096 / 4), %ecx
    xorl %eax, %eax
    rep stosl
    
    /* PML4[0] → boot_pdpt_low (identity map) */
    movl $boot_pdpt_low, %eax
    orl $0x03, %eax                                  /* Present + Writable */
    movl %eax, boot_pml4

    /* PML4[273] → boot_pdpt_low (physmap base 0xffff888000000000) */
    movl $boot_pdpt_low, %eax
    orl $0x03, %eax
    movl %eax, boot_pml4 + (273 * 8)
    
    /* PML4[511] → boot_pdpt_high (higher half: 0xFFFFFFFF80000000) */
    movl $boot_pdpt_high, %eax
    orl $0x03, %eax
    movl %eax, boot_pml4 + (511 * 8)
    
    /* PDPT_low[0] → boot_pd */
    movl $boot_pd, %eax
    orl $0x03, %eax
    movl %eax, boot_pdpt_low
    
    /* PDPT_high[510] → boot_pd (for 0xFFFFFFFF80000000) */
    movl $boot_pd, %eax
    orl $0x03, %eax
    movl %eax, boot_pdpt_high + (510 * 8)
    
    /* Fill boot_pd with 2MB pages (first 1GB) */
    movl $boot_pd, %edi
    movl $0x83, %eax                                 /* Present + Writable + Huge */
    movl $512, %ecx
.fill_pd_loop:
    movl %eax, (%edi)
    movl $0, 4(%edi)                                 /* Upper 32 bits = 0 */
    addl $0x200000, %eax                             /* Next 2MB */
    addl $8, %edi
    decl %ecx
    jnz .fill_pd_loop
    
    ret

/* ============================================================================ */

.code64

/* 64-bit entry point */
long_mode_entry:
    /* Set up segment registers */
    movw $0x10, %ax                                  /* Kernel data selector */
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    
    /* Set up stack in higher half */
    movabsq $boot_stack_top, %rsp
    
    /* Clear direction flag */
    cld
    
    /* Clear BSS (use higher half addresses) */
    movabsq $__bss_start, %rdi
    movabsq $__bss_end, %rcx
    subq %rdi, %rcx
    shrq $3, %rcx                                    /* Divide by 8 */
    xorq %rax, %rax
    rep stosq
    
    /* Fix GDT pointer to use higher half address */
    movabsq $gdt64, %rax
    movabsq $gdt64_ptr + 2, %rbx
    movq %rax, (%rbx)
    lgdt gdt64_ptr
    
    /* Prepare arguments for kernel_main */
    /* rdi = multiboot magic, rsi = multiboot info pointer */
    movl boot_mb_magic, %edi
    movl boot_mb_info, %esi
    
    /* Call kernel main */
    movabsq $kernel_main, %rax
    call *%rax
    
    /* If kernel_main returns, halt */
.halt:
    cli
    hlt
    jmp .halt

/* ============================================================================ */

.section .note.GNU-stack, "", @progbits
