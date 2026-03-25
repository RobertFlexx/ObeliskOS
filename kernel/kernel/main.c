/*
 * Obelisk OS - Kernel Main Entry Point
 * From Axioms, Order.
 *
 * This is the first C code executed after boot.S sets up the environment.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <obelisk/limits.h>
#include <obelisk/bootinfo.h>
#include <obelisk/initramfs.h>
#include <arch/cpu.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <mm/kmalloc.h>
#include <proc/process.h>
#include <proc/scheduler.h>
#include <fs/vfs.h>
#include <ipc/msgqueue.h>
#include <sysctl/sysctl.h>
#include <net/e1000.h>

/* Kernel banner */
static const char *banner =
    "\n"
    "  ____  _          _ _     _       ___  ____  \n"
    " / __ \\| |        | (_)   | |     / _ \\/ ___| \n"
    "| |  | | |__   ___| |_ ___| | __ | | | \\___ \\ \n"
    "| |  | | '_ \\ / _ \\ | / __| |/ / | | | |___) |\n"
    "| |__| | |_) |  __/ | \\__ \\   <  | |_| |____/ \n"
    " \\____/|_.__/ \\___|_|_|___/_|\\_\\  \\___/|_____| \n"
    "\n"
    "  From Axioms, Order.\n"
    "  Version " OBELISK_VERSION_STRING " (" OBELISK_CODENAME ")\n"
    "\n";

/* Boot information */
static struct {
    uint32_t magic;
    uint64_t multiboot_addr;
    uint64_t boot_time;
    char cmdline[256];
} boot_info;

enum boot_mode {
    BOOT_MODE_NORMAL = 0,
    BOOT_MODE_INSTALLER_CLI,
    BOOT_MODE_INSTALLER_TUI,
};

static enum boot_mode selected_boot_mode = BOOT_MODE_NORMAL;

/* Parse kernel command line */
static void parse_cmdline(const char *cmdline) {
    if (!cmdline) return;
    
    strncpy(boot_info.cmdline, cmdline, sizeof(boot_info.cmdline) - 1);
    boot_info.cmdline[sizeof(boot_info.cmdline) - 1] = '\0';
    
    printk(KERN_INFO "Command line: %s\n", boot_info.cmdline);
    
    char parsebuf[sizeof(boot_info.cmdline)];
    char *tok;

    strncpy(parsebuf, boot_info.cmdline, sizeof(parsebuf) - 1);
    parsebuf[sizeof(parsebuf) - 1] = '\0';

    tok = strtok(parsebuf, " ");
    while (tok) {
        if (strcmp(tok, "installer=cli") == 0 ||
            strcmp(tok, "obelisk.installer=cli") == 0) {
            selected_boot_mode = BOOT_MODE_INSTALLER_CLI;
        } else if (strcmp(tok, "installer=tui") == 0 ||
                   strcmp(tok, "obelisk.installer=tui") == 0) {
            selected_boot_mode = BOOT_MODE_INSTALLER_TUI;
        }
        tok = strtok(NULL, " ");
    }
}

static void import_boot_rootfs(void) {
    const struct obelisk_boot_module *mod = bootinfo_find_module("rootfs");
    if (!mod) {
        printk(KERN_WARNING "No rootfs module supplied; continuing without initramfs\n");
        return;
    }

    if (mod->end <= mod->start) {
        printk(KERN_ERR "Invalid rootfs module bounds\n");
        return;
    }

    const void *archive = PHYS_TO_VIRT(mod->start);
    size_t archive_size = (size_t)(mod->end - mod->start);
    int ret = initramfs_unpack_tar(archive, archive_size);
    if (ret < 0) {
        printk(KERN_ERR "Failed to import rootfs tar: %d\n", ret);
    }
}

/* Early initialization (before memory management) */
static void early_init(void) {
    /* Initialize serial console first for debugging */
    uart_init();
    
    /* Print banner */
    printk("%s", banner);
    
    printk(KERN_INFO "Early initialization...\n");
    printk(KERN_INFO "Multiboot magic: 0x%x\n", boot_info.magic);
    printk(KERN_INFO "Multiboot info at: 0x%lx\n", boot_info.multiboot_addr);
}

/* Initialize memory subsystem */
static void memory_init(void) {
    printk(KERN_INFO "Initializing memory subsystem...\n");
    
    /* Physical memory manager */
    pmm_init(boot_info.multiboot_addr);
    
    /* Virtual memory manager */
    vmm_init();
    
    /* Kernel heap allocator */
    kmalloc_init();

    /* Enable framebuffer text mirroring once MMU mappings are available. */
    console_fb_init();
    
    printk(KERN_INFO "Memory subsystem initialized.\n");
}

/* Initialize networking (phase 1 hardware bring-up) */
static void network_init_all(void) {
    printk(KERN_INFO "Initializing networking hardware...\n");
    net_init();
    pci_init();
    virtio_net_init();
    if (!net_is_ready()) {
        e1000_init();
    }
    printk(KERN_INFO "Networking hardware initialization complete.\n");
}

/* Initialize CPU and interrupts */
static void cpu_init_all(void) {
    printk(KERN_INFO "Initializing CPU...\n");
    
    /* CPU feature/CR setup (SSE/FPU, etc.) */
    cpu_init();

    /* GDT */
    gdt_init();
    
    /* IDT and interrupts */
    idt_init();
    
    /* Syscall interface */
    extern void syscall_init(void);
    syscall_init();
    
    printk(KERN_INFO "CPU initialized.\n");
}

/* Initialize process management */
static void process_init_all(void) {
    printk(KERN_INFO "Initializing process management...\n");
    
    /* Process structures */
    process_init();
    
    /* Scheduler */
    scheduler_init();
    
    printk(KERN_INFO "Process management initialized.\n");
}

/* Initialize filesystems */
static void fs_init_all(void) {
    printk(KERN_INFO "Initializing filesystems...\n");
    
    /* VFS layer */
    vfs_init();
    
    /* AxiomFS */
    axiomfs_init();

    /* Mount in-memory AxiomFS as root */
    if (vfs_mount(NULL, "/", "axiomfs", 0, NULL) < 0) {
        panic("Failed to mount root filesystem");
    }

    /* Seed standard directories expected by early userland */
    vfs_mkdir("/sbin", 0755);
    vfs_mkdir("/etc", 0755);
    vfs_mkdir("/var", 0755);
    vfs_mkdir("/tmp", 0777);
    vfs_mkdir("/dev", 0755);
    
    /* DevFS */
    devfs_init();
    if (vfs_mount(NULL, "/dev", "devfs", 0, NULL) < 0) {
        printk(KERN_WARNING "Failed to mount devfs at /dev; falling back to serial-only stdio\n");
    }
    
    printk(KERN_INFO "Filesystems initialized.\n");
}

/* Initialize IPC */
static void ipc_init_all(void) {
    printk(KERN_INFO "Initializing IPC...\n");
    
    /* Message queues */
    ipc_init();
    
    /* Axiomd channel */
    extern void axiomd_channel_init(void);
    axiomd_channel_init();
    
    printk(KERN_INFO "IPC initialized.\n");
}

/* Initialize sysctl */
static void sysctl_init_all(void) {
    printk(KERN_INFO "Initializing sysctl...\n");
    
    sysctl_init();
    
    printk(KERN_INFO "sysctl initialized.\n");
}

/* Late initialization */
static void late_init(void) {
    printk(KERN_INFO "Late initialization...\n");

    import_boot_rootfs();

    /* Ensure PID 1 has a valid VFS root/cwd so relative paths and getcwd work. */
    if (init_process) {
        struct dentry *root = vfs_lookup("/");
        if (root && root->d_inode) {
            if (init_process->root) {
                dput(init_process->root);
            }
            if (init_process->cwd) {
                dput(init_process->cwd);
            }
            init_process->root = dget(root);
            init_process->cwd = dget(root);
            printk(KERN_INFO "init: filesystem context set to '/'\n");
            dput(root);
        } else {
            printk(KERN_WARNING "init: unable to resolve '/' for process root/cwd\n");
        }
    }

    if (selected_boot_mode == BOOT_MODE_INSTALLER_TUI) {
        printk(KERN_INFO "Boot mode: installer TUI\n");
        process_set_init_path("/sbin/installer-tui");
    } else if (selected_boot_mode == BOOT_MODE_INSTALLER_CLI) {
        printk(KERN_INFO "Boot mode: installer CLI\n");
        process_set_init_path("/sbin/installer");
    } else {
        process_set_init_path("/sbin/init");
    }

    /* Enable timer interrupt */
    extern void timer_init(void);
    timer_init();

    /* Start scheduler and hand off to PID 1 bootstrap path */
    scheduler_start();

    printk(KERN_INFO "Late initialization complete.\n");
}

/* Print system information */
static void print_system_info(void) {
    printk(KERN_INFO "=== System Information ===\n");
    
    /* Memory */
    pmm_dump_stats();
    
    /* CPU */
    struct cpu_info *cpu = cpu_get_current();
    if (cpu) {
        printk(KERN_INFO "CPU: %s\n", cpu->vendor);
        printk(KERN_INFO "  Family: %u, Model: %u, Stepping: %u\n",
               cpu->family, cpu->model, cpu->stepping);
    }
    
    printk(KERN_INFO "===========================\n");
}

/* Idle loop */
static void __noreturn idle_loop(void) {
    printk(KERN_INFO "Entering idle loop...\n");
    
    /* Enable interrupts */
    sti();
    
    while (1) {
        /* Check for pending work */
        scheduler_yield();
        
        /* Halt until next interrupt */
        hlt();
    }
}

/*
 * kernel_main - Main kernel entry point
 * @magic: Multiboot magic number
 * @multiboot_addr: Physical address of Multiboot information structure
 *
 * This function is called from boot.S after basic CPU setup.
 * It initializes all kernel subsystems and starts the first user process.
 */
void __noreturn kernel_main(uint32_t magic, uint64_t multiboot_addr) {
    /* Save boot information */
    boot_info.magic = magic;
    boot_info.multiboot_addr = multiboot_addr;
    boot_info.boot_time = 0;  /* TODO: Get from RTC */
    bootinfo_init(magic, multiboot_addr);
    parse_cmdline(bootinfo_cmdline());
    
    /* Phase 1: Early initialization */
    early_init();
    
    /* Phase 2: CPU setup */
    cpu_init_all();
    
    /* Phase 3: Memory management */
    memory_init();
    
    /* Phase 4: Networking */
    network_init_all();

    /* Phase 5: Process management */
    process_init_all();
    
    /* Phase 6: Filesystems */
    fs_init_all();
    
    /* Phase 7: IPC */
    ipc_init_all();
    
    /* Phase 8: sysctl */
    sysctl_init_all();
    
    /* Phase 9: Late initialization */
    late_init();
    
    /* Print system information */
    print_system_info();
    
    printk(KERN_NOTICE "Obelisk OS initialized successfully.\n");
    printk(KERN_NOTICE "From Axioms, Order.\n\n");
    
    /* Enter idle loop (scheduler will preempt us) */
    idle_loop();
}