/*
 * Obelisk OS - Context Switching
 * From Axioms, Order.
 *
 * Implements low-level context switch between processes.
 */

.section .text
.code64

/*
 * context_switch(struct cpu_context *old, struct cpu_context *new)
 *
 * Saves current context to 'old' and restores context from 'new'.
 *
 * struct cpu_context {
 *     uint64_t rbx;    // offset 0
 *     uint64_t rbp;    // offset 8
 *     uint64_t r12;    // offset 16
 *     uint64_t r13;    // offset 24
 *     uint64_t r14;    // offset 32
 *     uint64_t r15;    // offset 40
 *     uint64_t rsp;    // offset 48
 *     uint64_t rip;    // offset 56
 *     uint8_t  fpu[512]; // offset 64
 *     bool     fpu_used; // offset 576
 * };
 */

.global context_switch
.type context_switch, @function
context_switch:
    /* Check if old context is NULL (first switch) */
    testq %rdi, %rdi
    jz .restore_context
    
    /* Save callee-saved registers to old context */
    movq %rbx, 0(%rdi)
    movq %rbp, 8(%rdi)
    movq %r12, 16(%rdi)
    movq %r13, 24(%rdi)
    movq %r14, 32(%rdi)
    movq %r15, 40(%rdi)
    
    /* Save stack pointer */
    movq %rsp, 48(%rdi)
    
    /* Save return address as RIP */
    movq (%rsp), %rax
    movq %rax, 56(%rdi)
    
    /* Check if FPU was used and save state */
    cmpb $0, 576(%rdi)
    je .restore_context
    fxsave 64(%rdi)

.restore_context:
    /* Restore callee-saved registers from new context */
    movq 0(%rsi), %rbx
    movq 8(%rsi), %rbp
    movq 16(%rsi), %r12
    movq 24(%rsi), %r13
    movq 32(%rsi), %r14
    movq 40(%rsi), %r15
    
    /* Restore stack pointer */
    movq 48(%rsi), %rsp
    
    /* Check if FPU state needs to be restored */
    cmpb $0, 576(%rsi)
    je .skip_fpu_restore
    fxrstor 64(%rsi)

.skip_fpu_restore:
    /* SysV ABI requires DF=0 before calling/returning through C paths. */
    cld
    /* Resume by returning through the restored call frame.
     * %rsp points to the saved return address in that task's stack. */
    ret

/*
 * context_switch_initial(struct cpu_context *new)
 *
 * Used for the very first context switch to a new process.
 * Does not save any state.
 */
.global context_switch_initial
.type context_switch_initial, @function
context_switch_initial:
    /* Restore registers from new context */
    movq 0(%rdi), %rbx
    movq 8(%rdi), %rbp
    movq 16(%rdi), %r12
    movq 24(%rdi), %r13
    movq 32(%rdi), %r14
    movq 40(%rdi), %r15
    
    /* Restore stack pointer */
    movq 48(%rdi), %rsp

    cld
    
    /* Jump to entry point */
    movq 56(%rdi), %rax
    jmp *%rax

/*
 * fork_return
 *
 * Return point for child process after fork.
 * Returns 0 to indicate this is the child.
 */
.global fork_return
.type fork_return, @function
fork_return:
    /*
     * Child is resumed with %rsp pointing at struct cpu_regs.
     * Restore exactly like syscall return path and iretq to user mode.
     */
    movq $0, 112(%rsp)              /* saved rax = 0 (child return value) */

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

    addq $16, %rsp                  /* drop vector + error code */
    cld
    iretq

/*
 * user_mode_enter(uint64_t entry, uint64_t user_stack, uint64_t fs_base)
 *
 * Enter user mode for the first time.
 */
.global user_mode_enter
.type user_mode_enter, @function
user_mode_enter:
    /*
     * Arguments:
     *   rdi = entry point
     *   rsi = user stack pointer
     *   rdx = IA32_FS_BASE (TLS / dynamic ELF)
     */
    movq %rdi, %r8
    movq %rsi, %r9
    movq %rdx, %r10

    /* Set up user segments */
    movw $0x1B, %ax         /* User data segment | RPL 3 */
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs

    /*
     * Set FS base last, immediately before iretq. Some paths rely on %fs-relative
     * addressing in ld.so before arch_prctl; the segment load above must not leave
     * IA32_FS_BASE stale relative to the new address space.
     */
    movl $0xC0000100, %ecx  /* MSR_IA32_FS_BASE */
    movl %r10d, %eax
    shrq $32, %r10
    movl %r10d, %edx
    wrmsr

    movq %r8, %rdi
    movq %r9, %rsi
    
    /* Prepare iretq frame */
    pushq $0x1B             /* User SS */
    pushq %rsi              /* User RSP */
    pushq $0x202            /* RFLAGS (IF=1) */
    pushq $0x23             /* User CS | RPL 3 */
    pushq %rdi              /* User RIP */
    
    /* Clear all registers for security */
    xorq %rax, %rax
    xorq %rbx, %rbx
    xorq %rcx, %rcx
    xorq %rdx, %rdx
    xorq %rsi, %rsi
    xorq %rdi, %rdi
    xorq %rbp, %rbp
    xorq %r8, %r8
    xorq %r9, %r9
    xorq %r10, %r10
    xorq %r11, %r11
    xorq %r12, %r12
    xorq %r13, %r13
    xorq %r14, %r14
    xorq %r15, %r15

    cld
    /* Return to user mode */
    iretq

.section .note.GNU-stack, "", @progbits
