/*
 * Obelisk OS - opkg startup shim
 * From Axioms, Order.
 *
 * Keep only startup/argc glue in C.
 * Command logic lives in D (opkg_d.d) for static in-image mode.
 */

extern int opkg_main_d(int argc, char **argv);
extern void _exit(int status);

static __attribute__((used)) void opkg_main(int argc, char **argv) {
    _exit(opkg_main_d(argc, argv));
}

__attribute__((naked)) void _start(void) {
    __asm__ volatile(
        "xor %rbp, %rbp\n"
        "mov (%rsp), %rdi\n"   /* argc */
        "lea 8(%rsp), %rsi\n"  /* argv */
        "andq $-16, %rsp\n"
        "call opkg_main\n"
        "mov $1, %edi\n"
        "call _exit\n"
    );
}
