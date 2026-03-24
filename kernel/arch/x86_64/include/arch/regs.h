/*
 * Obelisk OS - x86_64 Register Definitions
 * From Axioms, Order.
 */

#ifndef _ARCH_REGS_H
#define _ARCH_REGS_H

#include <obelisk/types.h>

/* General purpose registers saved during interrupt/syscall */
struct cpu_regs {
    /* Saved by interrupt stub */
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;
    
    /* Pushed by CPU or stub */
    uint64_t vector;        /* Interrupt vector number */
    uint64_t error_code;    /* Error code (0 if none) */
    
    /* Pushed by CPU during interrupt */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __packed;

/* CPU context for context switching */
struct cpu_context {
    /* Callee-saved registers */
    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    
    /* Stack pointer */
    uint64_t rsp;
    
    /* Instruction pointer */
    uint64_t rip;
    
    /* FPU/SSE state (if used) */
    uint8_t fpu_state[512] __aligned(16);
    bool fpu_used;
} __aligned(16);

/* Syscall frame (different from interrupt frame) */
struct syscall_frame {
    /* User registers */
    uint64_t rdi;       /* Arg 1 */
    uint64_t rsi;       /* Arg 2 */
    uint64_t rdx;       /* Arg 3 */
    uint64_t r10;       /* Arg 4 (normally rcx, but syscall clobbers it) */
    uint64_t r8;        /* Arg 5 */
    uint64_t r9;        /* Arg 6 */
    
    /* Syscall number */
    uint64_t rax;
    
    /* Saved by syscall entry */
    uint64_t rcx;       /* User RIP */
    uint64_t r11;       /* User RFLAGS */
    
    /* User stack */
    uint64_t rsp;
} __packed;

/* FPU state operations */
static __always_inline void fpu_save(uint8_t *state) {
    __asm__ volatile("fxsave (%0)" :: "r"(state) : "memory");
}

static __always_inline void fpu_restore(const uint8_t *state) {
    __asm__ volatile("fxrstor (%0)" :: "r"(state));
}

static __always_inline void fpu_init(void) {
    __asm__ volatile("fninit");
}

/* Context switch function (implemented in assembly) */
extern void context_switch(struct cpu_context *old, struct cpu_context *new);
extern void context_switch_initial(struct cpu_context *new);
extern void user_mode_enter(uint64_t entry, uint64_t stack);

/* Helper to get syscall arguments from frame */
static inline void syscall_get_args(struct syscall_frame *frame,
                                    uint64_t *a1, uint64_t *a2, uint64_t *a3,
                                    uint64_t *a4, uint64_t *a5, uint64_t *a6) {
    *a1 = frame->rdi;
    *a2 = frame->rsi;
    *a3 = frame->rdx;
    *a4 = frame->r10;
    *a5 = frame->r8;
    *a6 = frame->r9;
}

/* Set syscall return value */
static inline void syscall_set_return(struct syscall_frame *frame, uint64_t val) {
    frame->rax = val;
}

/* Print registers for debugging */
void dump_regs(struct cpu_regs *regs);
void dump_context(struct cpu_context *ctx);

#endif /* _ARCH_REGS_H */