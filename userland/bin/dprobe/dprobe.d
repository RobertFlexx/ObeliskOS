/*
 * Obelisk OS - static D probe binary (-betterC)
 * From Axioms, Order.
 */

module bin.dprobe.dprobe;

extern(C) nothrow:

alias size_t = ulong;
alias ssize_t = long;

extern ssize_t write(int fd, const void* buf, size_t count);
extern int _exit(int status);

extern(C) void _start() {
    immutable msg = "dprobe: static D userspace execution OK\n";
    write(1, msg.ptr, msg.length);
    _exit(0);
}
