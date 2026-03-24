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
#include <uapi/syscall.h>

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
#define AT_CLKTCK       17
#define AT_SECURE       23
#define AT_RANDOM       25
#define AT_EXECFN       31

struct exec_image_info {
    uint64_t entry;
    uint64_t load_bias;
    uint64_t phdr;
    uint64_t phent;
    uint64_t phnum;
};

struct auxv_pair {
    uint64_t type;
    uint64_t value;
};

static int read_user_memory(struct process *proc, uint64_t uaddr, void *dst, size_t len) {
    uint8_t *p = (uint8_t *)dst;
    while (len > 0) {
        uint64_t phys = mmu_resolve(proc->mm->pt, uaddr);
        if (!phys) {
            return -EFAULT;
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
    return false;
}

static void exec_debug_dump_stack(struct process *proc, uint64_t sp, int argc, int envc, const char *filename) {
    if (!path_contains_ld_linux(filename)) {
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
 * Obelisk currently supports only static ET_EXEC binaries.
 * Dynamic/PIE binaries require PT_INTERP + auxv + dynamic linker handoff.
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

    /* PT_INTERP is handled by do_execve trampoline path. */
    (void)has_interp;

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
        if (phdr.p_type != PT_LOAD) {
            continue;
        }
        uint64_t seg_vaddr = phdr.p_vaddr + load_bias;
        printk(KERN_INFO "exec: PT_LOAD[%d] vaddr=0x%lx filesz=%lu memsz=%lu flags=0x%x\n",
               i, seg_vaddr, phdr.p_filesz, phdr.p_memsz, phdr.p_flags);
        
        /* Calculate memory region */
        uint64_t start = ALIGN_DOWN(seg_vaddr, PAGE_SIZE);
        uint64_t end = ALIGN_UP(seg_vaddr + phdr.p_memsz, PAGE_SIZE);
        
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

                off_t read_off = phdr.p_offset + copied;
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
 * Set up user stack with arguments and environment
 */
static int setup_user_stack(struct process *proc, char *const argv[],
                            char *const envp[], const char *filename,
                            const struct exec_image_info *img, uint64_t *sp) {
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
    
    /* Allocate a few pages at the top of stack */
    for (uint64_t addr = stack_top - PAGE_SIZE * 4; addr < stack_top; addr += PAGE_SIZE) {
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

    if (argc > 0) {
        execfn_ptr = argv_ptrs[0];
    } else if (filename) {
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
    auxv[auxc++] = (struct auxv_pair){ AT_UID,    0 };
    auxv[auxc++] = (struct auxv_pair){ AT_EUID,   0 };
    auxv[auxc++] = (struct auxv_pair){ AT_GID,    0 };
    auxv[auxc++] = (struct auxv_pair){ AT_EGID,   0 };
    auxv[auxc++] = (struct auxv_pair){ AT_PLATFORM, platform_ptr };
    auxv[auxc++] = (struct auxv_pair){ AT_CLKTCK, 100 };
    auxv[auxc++] = (struct auxv_pair){ AT_SECURE, 0 };
    auxv[auxc++] = (struct auxv_pair){ AT_RANDOM, random_ptr };
    auxv[auxc++] = (struct auxv_pair){ AT_EXECFN, execfn_ptr };
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
    printk(KERN_INFO "exec: user stack ready top=0x%lx sp=0x%lx argc=%d envc=%d\n",
           stack_top, sp_val, argc, envc);
    exec_debug_dump_stack(proc, sp_val, argc, envc, filename);
    
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
    memset(&img, 0, sizeof(img));
    memset(&main_img, 0, sizeof(main_img));
    memset(&interp_img, 0, sizeof(interp_img));
    memset(&stack_img, 0, sizeof(stack_img));
    
    printk(KERN_INFO "exec: begin %s pid=%d\n", filename, proc ? proc->pid : -1);
    
    /* Open executable */
    printk(KERN_INFO "exec: opening %s\n", filename);
    file = vfs_open(filename, O_RDONLY, 0);
    if (IS_ERR(file)) {
        printk(KERN_ERR "exec: open failed for %s: %ld\n", filename, PTR_ERR(file));
        return PTR_ERR(file);
    }
    printk(KERN_INFO "exec: opened %s\n", filename);
    
    /* Read ELF header */
    off_t offset = 0;
    printk(KERN_INFO "exec: reading ELF header\n");
    ret = vfs_read(file, &hdr, sizeof(hdr), &offset);
    if (ret != sizeof(hdr)) {
        printk(KERN_ERR "exec: failed reading ELF header ret=%d expected=%lu\n",
               ret, sizeof(hdr));
        vfs_close(file);
        return -EIO;
    }
    printk(KERN_INFO "exec: ELF header read complete\n");
    
    /* Validate ELF */
    ret = validate_elf(&hdr);
    if (ret < 0) {
        char shebang[128];
        off_t soff = 0;
        int n = vfs_read(file, shebang, sizeof(shebang) - 1, &soff);
        if (n > 2) {
            shebang[n] = '\0';
            if (shebang[0] == '#' && shebang[1] == '!') {
                char interp[PATH_MAX];
                char optarg[64];
                int i = 2, ip = 0, op = 0;
                bool has_optarg = false;
                while (shebang[i] == ' ' || shebang[i] == '\t') i++;
                while (shebang[i] && shebang[i] != ' ' && shebang[i] != '\t' &&
                       shebang[i] != '\n' && ip < (int)sizeof(interp) - 1) {
                    interp[ip++] = shebang[i++];
                }
                interp[ip] = '\0';
                while (shebang[i] == ' ' || shebang[i] == '\t') i++;
                while (shebang[i] && shebang[i] != '\n' && op < (int)sizeof(optarg) - 1) {
                    optarg[op++] = shebang[i++];
                }
                optarg[op] = '\0';
                has_optarg = (op > 0);
                if (ip > 0) {
                    int argc = 0;
                    while (argv && argv[argc] && argc < MAX_ARGC) argc++;
                    if (argc < MAX_ARGC) {
                        int extra = has_optarg ? 3 : 2; /* interp [optarg] script */
                        int new_argc = extra + ((argc > 1) ? (argc - 1) : 0);
                        char **sh_argv = kmalloc(sizeof(char *) * (new_argc + 1));
                        if (!sh_argv) {
                            vfs_close(file);
                            return -ENOMEM;
                        }
                        int k = 0;
                        sh_argv[k++] = interp;
                        if (has_optarg) sh_argv[k++] = optarg;
                        sh_argv[k++] = (char *)filename;
                        for (int j = 1; j < argc; j++) {
                            sh_argv[k++] = argv[j];
                        }
                        sh_argv[k] = NULL;
                        vfs_close(file);
                        printk(KERN_INFO "exec: shebang '%s' via %s\n", filename, interp);
                        ret = do_execve(interp, sh_argv, envp);
                        kfree(sh_argv);
                        return ret;
                    }
                }
            }
        }
        vfs_close(file);
        return ret;
    }
    printk(KERN_INFO "exec: ELF validated entry=0x%lx phnum=%u\n", hdr.e_entry, hdr.e_phnum);

    ret = read_elf_interp_path(file, &hdr, interp_path, sizeof(interp_path));
    if (ret < 0) {
        vfs_close(file);
        return ret;
    }
    if (ret > 0 && strcmp(filename, interp_path) != 0) {
        use_interp = true;
        printk(KERN_INFO "exec: PT_INTERP detected, loading interpreter %s\n", interp_path);
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
        ret = load_elf_segments(proc, file, &hdr, &main_img, 0x400000);
        if (ret < 0) {
            proc->mm = old_mm;
            vmm_destroy_address_space(new_mm);
            vfs_close(interp_file);
            vfs_close(file);
            return ret;
        }
        ret = load_elf_segments(proc, interp_file, &interp_hdr, &interp_img, 0x70000000ULL);
        if (ret < 0) {
            proc->mm = old_mm;
            vmm_destroy_address_space(new_mm);
            vfs_close(interp_file);
            vfs_close(file);
            return ret;
        }
        printk(KERN_INFO "exec: segments loaded for %s and interpreter %s\n", filename, interp_path);
        stack_img = main_img;
        stack_img.load_bias = interp_img.load_bias; /* AT_BASE points to interpreter base. */
        user_entry = interp_img.entry;
    } else {
        ret = load_elf_segments(proc, file, &hdr, &img, 0x400000);
        if (ret < 0) {
            proc->mm = old_mm;
            vmm_destroy_address_space(new_mm);
            vfs_close(file);
            return ret;
        }
        printk(KERN_INFO "exec: segments loaded for %s\n", filename);
        stack_img = img;
        user_entry = img.entry;
    }
    
    /* Set up user stack */
    ret = setup_user_stack(proc, argv, envp, filename, &stack_img, &sp);
    if (ret < 0) {
        proc->mm = old_mm;
        vmm_destroy_address_space(new_mm);
        if (interp_file) {
            vfs_close(interp_file);
        }
        vfs_close(file);
        return ret;
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
    printk(KERN_INFO "exec: switching to user mode entry=0x%lx sp=0x%lx\n", user_entry, sp);
    
    /* Jump to user mode */
    extern void user_mode_enter(uint64_t entry, uint64_t stack);
    user_mode_enter(user_entry, sp);
    
    /* Should never reach here */
    panic("exec: user_mode_enter returned unexpectedly");
    return 0;
}