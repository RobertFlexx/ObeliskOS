#include <stdint.h>
#include <stddef.h>

extern int printf(const char *fmt, ...);
extern int open(const char *pathname, int flags, int mode);
extern int close(int fd);
extern long read(int fd, void *buf, unsigned long count);
extern long lseek(int fd, long off, int whence);

#define O_RDONLY 0x0000
#define SEEK_SET 0

#define EI_NIDENT 16
#define PT_DYNAMIC 2
#define ET_DYN 3

struct elf64_hdr {
    unsigned char e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed));

struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed));

struct elf64_dyn {
    int64_t d_tag;
    uint64_t d_val;
} __attribute__((packed));

static int read_exact(int fd, void *buf, unsigned long n) {
    unsigned long done = 0;
    while (done < n) {
        long r = read(fd, (char *)buf + done, n - done);
        if (r <= 0) return -1;
        done += (unsigned long)r;
    }
    return 0;
}

static __attribute__((used)) void probe_main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: loader-elf-probe <elf-path>\n");
        __asm__ volatile("int $0x80" : : "a"(60), "D"(1) : "cc", "memory");
        __builtin_unreachable();
    }

    const char *path = argv[1];
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
        printf("loader-elf-probe: open failed for %s\n", path);
        __asm__ volatile("int $0x80" : : "a"(60), "D"(2) : "cc", "memory");
        __builtin_unreachable();
    }

    struct elf64_hdr eh;
    if (read_exact(fd, &eh, sizeof(eh)) < 0) {
        printf("loader-elf-probe: short read elf header\n");
        close(fd);
        __asm__ volatile("int $0x80" : : "a"(60), "D"(3) : "cc", "memory");
        __builtin_unreachable();
    }
    if (*(uint32_t *)eh.e_ident != 0x464c457fU) {
        printf("loader-elf-probe: not ELF magic\n");
        close(fd);
        __asm__ volatile("int $0x80" : : "a"(60), "D"(4) : "cc", "memory");
        __builtin_unreachable();
    }

    printf("loader-elf-probe: %s type=%u phoff=0x%llx phnum=%u\n",
           path, (unsigned)eh.e_type, (unsigned long long)eh.e_phoff, (unsigned)eh.e_phnum);

    int found = 0;
    for (uint16_t i = 0; i < eh.e_phnum; i++) {
        struct elf64_phdr ph;
        if (lseek(fd, (long)(eh.e_phoff + (uint64_t)i * sizeof(ph)), SEEK_SET) < 0) {
            break;
        }
        if (read_exact(fd, &ph, sizeof(ph)) < 0) {
            break;
        }
        if (ph.p_type != PT_DYNAMIC || ph.p_filesz < sizeof(struct elf64_dyn)) {
            continue;
        }
        struct elf64_dyn d0;
        if (lseek(fd, (long)ph.p_offset, SEEK_SET) < 0) {
            break;
        }
        if (read_exact(fd, &d0, sizeof(d0)) < 0) {
            break;
        }
        printf("loader-elf-probe: PT_DYNAMIC off=0x%llx vaddr=0x%llx first_tag=0x%llx first_val=0x%llx\n",
               (unsigned long long)ph.p_offset,
               (unsigned long long)ph.p_vaddr,
               (unsigned long long)d0.d_tag,
               (unsigned long long)d0.d_val);
        found = 1;
        break;
    }

    close(fd);
    if (!found) {
        printf("loader-elf-probe: PT_DYNAMIC not found\n");
        __asm__ volatile("int $0x80" : : "a"(60), "D"(5) : "cc", "memory");
    } else {
        __asm__ volatile("int $0x80" : : "a"(60), "D"(0) : "cc", "memory");
    }
    __builtin_unreachable();
}

__attribute__((naked)) void _start(void) {
    __asm__ volatile(
        "xor %rbp, %rbp\n"
        "mov (%rsp), %rdi\n"
        "lea 8(%rsp), %rsi\n"
        "andq $-16, %rsp\n"
        "call probe_main\n"
        "mov $1, %edi\n"
        "mov $60, %eax\n"
        "int $0x80\n"
    );
}
