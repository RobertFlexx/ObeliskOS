/*
 * Obelisk OS - Interrupt Stubs
 * From Axioms, Order.
 *
 * This file contains assembly stubs for all interrupt handlers.
 * Each stub saves CPU state and calls the appropriate C handler.
 */

.section .text
.code64

/* ==========================================================================
 * Macro for ISR stub without error code
 * ========================================================================== */
.macro ISR_NOERRCODE num
.global isr_stub_\num
.type isr_stub_\num, @function
isr_stub_\num:
    pushq $0                    /* Dummy error code */
    pushq $\num                 /* Interrupt vector */
    jmp isr_common_stub
.endm

/* ==========================================================================
 * Macro for ISR stub with error code (pushed by CPU)
 * ========================================================================== */
.macro ISR_ERRCODE num
.global isr_stub_\num
.type isr_stub_\num, @function
isr_stub_\num:
    pushq $\num                 /* Interrupt vector */
    jmp isr_common_stub
.endm

/* ==========================================================================
 * Macro for IRQ stub
 * ========================================================================== */
.macro IRQ_STUB num
.global irq_stub_\num
.type irq_stub_\num, @function
irq_stub_\num:
    pushq $0                    /* Dummy error code */
    pushq $(\num + 32)          /* IRQ number + 32 */
    jmp irq_common_stub
.endm

/* ==========================================================================
 * CPU Exception Stubs (0-31)
 * ========================================================================== */
ISR_NOERRCODE 0     /* Division Error */
ISR_NOERRCODE 1     /* Debug */
ISR_NOERRCODE 2     /* NMI */
ISR_NOERRCODE 3     /* Breakpoint */
ISR_NOERRCODE 4     /* Overflow */
ISR_NOERRCODE 5     /* Bound Range Exceeded */
ISR_NOERRCODE 6     /* Invalid Opcode */
ISR_NOERRCODE 7     /* Device Not Available */
ISR_ERRCODE   8     /* Double Fault */
ISR_NOERRCODE 9     /* Coprocessor Segment Overrun (legacy) */
ISR_ERRCODE   10    /* Invalid TSS */
ISR_ERRCODE   11    /* Segment Not Present */
ISR_ERRCODE   12    /* Stack-Segment Fault */
ISR_ERRCODE   13    /* General Protection Fault */
ISR_ERRCODE   14    /* Page Fault */
ISR_NOERRCODE 15    /* Reserved */
ISR_NOERRCODE 16    /* x87 FPU Error */
ISR_ERRCODE   17    /* Alignment Check */
ISR_NOERRCODE 18    /* Machine Check */
ISR_NOERRCODE 19    /* SIMD Floating-Point */
ISR_NOERRCODE 20    /* Virtualization */
ISR_ERRCODE   21    /* Control Protection */
ISR_NOERRCODE 22    /* Reserved */
ISR_NOERRCODE 23    /* Reserved */
ISR_NOERRCODE 24    /* Reserved */
ISR_NOERRCODE 25    /* Reserved */
ISR_NOERRCODE 26    /* Reserved */
ISR_NOERRCODE 27    /* Reserved */
ISR_NOERRCODE 28    /* Hypervisor Injection */
ISR_NOERRCODE 29    /* VMM Communication */
ISR_ERRCODE   30    /* Security Exception */
ISR_NOERRCODE 31    /* Reserved */

/* ==========================================================================
 * IRQ Stubs (0-15, mapped to vectors 32-47)
 * ========================================================================== */
IRQ_STUB 0      /* Timer */
IRQ_STUB 1      /* Keyboard */
IRQ_STUB 2      /* Cascade */
IRQ_STUB 3      /* COM2 */
IRQ_STUB 4      /* COM1 */
IRQ_STUB 5      /* LPT2 */
IRQ_STUB 6      /* Floppy */
IRQ_STUB 7      /* LPT1 / Spurious */
IRQ_STUB 8      /* RTC */
IRQ_STUB 9      /* ACPI */
IRQ_STUB 10     /* Available */
IRQ_STUB 11     /* Available */
IRQ_STUB 12     /* PS/2 Mouse */
IRQ_STUB 13     /* FPU */
IRQ_STUB 14     /* Primary ATA */
IRQ_STUB 15     /* Secondary ATA */

/* ==========================================================================
 * Common ISR stub
 * Saves all registers and calls exception_handler()
 * ========================================================================== */
isr_common_stub:
    /* Save all general-purpose registers */
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    
    /* Save segment registers (for debugging) */
    movw %ds, %ax
    pushq %rax
    movw %es, %ax
    pushq %rax
    
    /* Load kernel data segments */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    
    /* Pass pointer to register frame as argument */
    movq %rsp, %rdi
    addq $16, %rdi              /* Skip saved segment registers */
    
    /* Align stack to 16 bytes */
    movq %rsp, %rbp
    andq $-16, %rsp
    
    /* Call C exception handler */
    call exception_handler
    
    /* Restore stack */
    movq %rbp, %rsp
    
    /* Restore segment registers */
    popq %rax
    movw %ax, %es
    popq %rax
    movw %ax, %ds
    
    /* Restore general-purpose registers */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    
    /* Remove error code and vector from stack */
    addq $16, %rsp
    
    /* Return from interrupt */
    iretq

/* ==========================================================================
 * Common IRQ stub
 * Saves all registers and calls irq_handler()
 * ========================================================================== */
irq_common_stub:
    /* Save all general-purpose registers */
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    
    /* Load kernel data segments */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    
    /* Pass pointer to register frame as argument */
    movq %rsp, %rdi
    
    /* Align stack to 16 bytes */
    movq %rsp, %rbp
    andq $-16, %rsp
    
    /* Call C IRQ handler */
    call irq_handler
    
    /* Restore stack */
    movq %rbp, %rsp
    
    /* Restore general-purpose registers */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    
    /* Remove error code and vector from stack */
    addq $16, %rsp
    
    /* Return from interrupt */
    iretq

/* ==========================================================================
 * Syscall stub (INT 0x80 legacy interface)
 * ========================================================================== */
.global syscall_stub
.type syscall_stub, @function
syscall_stub:
    /* Save error code and vector placeholders */
    pushq $0                    /* Error code */
    pushq $0x80                 /* Vector */
    
    /* Save registers */
    pushq %rax                  /* Syscall number */
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    
    /* Load kernel data segments */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    
    /* Set up arguments for syscall_dispatch */
    /* rdi = syscall number (from saved rax) */
    movq 112(%rsp), %rdi        /* Original rax */
    /* rsi = pointer to saved registers */
    movq %rsp, %rsi
    
    /* Align stack */
    movq %rsp, %rbp
    andq $-16, %rsp
    
    /* Call syscall dispatcher */
    call syscall_dispatch
    
    /* Restore stack */
    movq %rbp, %rsp
    
    /* Store return value */
    movq %rax, 112(%rsp)        /* Will be restored to rax */
    
    /* Restore registers */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax                   /* Return value */
    
    /* Remove vector and error code */
    addq $16, %rsp
    
    iretq

/* ==========================================================================
 * SYSCALL/SYSRET entry point (fast syscall interface)
 * ========================================================================== */
.global syscall_entry
.type syscall_entry, @function
syscall_entry:
    /* 
     * At this point:
     * - RCX = user RIP
     * - R11 = user RFLAGS
     * - RAX = syscall number
     * - RDI, RSI, RDX, R10, R8, R9 = arguments
     */
    
    /* Build an interrupt-compatible frame for syscall_dispatch + iretq return. */
    pushq $0x1B                 /* User SS */
    pushq %rsp                  /* Placeholder; fix to original RSP below */
    pushq %r11                  /* User RFLAGS */
    pushq $0x23                 /* User CS */
    pushq %rcx                  /* User RIP */

    /* Replace placeholder with original pre-push user RSP. */
    leaq 40(%rsp), %rcx
    movq %rcx, 24(%rsp)

    /* Add vector + error code as expected by struct cpu_regs. */
    pushq $0                    /* Error code */
    pushq $0x80                 /* Vector */

    /* Save registers (same order as syscall_stub). */
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es

    /* rdi=syscall nr, rsi=frame */
    movq 112(%rsp), %rdi
    movq %rsp, %rsi

    movq %rsp, %rbp
    andq $-16, %rsp
    call syscall_dispatch
    movq %rbp, %rsp

    /* Write return value into saved rax slot. */
    movq %rax, 112(%rsp)

    /* Restore registers and return exactly like interrupt path. */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    addq $16, %rsp              /* Drop vector + error code */
    iretq

.section .note.GNU-stack, "", @progbits
