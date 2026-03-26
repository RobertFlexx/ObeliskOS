/*
 * Obelisk OS - Exec Implementation
 * From Axioms, Order.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <proc/process.h>
#include <proc/scheduler.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <fs/vfs.h>
#include <fs/inode.h>
#include <uapi/syscall.h>

/* Off-by-default loader debug instrumentation for responsiveness. */
extern int loader_exec_debug_enabled;

/* ELF definitions */
#define ELF_MAGIC       0x464C457F  /* "\x7FELF" */

#define ET_EXEC         2
#define ET_DYN          3

#define EM_X86_64       62

#define PT_NULL         0
#define PT_LOAD         1
#define PT_DYNAMIC      2
#define PT_INTERP       3
#define PT_NOTE         4
#define PT_PHDR         6
#define PT_TLS          7

#define PF_X            0x1
#define PF_W            0x2
#define PF_R            0x4

/* ELF64 header */
struct elf64_hdr {
    uint8_t  e_ident[16];
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
} __packed;

/* ELF64 program header */
struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __packed;

/* Maximum arguments and environment */
#define MAX_ARG_PAGES   32
#define MAX_ARG_STRLEN  4096
#define MAX_ARGC        256
#define ET_DYN_MAIN_BIAS    0x0000555555554000ULL
#define ET_DYN_INTERP_BIAS  0x00007ffff7dc0000ULL

#ifndef CONFIG_EXPERIMENTAL_DYNAMIC_ELF
#define CONFIG_EXPERIMENTAL_DYNAMIC_ELF 0
#endif

#ifndef CONFIG_EXEC_TRACE
#define CONFIG_EXEC_TRACE 0
#endif

#if CONFIG_EXEC_TRACE
#define EXEC_LOG(...) printk(KERN_INFO __VA_ARGS__)
#else
#define EXEC_LOG(...) do { } while (0)
#endif

/* Minimal auxv entries for libc-compatible startup */
#define AT_NULL         0
#define AT_IGNORE       1
#define AT_PHDR         3
#define AT_PHENT        4
#define AT_PHNUM        5
#define AT_PAGESZ       6
#define AT_BASE         7
#define AT_FLAGS        8
#define AT_ENTRY        9
#define AT_UID          11
#define AT_EUID         12
#define AT_GID          13
#define AT_EGID         14
#define AT_PLATFORM     15
#define AT_HWCAP        16
#define AT_CLKTCK       17
#define AT_SECURE       23
#define AT_RANDOM       25
#define AT_HWCAP2       26
#define AT_EXECFN       31
#define AT_SYSINFO_EHDR 33

#define HWCAP_X86_64_BASELINE 0x2ULL
#define HWCAP2_X86_64_BASELINE 0x2ULL

#define DT_NULL         0
#define DT_NEEDED       1
#define DT_STRTAB       5
#define DT_SYMTAB       6
#define DT_STRSZ        10
#define DT_RELA         7
#define DT_RELASZ       8
#define DT_RELAENT      9
#define DT_DEBUG        21
#define DT_FLAGS        30
#define DT_FLAGS_1      0x6ffffffb
#define DT_GNU_HASH     0x6ffffef5
#define DT_RELR         0x24
#define DT_RELRSZ       0x23
#define DT_RELRENT      0x25
#define DT_SYMENT       11
#define DT_JMPREL       23
#define DT_PLTRELSZ     2
#define DT_PLTREL       20

#define ELF64_R_SYM(i)  ((uint32_t)((uint64_t)(i) >> 32))
#define ELF64_R_TYPE(i) ((uint32_t)((uint64_t)(i) & 0xffffffffUL))

#define R_X86_64_RELATIVE   8
#define R_X86_64_COPY       5
#define R_X86_64_GLOB_DAT   6
#define R_X86_64_JUMP_SLOT  7
#define R_X86_64_IRELATIVE  37

#define SHN_UNDEF 0

struct elf64_rela {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} __packed;

struct elf64_sym {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} __packed;

struct exec_image_info {
    uint64_t entry;
    uint64_t load_bias;
    uint64_t phdr;
    uint64_t phent;
    uint64_t phnum;
    bool has_tls;
    uint64_t tls_base; /* x86_64 TLS variant II: initial thread pointer (end of TLS block, aligned). */
};

struct auxv_pair {
    uint64_t type;
    uint64_t value;
};

struct elf64_dyn {
    int64_t d_tag;
    union {
        uint64_t d_val;
        uint64_t d_ptr;
    } d_un;
} __packed;

static int read_user_memory(struct process *proc, uint64_t uaddr, void *dst, size_t len) {
    uint8_t *p = (uint8_t *)dst;
    while (len > 0) {
        uint64_t phys = mmu_resolve(proc->mm->pt, uaddr);
        if (!phys) {
            return -EFAULT;
        }
        /* Debug-only: ensure the mapping we are touching looks like user memory. */
        if (loader_exec_debug_enabled) {
            uint64_t flags = mmu_get_flags(proc->mm->pt, uaddr);
            if (!(flags & PTE_PRESENT) || !(flags & PTE_USER)) {
                return -EFAULT;
            }
        }
        size_t off = (size_t)(uaddr & (PAGE_SIZE - 1));
        size_t chunk = MIN(len, PAGE_SIZE - off);
        memcpy(p, (const uint8_t *)PHYS_TO_VIRT(phys), chunk);
        uaddr += chunk;
        p += chunk;
        len -= chunk;
    }
    return 0;
}

static int read_user_u64(struct process *proc, uint64_t uaddr, uint64_t *value) {
    return read_user_memory(proc, uaddr, value, sizeof(*value));
}

static bool path_contains_ld_linux(const char *filename) {
    if (!filename) {
        return false;
    }
    const char *needle = "ld-linux";
    size_t nlen = strlen(needle);
    size_t flen = strlen(filename);
    if (flen < nlen) {
        return false;
    }
    for (size_t i = 0; i + nlen <= flen; i++) {
        if (strncmp(filename + i, needle, nlen) == 0) {
            return true;
        }
    }
    const char *probe = "xdm";
    size_t plen = strlen(probe);
    if (flen >= plen) {
        for (size_t i = 0; i + plen <= flen; i++) {
            if (strncmp(filename + i, probe, plen) == 0) {
                return true;
            }
        }
    }
    probe = "xinit";
    plen = strlen(probe);
    if (flen >= plen) {
        for (size_t i = 0; i + plen <= flen; i++) {
            if (strncmp(filename + i, probe, plen) == 0) {
                return true;
            }
        }
    }
    return false;
}

static void exec_debug_dump_dynamic(struct process *proc, struct file *src_file,
                                    const struct elf64_hdr *hdr, const struct exec_image_info *img,
                                    const char *label, const char *filename) {
    if (!proc || !hdr || !img || !loader_exec_debug_enabled || !path_contains_ld_linux(filename)) {
        return;
    }
    if (hdr->e_phentsize != sizeof(struct elf64_phdr) || hdr->e_phnum == 0) {
        printk(KERN_INFO "exec: %s dynamic dump skipped (bad phdr table)\n", label);
        return;
    }

    for (uint16_t i = 0; i < hdr->e_phnum; i++) {
        struct elf64_phdr phdr;
        uint64_t phdr_addr = img->phdr + (uint64_t)i * sizeof(struct elf64_phdr);
        if (read_user_memory(proc, phdr_addr, &phdr, sizeof(phdr)) < 0) {
            break;
        }
        if (phdr.p_type != PT_DYNAMIC || phdr.p_memsz < sizeof(struct elf64_dyn)) {
            continue;
        }
        uint64_t dyn_vaddr = img->load_bias + phdr.p_vaddr;
        size_t max_dyn = (size_t)(phdr.p_memsz / sizeof(struct elf64_dyn));
        if (max_dyn > 96) {
            max_dyn = 96;
        }
        printk(KERN_INFO "exec: %s PT_DYNAMIC vaddr=0x%lx entries=%zu\n", label, dyn_vaddr, max_dyn);
        if (src_file && phdr.p_filesz >= sizeof(struct elf64_dyn)) {
            struct elf64_dyn src_dyn0;
            off_t dyn_off = phdr.p_offset;
            if (vfs_read(src_file, &src_dyn0, sizeof(src_dyn0), &dyn_off) == sizeof(src_dyn0)) {
                printk(KERN_INFO "exec: %s PT_DYNAMIC file[0] tag=0x%lx val=0x%lx\n",
                       label, (uint64_t)src_dyn0.d_tag, src_dyn0.d_un.d_val);
            }
        }
        for (size_t di = 0; di < max_dyn; di++) {
            struct elf64_dyn dyn;
            if (read_user_memory(proc, dyn_vaddr + di * sizeof(struct elf64_dyn), &dyn, sizeof(dyn)) < 0) {
                printk(KERN_INFO "exec: %s dyn[%zu] unreadable\n", label, di);
                break;
            }
            if ((uint64_t)dyn.d_tag == DT_NULL) {
                printk(KERN_INFO "exec: %s dyn[%zu] DT_NULL\n", label, di);
                break;
            }
            if ((uint64_t)dyn.d_tag == DT_STRTAB || (uint64_t)dyn.d_tag == DT_SYMTAB ||
                (uint64_t)dyn.d_tag == DT_STRSZ || (uint64_t)dyn.d_tag == DT_RELA ||
                (uint64_t)dyn.d_tag == DT_RELASZ || (uint64_t)dyn.d_tag == DT_RELAENT ||
                (uint64_t)dyn.d_tag == DT_GNU_HASH || (uint64_t)dyn.d_tag == DT_DEBUG ||
                (uint64_t)dyn.d_tag == DT_FLAGS || (uint64_t)dyn.d_tag == DT_FLAGS_1 ||
                (uint64_t)dyn.d_tag == DT_RELR || (uint64_t)dyn.d_tag == DT_RELRSZ ||
                (uint64_t)dyn.d_tag == DT_RELRENT || (uint64_t)dyn.d_tag == DT_NEEDED) {
                printk(KERN_INFO "exec: %s dyn[%zu] tag=0x%lx val=0x%lx\n",
                       label, di, (uint64_t)dyn.d_tag, dyn.d_un.d_val);
            }
        }
        return;
    }
    printk(KERN_INFO "exec: %s PT_DYNAMIC not found\n", label);
}

static void exec_debug_dump_stack(struct process *proc, uint64_t sp, int argc, int envc, const char *filename) {
    if (!loader_exec_debug_enabled || !path_contains_ld_linux(filename)) {
        return;
    }

    printk(KERN_INFO "exec: stack debug for %s sp=0x%lx argc=%d envc=%d\n",
           filename, sp, argc, envc);

    uint64_t cur = sp;
    uint64_t val = 0;
    if (read_user_u64(proc, cur, &val) < 0) {
        printk(KERN_ERR "exec: stack debug failed reading argc\n");
        return;
    }
    printk(KERN_INFO "exec: stack argc@0x%lx = %lu\n", cur, val);
    cur += 8;

    for (int i = 0; i < argc + 1; i++) {
        uint64_t ptr = 0;
        if (read_user_u64(proc, cur, &ptr) < 0) break;
        printk(KERN_INFO "exec: argv[%d] ptr=0x%lx\n", i, ptr);
        if (ptr && i < argc) {
            char preview[96];
            size_t n = 0;
            memset(preview, 0, sizeof(preview));
            while (n < sizeof(preview) - 1) {
                char c = 0;
                if (read_user_memory(proc, ptr + n, &c, 1) < 0) break;
                preview[n++] = c;
                if (c == '\0') break;
            }
            preview[sizeof(preview) - 1] = '\0';
            printk(KERN_INFO "exec: argv[%d]=\"%s\"\n", i, preview);
        }
        cur += 8;
    }

    for (int i = 0; i < envc + 1; i++) {
        uint64_t ptr = 0;
        if (read_user_u64(proc, cur, &ptr) < 0) break;
        printk(KERN_INFO "exec: envp[%d] ptr=0x%lx\n", i, ptr);
        if (ptr && i < envc) {
            char preview[96];
            size_t n = 0;
            memset(preview, 0, sizeof(preview));
            while (n < sizeof(preview) - 1) {
                char c = 0;
                if (read_user_memory(proc, ptr + n, &c, 1) < 0) break;
                preview[n++] = c;
                if (c == '\0') break;
            }
            preview[sizeof(preview) - 1] = '\0';
            printk(KERN_INFO "exec: envp[%d]=\"%s\"\n", i, preview);
        }
        cur += 8;
    }

    for (int i = 0; i < 24; i++) {
        uint64_t type = 0, value = 0;
        if (read_user_u64(proc, cur, &type) < 0) break;
        if (read_user_u64(proc, cur + 8, &value) < 0) break;
        printk(KERN_INFO "exec: auxv[%d] type=%lu value=0x%lx\n", i, type, value);
        cur += 16;
        if (type == AT_NULL) {
            break;
        }
    }
}

static int write_user_memory(struct process *proc, uint64_t uaddr, const void *src, size_t len) {
    const uint8_t *p = (const uint8_t *)src;
    while (len > 0) {
        uint64_t phys = mmu_resolve(proc->mm->pt, uaddr);
        if (!phys) {
            return -EFAULT;
        }
        /* Debug-only: ensure the mapping is writable user memory before memcpy(). */
        if (loader_exec_debug_enabled) {
            uint64_t flags = mmu_get_flags(proc->mm->pt, uaddr);
            if (!(flags & PTE_PRESENT) || !(flags & PTE_USER) || !(flags & PTE_WRITABLE)) {
                return -EFAULT;
            }
        }
        size_t off = (size_t)(uaddr & (PAGE_SIZE - 1));
        size_t chunk = MIN(len, PAGE_SIZE - off);
        memcpy((uint8_t *)PHYS_TO_VIRT(phys), p, chunk);
        uaddr += chunk;
        p += chunk;
        len -= chunk;
    }
    return 0;
}

static int write_user_u64(struct process *proc, uint64_t uaddr, uint64_t value) {
    return write_user_memory(proc, uaddr, &value, sizeof(value));
}

/*
 * Apply DT_RELR (packed relative relocs). Matches musl ldso/dlstart.c.
 */
static int apply_dt_relr(struct process *proc, uint64_t load_bias, uint64_t relr_va, size_t relr_sz) {
    uint64_t where_va = 0;
    bool have_where = false;

    for (size_t o = 0; o < relr_sz; o += sizeof(uint64_t)) {
        uint64_t rel = 0;
        if (read_user_u64(proc, relr_va + o, &rel) < 0) {
            return -EFAULT;
        }
        if ((rel & 1ULL) == 0) {
            uint64_t addr = load_bias + rel;
            uint64_t val = 0;
            if (read_user_u64(proc, addr, &val) < 0) {
                return -EFAULT;
            }
            val += load_bias;
            if (write_user_u64(proc, addr, val) < 0) {
                return -EFAULT;
            }
            where_va = addr + sizeof(uint64_t);
            have_where = true;
        } else {
            if (!have_where) {
                return -ENOEXEC;
            }
            for (size_t i = 0, bitmap = (size_t)rel; bitmap >>= 1; i++) {
                if (bitmap & 1) {
                    uint64_t addr = where_va + (uint64_t)i * sizeof(uint64_t);
                    uint64_t val = 0;
                    if (read_user_u64(proc, addr, &val) < 0) {
                        return -EFAULT;
                    }
                    val += load_bias;
                    if (write_user_u64(proc, addr, val) < 0) {
                        return -EFAULT;
                    }
                }
            }
            where_va += (uint64_t)(8 * sizeof(uint64_t) - 1) * (uint64_t)sizeof(uint64_t);
        }
    }
    return 0;
}

static int elf_vaddr_to_file_offset(struct elf64_hdr *hdr, struct file *file, uint64_t vaddr,
                                    uint64_t *out_off) {
    for (int i = 0; i < hdr->e_phnum; i++) {
        struct elf64_phdr ph;
        off_t po = hdr->e_phoff + (off_t)i * (off_t)sizeof(ph);
        if (vfs_read(file, &ph, sizeof(ph), &po) != sizeof(ph)) {
            return -EIO;
        }
        if (ph.p_type != PT_LOAD) {
            continue;
        }
        if (vaddr >= ph.p_vaddr && vaddr < ph.p_vaddr + ph.p_memsz) {
            if (vaddr - ph.p_vaddr > ph.p_filesz) {
                return -ENOENT;
            }
            *out_off = ph.p_offset + (vaddr - ph.p_vaddr);
            return 0;
        }
    }
    return -ENOENT;
}

static int find_pt_dynamic(struct elf64_hdr *hdr, struct file *file, uint64_t *dyn_off,
                           size_t *dyn_sz) {
    for (int i = 0; i < hdr->e_phnum; i++) {
        struct elf64_phdr ph;
        off_t po = hdr->e_phoff + (off_t)i * (off_t)sizeof(ph);
        if (vfs_read(file, &ph, sizeof(ph), &po) != sizeof(ph)) {
            return -EIO;
        }
        if (ph.p_type != PT_DYNAMIC) {
            continue;
        }
        if (ph.p_memsz < sizeof(struct elf64_dyn)) {
            return -ENOEXEC;
        }
        *dyn_off = ph.p_offset;
        *dyn_sz = (size_t)ph.p_memsz;
        return 0;
    }
    return -ENOENT;
}

static int read_elf_sym_at(struct file *file, uint64_t sym_file_off, uint32_t idx, uint64_t syment,
                           struct elf64_sym *out) {
    off_t o = (off_t)sym_file_off + (off_t)idx * (off_t)syment;
    return vfs_read(file, out, sizeof(*out), &o) == sizeof(*out) ? 0 : -EIO;
}

static int apply_rela_range(struct process *proc, struct file *file, uint64_t load_bias,
                            uint64_t rela_file_off, size_t relasz, uint64_t relaent,
                            uint64_t symtab_file_off, uint64_t syment) {
    if (relaent != sizeof(struct elf64_rela)) {
        return -ENOEXEC;
    }
    if (relasz % relaent != 0) {
        return -ENOEXEC;
    }
    size_t n = relasz / relaent;
    if (n > 0x100000) {
        return -ENOEXEC;
    }

    for (size_t k = 0; k < n; k++) {
        struct elf64_rela rela;
        off_t ro = (off_t)rela_file_off + (off_t)k * (off_t)relaent;
        if (vfs_read(file, &rela, sizeof(rela), &ro) != sizeof(rela)) {
            return -EIO;
        }
        uint32_t type = ELF64_R_TYPE(rela.r_info);
        uint32_t symidx = ELF64_R_SYM(rela.r_info);
        uint64_t target = load_bias + rela.r_offset;

        switch (type) {
        case R_X86_64_RELATIVE:
            if (write_user_u64(proc, target, load_bias + (uint64_t)rela.r_addend) < 0) {
                return -EFAULT;
            }
            break;
        case R_X86_64_IRELATIVE:
            /* 
             * IRELATIVE requires calling an IFUNC resolver in ring-3.
             * The dynamic loader expects to perform this step itself; writing a
             * placeholder value here can poison early bootstrap state.
             *
             * Therefore we intentionally leave the relocation target untouched.
             */
            break;
        case R_X86_64_GLOB_DAT:
        case R_X86_64_JUMP_SLOT: {
            struct elf64_sym sym;
            if (read_elf_sym_at(file, symtab_file_off, symidx, syment, &sym) < 0) {
                return -EIO;
            }
            if (sym.st_shndx == SHN_UNDEF) {
                EXEC_LOG("exec: interp reloc: undefined sym idx %u\n", symidx);
                return -ENOEXEC;
            }
            uint64_t val = load_bias + sym.st_value;
            if (write_user_u64(proc, target, val) < 0) {
                return -EFAULT;
            }
            break;
        }
        default:
            EXEC_LOG("exec: interp reloc: unsupported type %u\n", type);
            return -ENOEXEC;
        }
    }
    return 0;
}

/*
 * Apply DT_RELR (file) + RELA/JMPREL for the interpreter image only.
 * The main executable is still relocated by ld.so when present.
 */
static int apply_elf_interp_relocations(struct process *proc, struct file *file,
                                      struct elf64_hdr *hdr, uint64_t load_bias) {
    uint64_t dyn_off = 0;
    size_t dyn_sz = 0;
    int dr = find_pt_dynamic(hdr, file, &dyn_off, &dyn_sz);
    if (dr < 0) {
        return -ENOEXEC;
    }

    uint64_t relr_ptr = 0, relrsz = 0;
    uint64_t rela_ptr = 0, relasz = 0, relaent = sizeof(struct elf64_rela);
    uint64_t jmprel_ptr = 0, pltrelsz = 0;
    uint64_t pltrel = 0;
    uint64_t symtab_ptr = 0;
    uint64_t syment = sizeof(struct elf64_sym);
    int err;

    for (size_t di = 0; di * sizeof(struct elf64_dyn) < dyn_sz; di++) {
        struct elf64_dyn d;
        off_t o = (off_t)dyn_off + (off_t)di * (off_t)sizeof(d);
        if (vfs_read(file, &d, sizeof(d), &o) != sizeof(d)) {
            return -EIO;
        }
        int64_t tag = d.d_tag;
        if (tag == DT_NULL) {
            break;
        }
        switch (tag) {
        case DT_RELR:
            relr_ptr = d.d_un.d_ptr;
            break;
        case DT_RELRSZ:
            relrsz = d.d_un.d_val;
            break;
        case DT_RELA:
            rela_ptr = d.d_un.d_ptr;
            break;
        case DT_RELASZ:
            relasz = d.d_un.d_val;
            break;
        case DT_RELAENT:
            relaent = d.d_un.d_val;
            break;
        case DT_JMPREL:
            jmprel_ptr = d.d_un.d_ptr;
            break;
        case DT_PLTRELSZ:
            pltrelsz = d.d_un.d_val;
            break;
        case DT_PLTREL:
            pltrel = d.d_un.d_val;
            break;
        case DT_SYMTAB:
            symtab_ptr = d.d_un.d_ptr;
            break;
        case DT_SYMENT:
            syment = d.d_un.d_val;
            break;
        default:
            break;
        }
    }

    if (relrsz > 0) {
        uint64_t relr_va = load_bias + relr_ptr;
        err = apply_dt_relr(proc, load_bias, relr_va, relrsz);
        if (err < 0) {
            return err;
        }
    }

    if (symtab_ptr == 0) {
        return -ENOEXEC;
    }
    uint64_t symtab_off = 0;
    if (elf_vaddr_to_file_offset(hdr, file, symtab_ptr, &symtab_off) < 0) {
        return -ENOEXEC;
    }

    if (relasz > 0) {
        uint64_t rela_off = 0;
        if (elf_vaddr_to_file_offset(hdr, file, rela_ptr, &rela_off) < 0) {
            return -ENOEXEC;
        }
        err = apply_rela_range(proc, file, load_bias, rela_off, relasz, relaent, symtab_off, syment);
        if (err < 0) {
            return err;
        }
    }

    if (pltrelsz > 0) {
        if (pltrel != 0 && pltrel != DT_RELA) {
            return -ENOEXEC;
        }
        uint64_t jmp_off = 0;
        if (elf_vaddr_to_file_offset(hdr, file, jmprel_ptr, &jmp_off) < 0) {
            return -ENOEXEC;
        }
        err = apply_rela_range(proc, file, load_bias, jmp_off, pltrelsz, relaent, symtab_off, syment);
        if (err < 0) {
            return err;
        }
    }

    return 0;
}

static int read_elf_interp_path(struct file *file, struct elf64_hdr *hdr,
                                char *buf, size_t buflen) {
    struct elf64_phdr phdr;
    int ret;

    if (!buf || buflen == 0) {
        return -EINVAL;
    }
    buf[0] = '\0';

    for (int i = 0; i < hdr->e_phnum; i++) {
        off_t phoff = hdr->e_phoff + (i * sizeof(phdr));
        ret = vfs_read(file, &phdr, sizeof(phdr), &phoff);
        if (ret != (int)sizeof(phdr)) {
            return -EIO;
        }
        if (phdr.p_type != PT_INTERP) {
            continue;
        }
        if (phdr.p_filesz == 0 || phdr.p_filesz > buflen) {
            return -ENOEXEC;
        }
        off_t off = phdr.p_offset;
        ret = vfs_read(file, buf, phdr.p_filesz, &off);
        if (ret != (int)phdr.p_filesz) {
            return -EIO;
        }
        buf[phdr.p_filesz - 1] = '\0';
        return 1;
    }

    return 0;
}

/*
 * Validate ELF header
 */
static int validate_elf(struct elf64_hdr *hdr) {
    /* Check magic */
    if (*(uint32_t *)hdr->e_ident != ELF_MAGIC) {
        return -ENOEXEC;
    }
    
    /* Check class (64-bit) */
    if (hdr->e_ident[4] != 2) {
        return -ENOEXEC;
    }
    
    /* Check endianness (little) */
    if (hdr->e_ident[5] != 1) {
        return -ENOEXEC;
    }
    
    /* Check type */
    if (hdr->e_type != ET_EXEC && hdr->e_type != ET_DYN) {
        return -ENOEXEC;
    }
    
    /* Check machine */
    if (hdr->e_machine != EM_X86_64) {
        return -ENOEXEC;
    }
    
    return 0;
}

/*
 * Dynamic PT_INTERP execution is gated behind EXPERIMENTAL_DYNAMIC_ELF.
 * Default builds remain static-first for release stability.
 */
static int validate_exec_format(struct file *file, struct elf64_hdr *hdr) {
    struct elf64_phdr phdr;
    bool has_interp = false;
    int ret;

    for (int i = 0; i < hdr->e_phnum; i++) {
        off_t offset = hdr->e_phoff + (i * sizeof(phdr));
        ret = vfs_read(file, &phdr, sizeof(phdr), &offset);
        if (ret != sizeof(phdr)) {
            return -EIO;
        }

        if (phdr.p_type == PT_INTERP) {
            has_interp = true;
        }
    }

    if (has_interp && !CONFIG_EXPERIMENTAL_DYNAMIC_ELF) {
        printk(KERN_WARNING
               "exec: PT_INTERP binary rejected (experimental dynamic ELF disabled)\n");
        return -ENOEXEC;
    }

    return 0;
}

/*
 * Load ELF segments
 */
static int load_elf_segments(struct process *proc, struct file *file,
                             struct elf64_hdr *hdr, struct exec_image_info *img,
                             uint64_t et_dyn_bias) {
    struct elf64_phdr phdr;
    uint64_t brk_start = 0;
    uint64_t load_bias = (hdr->e_type == ET_DYN) ? et_dyn_bias : 0;
    bool phdr_found = false;
    int ret;
    
    for (int i = 0; i < hdr->e_phnum; i++) {
        /* Read program header */
        off_t offset = hdr->e_phoff + (i * sizeof(phdr));
        ret = vfs_read(file, &phdr, sizeof(phdr), &offset);
        if (ret != sizeof(phdr)) {
            return -EIO;
        }
        
        if (phdr.p_type == PT_PHDR) {
            img->phdr = phdr.p_vaddr + load_bias;
            phdr_found = true;
        }

        if (phdr.p_type == PT_TLS) {
            /* Map TLS template like PT_LOAD; ld.so / libc need this mapped before early %fs-relative access. */
            if (phdr.p_filesz > phdr.p_memsz) {
                return -ENOEXEC;
            }
            if (phdr.p_memsz == 0) {
                continue;
            }
            if (load_bias != 0 && phdr.p_vaddr > (~(uint64_t)0 - load_bias)) {
                return -ENOEXEC;
            }
            uint64_t seg_vaddr_tls = phdr.p_vaddr + load_bias;
            if (phdr.p_memsz > (~(uint64_t)0 - seg_vaddr_tls)) {
                return -ENOEXEC;
            }
            EXEC_LOG("exec: PT_TLS[%d] vaddr=0x%lx filesz=%lu memsz=%lu align=%lu flags=0x%x\n",
                     i, seg_vaddr_tls, phdr.p_filesz, phdr.p_memsz, phdr.p_align, phdr.p_flags);

            uint64_t start_tls = ALIGN_DOWN(seg_vaddr_tls, PAGE_SIZE);
            uint64_t end_tls = ALIGN_UP(seg_vaddr_tls + phdr.p_memsz, PAGE_SIZE);
            if (end_tls <= start_tls) {
                return -ENOEXEC;
            }

            int prot_tls = 0;
            if (phdr.p_flags & PF_R) {
                prot_tls |= PROT_READ;
            }
            if (phdr.p_flags & PF_W) {
                prot_tls |= PROT_WRITE;
            }
            if (phdr.p_flags & PF_X) {
                prot_tls |= PROT_EXEC;
            }
            if (prot_tls == 0) {
                prot_tls = PROT_READ | PROT_WRITE;
            }

            void *addr_tls = vmm_mmap(proc->mm, (void *)start_tls, end_tls - start_tls,
                                      prot_tls, MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS,
                                      NULL, 0);
            if (addr_tls == MAP_FAILED) {
                return -ENOMEM;
            }
            for (uint64_t page_vaddr = start_tls; page_vaddr < end_tls; page_vaddr += PAGE_SIZE) {
                uint64_t phys = pmm_alloc_page();
                if (!phys) {
                    return -ENOMEM;
                }
                memset(PHYS_TO_VIRT(phys), 0, PAGE_SIZE);
                mmu_map(proc->mm->pt, page_vaddr, phys, vmm_prot_to_pte_flags(prot_tls));
            }
            if (phdr.p_filesz > 0) {
                uint64_t copied = 0;
                while (copied < phdr.p_filesz) {
                    uint64_t dst_vaddr = seg_vaddr_tls + copied;
                    uint64_t phys = mmu_resolve(proc->mm->pt, dst_vaddr);
                    if (!phys) {
                        return -EFAULT;
                    }
                    size_t page_off = (size_t)(dst_vaddr & (PAGE_SIZE - 1));
                    size_t chunk = MIN((uint64_t)(PAGE_SIZE - page_off), phdr.p_filesz - copied);
                    void *dst = (uint8_t *)PHYS_TO_VIRT(phys) + page_off;
                    uint64_t read_off_u = phdr.p_offset + copied;
                    if (read_off_u < phdr.p_offset) {
                        return -ENOEXEC;
                    }
                    off_t read_off = (off_t)read_off_u;
                    ret = vfs_read(file, dst, chunk, &read_off);
                    if (ret != (int)chunk) {
                        return (ret < 0) ? ret : -EIO;
                    }
                    copied += chunk;
                }
            }

            uint64_t tls_align = phdr.p_align;
            if (tls_align < sizeof(uint64_t)) {
                tls_align = sizeof(uint64_t);
            }
            img->tls_base = ALIGN_UP(seg_vaddr_tls + phdr.p_memsz, tls_align);
            img->has_tls = true;

            if (end_tls > brk_start) {
                brk_start = end_tls;
            }
            continue;
        }

        if (phdr.p_type != PT_LOAD) {
            continue;
        }
        /* Basic ELF sanity: p_filesz must not exceed p_memsz. */
        if (phdr.p_filesz > phdr.p_memsz) {
            return -ENOEXEC;
        }
        if (phdr.p_memsz == 0) {
            /* Nothing to map/copy. */
            continue;
        }
        /* Overflow-safe calculations for ET_DYN load bias. */
        if (load_bias != 0 && phdr.p_vaddr > (~(uint64_t)0 - load_bias)) {
            return -ENOEXEC;
        }
        uint64_t seg_vaddr = phdr.p_vaddr + load_bias;
        if (phdr.p_memsz > (~(uint64_t)0 - seg_vaddr)) {
            return -ENOEXEC;
        }
        EXEC_LOG("exec: PT_LOAD[%d] vaddr=0x%lx filesz=%lu memsz=%lu flags=0x%x\n",
                 i, seg_vaddr, phdr.p_filesz, phdr.p_memsz, phdr.p_flags);
        
        /* Calculate memory region */
        uint64_t start = ALIGN_DOWN(seg_vaddr, PAGE_SIZE);
        uint64_t end = ALIGN_UP(seg_vaddr + phdr.p_memsz, PAGE_SIZE);
        if (end <= start) {
            return -ENOEXEC;
        }
        
        /* Set up protection flags */
        uint32_t vma_flags = VMA_PRIVATE;
        int prot = 0;
        
        if (phdr.p_flags & PF_R) {
            prot |= PROT_READ;
            vma_flags |= VMA_READ;
        }
        if (phdr.p_flags & PF_W) {
            prot |= PROT_WRITE;
            vma_flags |= VMA_WRITE;
        }
        if (phdr.p_flags & PF_X) {
            prot |= PROT_EXEC;
            vma_flags |= VMA_EXEC;
        }
        
        /* Map the region */
        void *addr = vmm_mmap(proc->mm, (void *)start, end - start,
                              prot, MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS,
                              NULL, 0);
        if (addr == MAP_FAILED) {
            return -ENOMEM;
        }

        /* Back with physical pages (zero-filled) for full memsz coverage. */
        for (uint64_t page_vaddr = start; page_vaddr < end; page_vaddr += PAGE_SIZE) {
            uint64_t phys = pmm_alloc_page();
            if (!phys) {
                return -ENOMEM;
            }
            memset(PHYS_TO_VIRT(phys), 0, PAGE_SIZE);
            mmu_map(proc->mm->pt, page_vaddr, phys, vmm_prot_to_pte_flags(prot));
        }

        /* Copy file bytes to exact virtual load address (handles unaligned segments). */
        if (phdr.p_filesz > 0) {
            uint64_t copied = 0;
            while (copied < phdr.p_filesz) {
                uint64_t dst_vaddr = seg_vaddr + copied;
                uint64_t phys = mmu_resolve(proc->mm->pt, dst_vaddr);
                if (!phys) {
                    return -EFAULT;
                }

                size_t page_off = (size_t)(dst_vaddr & (PAGE_SIZE - 1));
                size_t chunk = MIN((uint64_t)(PAGE_SIZE - page_off), phdr.p_filesz - copied);
                void *dst = (uint8_t *)PHYS_TO_VIRT(phys) + page_off;

                uint64_t read_off_u = phdr.p_offset + copied;
                if (read_off_u < phdr.p_offset) {
                    return -ENOEXEC;
                }
                off_t read_off = (off_t)read_off_u;
                ret = vfs_read(file, dst, chunk, &read_off);
                if (ret != (int)chunk) {
                    return (ret < 0) ? ret : -EIO;
                }

                copied += chunk;
            }
        }
        
        /* Track brk */
        if (end > brk_start) {
            brk_start = end;
        }
    }

    /* Set up heap */
    proc->mm->brk_start = ALIGN_UP(brk_start, PAGE_SIZE);
    proc->mm->brk_end = proc->mm->brk_start;
    
    img->entry = hdr->e_entry + load_bias;
    img->load_bias = load_bias;
    img->phent = hdr->e_phentsize;
    img->phnum = hdr->e_phnum;
    if (!phdr_found) {
        img->phdr = load_bias + hdr->e_phoff;
    }
    
    return 0;
}

/*
 * One page immediately below the fixed user stack. Many glibc PIEs and ld-linux omit PT_TLS;
 * TLS is described via DT_TLS and set up by the dynamic linker, but ld.so may touch %fs:offset
 * before arch_prctl. Provide a minimal RW page so FS_BASE+offset is mapped.
 *
 * Note: imported glibc ld.so may still fault early for other reasons (e.g. mov 0x8(%rdx) with
 * %rdx==0 in .text) if relocations / link-map bootstrap are incomplete — that is separate from
 * IA32_FS_BASE.
 */
#define USER_TLS_STUB_VADDR (USER_STACK_TOP - CONFIG_USER_STACK_SIZE - PAGE_SIZE)

static int map_user_tls_stub(struct process *proc) {
    void *m = vmm_mmap(proc->mm, (void *)USER_TLS_STUB_VADDR, PAGE_SIZE,
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS,
                       NULL, 0);
    if (m == MAP_FAILED) {
        return -ENOMEM;
    }
    uint64_t phys = pmm_alloc_page();
    if (!phys) {
        return -ENOMEM;
    }
    /* Minimal x86_64 glibc TLS head initialization.
     * glibc reads header.tcb/header.self via %fs offsets very early (before TLS
     * dtv/dtv handling is set up). With no PT_TLS in main/interp we provide
     * a stub page, but it must at least return a non-NULL thread descriptor
     * through THREAD_SELF. */
    uint8_t *stub = (uint8_t *)PHYS_TO_VIRT(phys);
    memset(stub, 0, PAGE_SIZE);
    /* tcbhead_t (glibc sysdeps/x86_64/nptl/tls.h): tcb @0, dtv @8, self @0x10.
     * Offset 0x68 is __private_tm[3] (TM ABI reservation), not struct pthread::rtld_catch.
     * THREAD_SELF uses %fs:offsetof(tcbhead_t,self)=0x10. */
    *(uint64_t *)(stub + 0x0) = USER_TLS_STUB_VADDR;
    *(uint64_t *)(stub + 0x8) = 0; /* dtv filled by ld.so after TLS init; keep 0 for bootstrap */
    *(uint64_t *)(stub + 0x10) = USER_TLS_STUB_VADDR;
    mmu_map(proc->mm->pt, USER_TLS_STUB_VADDR, phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    return 0;
}

/*
 * Set up user stack with arguments and environment
 */
static int setup_user_stack(struct process *proc, char *const argv[],
                            char *const envp[], const char *filename,
                            const struct exec_image_info *img, uint64_t *sp,
                            int at_secure) {
    /* Allocate stack region */
    uint64_t stack_top = USER_STACK_TOP;
    uint64_t stack_size = CONFIG_USER_STACK_SIZE;
    uint64_t stack_bottom = stack_top - stack_size;
    
    void *stack = vmm_mmap(proc->mm, (void *)stack_bottom, stack_size,
                           PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS | MAP_GROWSDOWN,
                           NULL, 0);
    if (stack == MAP_FAILED) {
        printk(KERN_ERR "exec: stack mmap failed at [0x%lx..0x%lx)\n", stack_bottom, stack_top);
        return -ENOMEM;
    }
    
    /* Pre-map full stack range.
     * We currently rely on eager stack backing (no demand stack growth fault path),
     * so user buffers placed deeper in stack must be valid for copy_to_user/copy_from_user. */
    for (uint64_t addr = stack_bottom; addr < stack_top; addr += PAGE_SIZE) {
        uint64_t phys = pmm_alloc_page();
        if (!phys) {
            return -ENOMEM;
        }
        memset(PHYS_TO_VIRT(phys), 0, PAGE_SIZE);
        mmu_map(proc->mm->pt, addr, phys, PTE_PRESENT | PTE_WRITABLE | PTE_USER);
    }
    
    int argc = 0, envc = 0, auxc = 0;
    uint64_t argv_ptrs[MAX_ARGC];
    uint64_t envp_ptrs[MAX_ARGC];
    struct auxv_pair auxv[24];
    uint64_t random_ptr = 0;
    uint64_t execfn_ptr = 0;
    uint64_t platform_ptr = 0;
    static const uint8_t random_seed[16] = {
        0x41, 0x78, 0x69, 0x6f, 0x6d, 0x2d, 0x4f, 0x53,
        0x2d, 0x72, 0x6e, 0x67, 0x2d, 0x31, 0x36, 0x21
    };
    static const char platform_name[] = "x86_64";
    memset(argv_ptrs, 0, sizeof(argv_ptrs));
    memset(envp_ptrs, 0, sizeof(envp_ptrs));

    if (argv) {
        for (argc = 0; argc < MAX_ARGC; argc++) {
            if (!argv[argc]) break;
        }
        if (argc == MAX_ARGC) return -E2BIG;
    }
    if (envp) {
        for (envc = 0; envc < MAX_ARGC; envc++) {
            if (!envp[envc]) break;
        }
        if (envc == MAX_ARGC) return -E2BIG;
    }

    uint64_t sp_val = stack_top;

    /* Copy argv/env strings to top of stack (high -> low). */
    for (int i = envc - 1; i >= 0; i--) {
        size_t len = strlen(envp[i]) + 1;
        if (len > MAX_ARG_STRLEN) {
            return -E2BIG;
        }
        if (sp_val < stack_bottom + len) {
            return -E2BIG;
        }
        sp_val -= len;
        if (write_user_memory(proc, sp_val, envp[i], len) < 0) {
            return -EFAULT;
        }
        envp_ptrs[i] = sp_val;
    }

    for (int i = argc - 1; i >= 0; i--) {
        size_t len = strlen(argv[i]) + 1;
        if (len > MAX_ARG_STRLEN) {
            return -E2BIG;
        }
        if (sp_val < stack_bottom + len) {
            return -E2BIG;
        }
        sp_val -= len;
        if (write_user_memory(proc, sp_val, argv[i], len) < 0) {
            return -EFAULT;
        }
        argv_ptrs[i] = sp_val;
    }

    if (filename) {
        size_t len = strlen(filename) + 1;
        if (sp_val < stack_bottom + len) return -E2BIG;
        sp_val -= len;
        if (write_user_memory(proc, sp_val, filename, len) < 0) return -EFAULT;
        execfn_ptr = sp_val;
    }

    if (sp_val < stack_bottom + sizeof(random_seed)) return -E2BIG;
    sp_val -= sizeof(random_seed);
    if (write_user_memory(proc, sp_val, random_seed, sizeof(random_seed)) < 0) return -EFAULT;
    random_ptr = sp_val;
    if (sp_val < stack_bottom + sizeof(platform_name)) return -E2BIG;
    sp_val -= sizeof(platform_name);
    if (write_user_memory(proc, sp_val, platform_name, sizeof(platform_name)) < 0) return -EFAULT;
    platform_ptr = sp_val;

    /* Align stack to 16-byte boundary. */
    sp_val &= ~0xFULL;

    auxv[auxc++] = (struct auxv_pair){ AT_PHDR,   img ? img->phdr : 0 };
    auxv[auxc++] = (struct auxv_pair){ AT_PHENT,  img ? img->phent : sizeof(struct elf64_phdr) };
    auxv[auxc++] = (struct auxv_pair){ AT_PHNUM,  img ? img->phnum : 0 };
    auxv[auxc++] = (struct auxv_pair){ AT_PAGESZ, PAGE_SIZE };
    auxv[auxc++] = (struct auxv_pair){ AT_BASE,   img ? img->load_bias : 0 };
    auxv[auxc++] = (struct auxv_pair){ AT_FLAGS,  0 };
    auxv[auxc++] = (struct auxv_pair){ AT_ENTRY,  img ? img->entry : 0 };
    auxv[auxc++] = (struct auxv_pair){ AT_UID,    (proc && proc->cred) ? proc->cred->uid  : 0 };
    auxv[auxc++] = (struct auxv_pair){ AT_EUID,   (proc && proc->cred) ? proc->cred->euid : 0 };
    auxv[auxc++] = (struct auxv_pair){ AT_GID,    (proc && proc->cred) ? proc->cred->gid  : 0 };
    auxv[auxc++] = (struct auxv_pair){ AT_EGID,   (proc && proc->cred) ? proc->cred->egid : 0 };
    auxv[auxc++] = (struct auxv_pair){ AT_PLATFORM, platform_ptr };
    auxv[auxc++] = (struct auxv_pair){ AT_HWCAP, HWCAP_X86_64_BASELINE };
    auxv[auxc++] = (struct auxv_pair){ AT_CLKTCK, 100 };
    auxv[auxc++] = (struct auxv_pair){ AT_SECURE, at_secure ? 1 : 0 };
    auxv[auxc++] = (struct auxv_pair){ AT_RANDOM, random_ptr };
    auxv[auxc++] = (struct auxv_pair){ AT_HWCAP2, HWCAP2_X86_64_BASELINE };
    auxv[auxc++] = (struct auxv_pair){ AT_EXECFN, execfn_ptr };
    auxv[auxc++] = (struct auxv_pair){ AT_SYSINFO_EHDR, 0 };
    auxv[auxc++] = (struct auxv_pair){ AT_NULL,   0 };

    /* Keep final initial %rsp 16-byte aligned. */
    int stack_slots = 1 + (argc + 1) + (envc + 1) + (auxc * 2);
    if ((stack_slots & 1) != 0) {
        sp_val -= 8;
        if (write_user_u64(proc, sp_val, 0) < 0) return -EFAULT;
    }

    for (int i = auxc - 1; i >= 0; i--) {
        sp_val -= 8;
        if (write_user_u64(proc, sp_val, auxv[i].value) < 0) return -EFAULT;
        sp_val -= 8;
        if (write_user_u64(proc, sp_val, auxv[i].type) < 0) return -EFAULT;
    }

    /* envp[] pointers + NULL */
    sp_val -= 8;
    if (write_user_u64(proc, sp_val, 0) < 0) return -EFAULT;
    for (int i = envc - 1; i >= 0; i--) {
        sp_val -= 8;
        if (write_user_u64(proc, sp_val, envp_ptrs[i]) < 0) return -EFAULT;
    }

    /* argv[] pointers + NULL */
    sp_val -= 8;
    if (write_user_u64(proc, sp_val, 0) < 0) return -EFAULT;
    for (int i = argc - 1; i >= 0; i--) {
        sp_val -= 8;
        if (write_user_u64(proc, sp_val, argv_ptrs[i]) < 0) return -EFAULT;
    }

    /* argc */
    sp_val -= 8;
    if (write_user_u64(proc, sp_val, (uint64_t)argc) < 0) return -EFAULT;
    
    *sp = sp_val;
    proc->mm->start_stack = sp_val;
    EXEC_LOG("exec: user stack ready top=0x%lx sp=0x%lx argc=%d envc=%d\n",
             stack_top, sp_val, argc, envc);
    if (loader_exec_debug_enabled) {
        exec_debug_dump_stack(proc, sp_val, argc, envc, filename);
    }
    
    return 0;
}

/*
 * do_execve - Execute a new program
 */
int do_execve(const char *filename, char *const argv[], char *const envp[]) {
    struct process *proc = current;
    struct file *file;
    struct file *interp_file = NULL;
    struct elf64_hdr hdr;
    struct elf64_hdr interp_hdr;
    uint64_t sp;
    int ret;
    char interp_path[PATH_MAX];
    struct exec_image_info img;
    struct exec_image_info main_img;
    struct exec_image_info interp_img;
    struct exec_image_info stack_img;
    uint64_t user_entry = 0;
    bool use_interp = false;
    bool apply_setuid = false;
    bool apply_setgid = false;
    int at_secure = 0;
    mode_t exec_mode = 0;
    uid_t exec_uid = 0;
    gid_t exec_gid = 0;
    memset(&img, 0, sizeof(img));
    memset(&main_img, 0, sizeof(main_img));
    memset(&interp_img, 0, sizeof(interp_img));
    memset(&stack_img, 0, sizeof(stack_img));
    
    EXEC_LOG("exec: begin %s pid=%d\n", filename, proc ? proc->pid : -1);
    
    /* Open executable */
    EXEC_LOG("exec: opening %s\n", filename);
    file = vfs_open(filename, O_RDONLY, 0);
    if (IS_ERR(file)) {
        long err = PTR_ERR(file);
        /* PATH probing frequently hits ENOENT; keep that quiet. */
        if (err != -ENOENT) {
            printk(KERN_ERR "exec: open failed for %s: %ld\n", filename, err);
        } else {
            EXEC_LOG("exec: open failed for %s: %ld\n", filename, err);
        }
        return err;
    }
    EXEC_LOG("exec: opened %s\n", filename);

    if (!file->f_dentry || !file->f_dentry->d_inode) {
        vfs_close(file);
        return -ENOENT;
    }
    if (file->f_dentry->d_inode->i_op && file->f_dentry->d_inode->i_op->permission) {
        ret = file->f_dentry->d_inode->i_op->permission(file->f_dentry->d_inode, MAY_EXEC);
    } else {
        ret = generic_permission(file->f_dentry->d_inode, MAY_EXEC);
    }
    if (ret < 0) {
        vfs_close(file);
        return ret;
    }

    /* Exec-time setuid/setgid handling (real UNIX semantics baseline).
     * We preserve real uid/gid (cred->uid/gid) and update effective/fs ids. */
    if (proc && proc->cred && file->f_dentry && file->f_dentry->d_inode) {
        exec_mode = file->f_dentry->d_inode->i_mode;
        exec_uid = file->f_dentry->d_inode->i_uid;
        exec_gid = file->f_dentry->d_inode->i_gid;

        const bool has_group_or_other_write = (exec_mode & 022) != 0;
        const bool caller_is_exec_owner = proc->cred->uid == exec_uid;
        const bool caller_is_root = proc->cred->euid == 0;

        if ((exec_mode & S_ISUID) != 0) {
            /* Very small “unsafe executable” defense: ignore setuid if it is group/other writable
             * and the caller is not the executable owner (unless the caller is already root). */
            if (!has_group_or_other_write || caller_is_exec_owner || caller_is_root) {
                apply_setuid = true;
                at_secure = 1;
            }
        }
        if ((exec_mode & S_ISGID) != 0) {
            if (!has_group_or_other_write || caller_is_exec_owner || caller_is_root) {
                apply_setgid = true;
                at_secure = 1;
            }
        }
    }
    
    /* Read ELF header */
    off_t offset = 0;
    EXEC_LOG("exec: reading ELF header\n");
    ret = vfs_read(file, &hdr, sizeof(hdr), &offset);
    if (ret < 0) {
        printk(KERN_ERR "exec: failed reading executable header ret=%d\n", ret);
        vfs_close(file);
        return ret;
    }
    if (ret != sizeof(hdr)) {
        char shebang[128];
        int n = ret;
        if (n > (int)sizeof(shebang) - 1) {
            n = (int)sizeof(shebang) - 1;
        }
        memcpy(shebang, &hdr, (size_t)n);
        if (n > 2) {
            shebang[n] = '\0';
            int shebang_off = 0;
            if (n >= 3 &&
                (uint8_t)shebang[0] == 0xEF &&
                (uint8_t)shebang[1] == 0xBB &&
                (uint8_t)shebang[2] == 0xBF) {
                shebang_off = 3;
            }
            while (shebang_off < n &&
                   (shebang[shebang_off] == ' ' || shebang[shebang_off] == '\t' ||
                    shebang[shebang_off] == '\r' || shebang[shebang_off] == '\n')) {
                shebang_off++;
            }
            bool shebang_ok = false;
            int shebang_i = shebang_off;
            if ((shebang_off + 1) < n &&
                shebang[shebang_off] == '#' &&
                shebang[shebang_off + 1] == '!') {
                shebang_ok = true;
                shebang_i = shebang_off + 2;
            } else if ((shebang_off + 1) < n &&
                       shebang[shebang_off] == '!' &&
                       shebang[shebang_off + 1] == '/') {
                /* Accept !/bin/sh as a tolerant shebang fallback. */
                shebang_ok = true;
                shebang_i = shebang_off + 1;
            }
            if (shebang_ok) {
                char interp[PATH_MAX];
                char optarg[64];
                int i = shebang_i, ip = 0, op = 0;
                bool has_optarg = false;
                while (shebang[i] == ' ' || shebang[i] == '\t') i++;
                while (shebang[i] && shebang[i] != ' ' && shebang[i] != '\t' &&
                       shebang[i] != '\n' && ip < (int)sizeof(interp) - 1) {
                    interp[ip++] = shebang[i++];
                }
                interp[ip] = '\0';
                while (shebang[i] == ' ' || shebang[i] == '\t') i++;
                while (shebang[i] && shebang[i] != '\n' && shebang[i] != '\r' &&
                       op < (int)sizeof(optarg) - 1) {
                    optarg[op++] = shebang[i++];
                }
                while (op > 0 && (optarg[op - 1] == ' ' || optarg[op - 1] == '\t')) {
                    op--;
                }
                optarg[op] = '\0';
                has_optarg = (op > 0);
                if (ip > 0) {
                    (void)optarg;
                    (void)has_optarg;
                    vfs_close(file);
                    EXEC_LOG("exec: short-header script '%s' detected (interp=%s), returning ENOEXEC for shell fallback\n",
                             filename, interp);
                    return -ENOEXEC;
                }
            }
        }
        printk(KERN_ERR "exec: unsupported executable header ret=%d expected=%lu\n",
               ret, sizeof(hdr));
        vfs_close(file);
        return -ENOEXEC;
    }
    EXEC_LOG("exec: ELF header read complete\n");
    
    /* Validate ELF */
    ret = validate_elf(&hdr);
    if (ret < 0) {
        char shebang[128];
        off_t soff = 0;
        int n = vfs_read(file, shebang, sizeof(shebang) - 1, &soff);
        if (n > 2) {
            shebang[n] = '\0';
            int shebang_off = 0;
            if (n >= 3 &&
                (uint8_t)shebang[0] == 0xEF &&
                (uint8_t)shebang[1] == 0xBB &&
                (uint8_t)shebang[2] == 0xBF) {
                shebang_off = 3;
            }
            while (shebang_off < n &&
                   (shebang[shebang_off] == ' ' || shebang[shebang_off] == '\t' ||
                    shebang[shebang_off] == '\r' || shebang[shebang_off] == '\n')) {
                shebang_off++;
            }
            bool shebang_ok = false;
            int shebang_i = shebang_off;
            if ((shebang_off + 1) < n &&
                shebang[shebang_off] == '#' &&
                shebang[shebang_off + 1] == '!') {
                shebang_ok = true;
                shebang_i = shebang_off + 2;
            } else if ((shebang_off + 1) < n &&
                       shebang[shebang_off] == '!' &&
                       shebang[shebang_off + 1] == '/') {
                shebang_ok = true;
                shebang_i = shebang_off + 1;
            }
            if (shebang_ok) {
                char interp[PATH_MAX];
                char optarg[64];
                int i = shebang_i, ip = 0, op = 0;
                bool has_optarg = false;
                while (shebang[i] == ' ' || shebang[i] == '\t') i++;
                while (shebang[i] && shebang[i] != ' ' && shebang[i] != '\t' &&
                       shebang[i] != '\n' && ip < (int)sizeof(interp) - 1) {
                    interp[ip++] = shebang[i++];
                }
                interp[ip] = '\0';
                while (shebang[i] == ' ' || shebang[i] == '\t') i++;
                while (shebang[i] && shebang[i] != '\n' && shebang[i] != '\r' &&
                       op < (int)sizeof(optarg) - 1) {
                    optarg[op++] = shebang[i++];
                }
                while (op > 0 && (optarg[op - 1] == ' ' || optarg[op - 1] == '\t')) {
                    op--;
                }
                optarg[op] = '\0';
                has_optarg = (op > 0);
                if (ip > 0) {
                    (void)optarg;
                    (void)has_optarg;
                    vfs_close(file);
                    EXEC_LOG("exec: script '%s' detected (interp=%s), returning ENOEXEC for shell fallback\n",
                             filename, interp);
                    return -ENOEXEC;
                }
            }
        }
        vfs_close(file);
        return ret;
    }
    EXEC_LOG("exec: ELF validated entry=0x%lx phnum=%u\n", hdr.e_entry, hdr.e_phnum);

    ret = read_elf_interp_path(file, &hdr, interp_path, sizeof(interp_path));
    if (ret < 0) {
        vfs_close(file);
        return ret;
    }
    if (ret > 0 && strcmp(filename, interp_path) != 0) {
        if (!CONFIG_EXPERIMENTAL_DYNAMIC_ELF) {
            printk(KERN_WARNING
                   "exec: rejecting dynamic ELF %s (set EXPERIMENTAL_DYNAMIC_ELF=1 to enable)\n",
                   filename);
            vfs_close(file);
            return -ENOEXEC;
        }
        use_interp = true;
        EXEC_LOG("exec: PT_INTERP detected, loading interpreter %s\n", interp_path);
        interp_file = vfs_open(interp_path, O_RDONLY, 0);
        if (IS_ERR(interp_file)) {
            vfs_close(file);
            return PTR_ERR(interp_file);
        }

        off_t ioff = 0;
        ret = vfs_read(interp_file, &interp_hdr, sizeof(interp_hdr), &ioff);
        if (ret != sizeof(interp_hdr)) {
            vfs_close(interp_file);
            vfs_close(file);
            return -EIO;
        }

        ret = validate_elf(&interp_hdr);
        if (ret < 0) {
            vfs_close(interp_file);
            vfs_close(file);
            return ret;
        }
    }

    ret = validate_exec_format(file, &hdr);
    if (ret < 0) {
        printk(KERN_ERR "exec: unsupported ELF format for %s\n",
               filename);
        vfs_close(file);
        return ret;
    }
    
    /* Point of no return - start modifying process */
    uint64_t old_fs_base = proc ? proc->fs_base : 0;

    /* Create new address space */
    struct address_space *new_mm = vmm_create_address_space();
    if (!new_mm) {
        vfs_close(file);
        return -ENOMEM;
    }
    
    struct address_space *old_mm = proc->mm;
    proc->mm = new_mm;
    
    /* Load ELF segments */
    if (use_interp) {
        ret = load_elf_segments(proc, file, &hdr, &main_img, ET_DYN_MAIN_BIAS);
        if (ret < 0) {
            proc->mm = old_mm;
            vmm_destroy_address_space(new_mm);
            vfs_close(interp_file);
            vfs_close(file);
            return ret;
        }
        ret = load_elf_segments(proc, interp_file, &interp_hdr, &interp_img, ET_DYN_INTERP_BIAS);
        if (ret < 0) {
            proc->mm = old_mm;
            vmm_destroy_address_space(new_mm);
            vfs_close(interp_file);
            vfs_close(file);
            return ret;
        }
        ret = apply_elf_interp_relocations(proc, interp_file, &interp_hdr, interp_img.load_bias);
        if (ret < 0) {
            proc->mm = old_mm;
            vmm_destroy_address_space(new_mm);
            vfs_close(interp_file);
            vfs_close(file);
            return ret;
        }
        EXEC_LOG("exec: segments loaded for %s and interpreter %s\n", filename, interp_path);
        if (loader_exec_debug_enabled && path_contains_ld_linux(filename)) {
            printk(KERN_INFO "exec: main image base=0x%lx entry=0x%lx phdr=0x%lx phnum=%lu phent=%lu\n",
                   main_img.load_bias, main_img.entry, main_img.phdr, main_img.phnum, main_img.phent);
            printk(KERN_INFO "exec: interp image base=0x%lx entry=0x%lx phdr=0x%lx phnum=%lu phent=%lu\n",
                   interp_img.load_bias, interp_img.entry, interp_img.phdr, interp_img.phnum, interp_img.phent);
            exec_debug_dump_dynamic(proc, file, &hdr, &main_img, "main", filename);
            exec_debug_dump_dynamic(proc, interp_file, &interp_hdr, &interp_img, "interp", filename);
        }
        stack_img = main_img;
        stack_img.load_bias = interp_img.load_bias; /* AT_BASE points to interpreter base. */
        user_entry = interp_img.entry;
    } else {
        ret = load_elf_segments(proc, file, &hdr, &img, ET_DYN_MAIN_BIAS);
        if (ret < 0) {
            proc->mm = old_mm;
            vmm_destroy_address_space(new_mm);
            vfs_close(file);
            return ret;
        }
        EXEC_LOG("exec: segments loaded for %s\n", filename);
        stack_img = img;
        user_entry = img.entry;
    }
    
    /* Set up user stack */
    uid_t old_euid = 0, old_suid = 0, old_fsuid = 0;
    gid_t old_egid = 0, old_sgid = 0, old_fsgid = 0;
    if (apply_setuid || apply_setgid) {
        if (proc && proc->cred) {
            old_euid = proc->cred->euid;
            old_suid = proc->cred->suid;
            old_fsuid = proc->cred->fsuid;
            old_egid = proc->cred->egid;
            old_sgid = proc->cred->sgid;
            old_fsgid = proc->cred->fsgid;
            if (apply_setuid) {
                proc->cred->euid = exec_uid;
                proc->cred->suid = exec_uid;
                proc->cred->fsuid = exec_uid;
            }
            if (apply_setgid) {
                proc->cred->egid = exec_gid;
                proc->cred->sgid = exec_gid;
                proc->cred->fsgid = exec_gid;
            }
        }
    }

    ret = setup_user_stack(proc, argv, envp, filename, &stack_img, &sp, at_secure);
    if (ret < 0) {
        /* Exec failed; restore credentials to pre-exec values. */
        if (apply_setuid || apply_setgid) {
            if (proc && proc->cred) {
                proc->cred->euid = old_euid;
                proc->cred->suid = old_suid;
                proc->cred->fsuid = old_fsuid;
                proc->cred->egid = old_egid;
                proc->cred->sgid = old_sgid;
                proc->cred->fsgid = old_fsgid;
            }
        }
        if (proc) {
            proc->fs_base = old_fs_base;
        }
        proc->mm = old_mm;
        vmm_destroy_address_space(new_mm);
        if (interp_file) {
            vfs_close(interp_file);
        }
        vfs_close(file);
        return ret;
    }

    /* Initial %fs base for dynamic ELF / TLS (ld.so may touch %fs:offset before arch_prctl). */
    if (proc) {
        uint64_t new_fs = 0;
        if (use_interp) {
            new_fs = interp_img.has_tls ? interp_img.tls_base : main_img.tls_base;
        } else {
            new_fs = img.has_tls ? img.tls_base : 0;
        }
        if (new_fs == 0) {
            ret = map_user_tls_stub(proc);
            if (ret < 0) {
                if (apply_setuid || apply_setgid) {
                    if (proc->cred) {
                        proc->cred->euid = old_euid;
                        proc->cred->suid = old_suid;
                        proc->cred->fsuid = old_fsuid;
                        proc->cred->egid = old_egid;
                        proc->cred->sgid = old_sgid;
                        proc->cred->fsgid = old_fsgid;
                    }
                }
                proc->fs_base = old_fs_base;
                proc->mm = old_mm;
                vmm_destroy_address_space(new_mm);
                if (interp_file) {
                    vfs_close(interp_file);
                }
                vfs_close(file);
                return ret;
            }
            new_fs = USER_TLS_STUB_VADDR;
        }
        proc->fs_base = new_fs;
    }

    if (loader_exec_debug_enabled && path_contains_ld_linux(filename)) {
        printk(KERN_INFO "exec: final rsp=0x%lx (mod16=%lu) entry=0x%lx at_base=0x%lx\n",
               sp, sp & 0xFULL, user_entry, stack_img.load_bias);
    }
    
    /* Close file */
    if (interp_file) {
        vfs_close(interp_file);
    }
    vfs_close(file);
    
    /* Free old address space if this wasn't the shared kernel address space. */
    if (old_mm && old_mm != vmm_get_kernel_address_space()) {
        vmm_destroy_address_space(old_mm);
    }
    
    /* Reset signal handlers to default */
    for (int i = 0; i < NSIG; i++) {
        proc->sigactions[i].sa_handler = SIG_DFL;
    }
    
    /* Close close-on-exec file descriptors */
    for (size_t i = 0; i < proc->files->max_fds; i++) {
        if (proc->files->fds[i].flags & O_CLOEXEC) {
            fd_free(proc->files, i);
        }
    }
    
    /* Update process name */
    const char *name = strrchr(filename, '/');
    name = name ? name + 1 : filename;
    strncpy(proc->comm, name, sizeof(proc->comm) - 1);
    strncpy(proc->exec_path, filename, sizeof(proc->exec_path) - 1);
    proc->exec_path[sizeof(proc->exec_path) - 1] = '\0';
    
    /* Clear process flags */
    proc->flags &= ~PROC_FLAG_VFORKED;
    
    /* Switch to new address space */
    mmu_switch(new_mm->pt);
    EXEC_LOG("exec: switching to user mode entry=0x%lx sp=0x%lx\n", user_entry, sp);
    if (loader_exec_debug_enabled && path_contains_ld_linux(filename)) {
        printk(KERN_INFO "exec: user transition begin entry=0x%lx sp=0x%lx for %s\n",
               user_entry, sp, filename);
    }
    
    /* Jump to user mode (FS base applied in user_mode_enter immediately before iretq). */
    extern void user_mode_enter(uint64_t entry, uint64_t stack, uint64_t fs_base);
    user_mode_enter(user_entry, sp, proc ? proc->fs_base : 0);
    
    /* Should never reach here */
    panic("exec: user_mode_enter returned unexpectedly");
    return 0;
}