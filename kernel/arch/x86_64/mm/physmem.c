/*
 * Obelisk OS - Architecture-specific Physical Memory
 * From Axioms, Order.
 *
 * Parses Multiboot2 memory map and initializes PMM.
 */

#include <obelisk/types.h>
#include <obelisk/kernel.h>
#include <arch/cpu.h>
#include <mm/pmm.h>

/* Multiboot2 structures */
#define MULTIBOOT2_MAGIC            0x36d76289

#define MULTIBOOT_TAG_TYPE_END              0
#define MULTIBOOT_TAG_TYPE_CMDLINE          1
#define MULTIBOOT_TAG_TYPE_BOOT_LOADER_NAME 2
#define MULTIBOOT_TAG_TYPE_MODULE           3
#define MULTIBOOT_TAG_TYPE_BASIC_MEMINFO    4
#define MULTIBOOT_TAG_TYPE_BOOTDEV          5
#define MULTIBOOT_TAG_TYPE_MMAP             6
#define MULTIBOOT_TAG_TYPE_VBE              7
#define MULTIBOOT_TAG_TYPE_FRAMEBUFFER      8
#define MULTIBOOT_TAG_TYPE_ELF_SECTIONS     9
#define MULTIBOOT_TAG_TYPE_APM              10
#define MULTIBOOT_TAG_TYPE_EFI32            11
#define MULTIBOOT_TAG_TYPE_EFI64            12
#define MULTIBOOT_TAG_TYPE_SMBIOS           13
#define MULTIBOOT_TAG_TYPE_ACPI_OLD         14
#define MULTIBOOT_TAG_TYPE_ACPI_NEW         15
#define MULTIBOOT_TAG_TYPE_NETWORK          16
#define MULTIBOOT_TAG_TYPE_EFI_MMAP         17
#define MULTIBOOT_TAG_TYPE_EFI_BS           18
#define MULTIBOOT_TAG_TYPE_EFI32_IH         19
#define MULTIBOOT_TAG_TYPE_EFI64_IH         20
#define MULTIBOOT_TAG_TYPE_LOAD_BASE_ADDR   21

#define MULTIBOOT_MEMORY_AVAILABLE          1
#define MULTIBOOT_MEMORY_RESERVED           2
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE   3
#define MULTIBOOT_MEMORY_NVS                4
#define MULTIBOOT_MEMORY_BADRAM             5

struct multiboot_tag {
    uint32_t type;
    uint32_t size;
} __packed;

struct multiboot_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
} __packed;

struct multiboot_mmap_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t zero;
} __packed;

struct multiboot_tag_basic_meminfo {
    uint32_t type;
    uint32_t size;
    uint32_t mem_lower;
    uint32_t mem_upper;
} __packed;

struct multiboot_tag_module {
    uint32_t type;
    uint32_t size;
    uint32_t mod_start;
    uint32_t mod_end;
    char cmdline[];
} __packed;

struct multiboot_tag_string {
    uint32_t type;
    uint32_t size;
    char string[];
} __packed;

/* External symbols from linker script */
extern char __kernel_start[];
extern char __kernel_end[];
extern char __text_start[];
extern char __text_end[];
extern char __rodata_start[];
extern char __rodata_end[];
extern char __data_start[];
extern char __data_end[];
extern char __bss_start[];
extern char __bss_end[];

/* Memory information */
static uint64_t total_memory = 0;
static uint64_t usable_memory = 0;
static uint64_t kernel_start_phys = 0;
static uint64_t kernel_end_phys = 0;

/* Get multiboot tag */
static struct multiboot_tag *multiboot_get_tag(void *mbi, uint32_t type) {
    struct multiboot_tag *tag = (struct multiboot_tag *)((uint8_t *)mbi + 8);
    
    while (tag->type != MULTIBOOT_TAG_TYPE_END) {
        if (tag->type == type) {
            return tag;
        }
        
        /* Move to next tag (8-byte aligned) */
        uint64_t addr = (uint64_t)tag + tag->size;
        addr = ALIGN_UP(addr, 8);
        tag = (struct multiboot_tag *)addr;
    }
    
    return NULL;
}

/* Parse memory map and initialize PMM */
void physmem_init(uint32_t magic, void *mbi) {
    /* Verify multiboot magic */
    if (magic != MULTIBOOT2_MAGIC) {
        panic("Invalid Multiboot2 magic: 0x%x", magic);
    }
    
    printk(KERN_INFO "Parsing Multiboot2 information at %p\n", mbi);
    
    /* Calculate kernel physical addresses */
    kernel_start_phys = (uint64_t)__kernel_start - KERNEL_VIRT_BASE;
    kernel_end_phys = (uint64_t)__kernel_end - KERNEL_VIRT_BASE;
    
    printk(KERN_INFO "Kernel: 0x%lx - 0x%lx (%lu KB)\n",
           kernel_start_phys, kernel_end_phys,
           (kernel_end_phys - kernel_start_phys) / 1024);
    
    /* Find memory map tag */
    struct multiboot_tag_mmap *mmap_tag = 
        (struct multiboot_tag_mmap *)multiboot_get_tag(mbi, MULTIBOOT_TAG_TYPE_MMAP);
    
    if (!mmap_tag) {
        panic("No memory map in Multiboot2 info");
    }
    
    printk(KERN_INFO "Memory map:\n");
    
    /* First pass: count total and usable memory */
    struct multiboot_mmap_entry *entry = (struct multiboot_mmap_entry *)(mmap_tag + 1);
    struct multiboot_mmap_entry *end = (struct multiboot_mmap_entry *)
        ((uint8_t *)mmap_tag + mmap_tag->size);
    
    uint64_t highest_addr = 0;
    
    while (entry < end) {
        const char *type_str;
        switch (entry->type) {
            case MULTIBOOT_MEMORY_AVAILABLE:
                type_str = "Available";
                usable_memory += entry->len;
                break;
            case MULTIBOOT_MEMORY_RESERVED:
                type_str = "Reserved";
                break;
            case MULTIBOOT_MEMORY_ACPI_RECLAIMABLE:
                type_str = "ACPI Reclaimable";
                break;
            case MULTIBOOT_MEMORY_NVS:
                type_str = "ACPI NVS";
                break;
            case MULTIBOOT_MEMORY_BADRAM:
                type_str = "Bad RAM";
                break;
            default:
                type_str = "Unknown";
                break;
        }
        
        total_memory += entry->len;
        
        if (entry->addr + entry->len > highest_addr) {
            highest_addr = entry->addr + entry->len;
        }
        
        printk(KERN_INFO "  0x%016lx - 0x%016lx: %s (%lu MB)\n",
               entry->addr, entry->addr + entry->len - 1,
               type_str, entry->len / (1024 * 1024));
        
        entry = (struct multiboot_mmap_entry *)
            ((uint8_t *)entry + mmap_tag->entry_size);
    }
    
    printk(KERN_INFO "Total memory: %lu MB, Usable: %lu MB\n",
           total_memory / (1024 * 1024), usable_memory / (1024 * 1024));
    
    /* Initialize PMM with total page count */
    uint64_t total_pages = highest_addr / PAGE_SIZE;
    pmm_init_bitmap(total_pages);
    
    /* Second pass: mark available regions */
    entry = (struct multiboot_mmap_entry *)(mmap_tag + 1);
    
    while (entry < end) {
        if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
            uint64_t start = ALIGN_UP(entry->addr, PAGE_SIZE);
            uint64_t end_addr = ALIGN_DOWN(entry->addr + entry->len, PAGE_SIZE);
            
            if (end_addr > start) {
                pmm_mark_region_free(start, end_addr - start);
            }
        }
        
        entry = (struct multiboot_mmap_entry *)
            ((uint8_t *)entry + mmap_tag->entry_size);
    }
    
    /* Reserve first 1MB (legacy area) */
    pmm_mark_region_used(0, 0x100000);
    
    /* Reserve kernel memory */
    pmm_mark_region_used(kernel_start_phys, kernel_end_phys - kernel_start_phys);
    
    /* Reserve multiboot info */
    uint32_t mbi_size = *(uint32_t *)mbi;
    uint64_t mbi_addr = (uint64_t)mbi;
    uint64_t mbi_phys;
    if (mbi_addr >= KERNEL_PHYS_MAP_BASE) {
        mbi_phys = mbi_addr - KERNEL_PHYS_MAP_BASE;
    } else if (mbi_addr >= KERNEL_VIRT_BASE) {
        mbi_phys = mbi_addr - KERNEL_VIRT_BASE;
    } else {
        mbi_phys = mbi_addr;
    }
    pmm_mark_region_used(mbi_phys, ALIGN_UP(mbi_size, PAGE_SIZE));

    /* Reserve loaded modules (e.g. initramfs) */
    struct multiboot_tag *tag = (struct multiboot_tag *)((uint8_t *)mbi + 8);
    while (tag->type != MULTIBOOT_TAG_TYPE_END) {
        if (tag->type == MULTIBOOT_TAG_TYPE_MODULE) {
            struct multiboot_tag_module *mod = (struct multiboot_tag_module *)tag;
            uint64_t mod_start = mod->mod_start;
            uint64_t mod_end = mod->mod_end;
            if (mod_end > mod_start) {
                pmm_mark_region_used(mod_start, ALIGN_UP(mod_end - mod_start, PAGE_SIZE));
                printk(KERN_INFO "Reserved module memory: 0x%lx - 0x%lx (%s)\n",
                       mod_start, mod_end, mod->cmdline);
            }
        }
        tag = (struct multiboot_tag *)ALIGN_UP((uint64_t)tag + tag->size, 8);
    }
    
    printk(KERN_INFO "Physical memory manager initialized\n");
}

/* Get total memory */
uint64_t physmem_get_total(void) {
    return total_memory;
}

/* Get usable memory */
uint64_t physmem_get_usable(void) {
    return usable_memory;
}