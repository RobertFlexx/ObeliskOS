/*
 * Obelisk OS - Kernel Linker Script
 * From Axioms, Order.
 */

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

/* Virtual address where kernel is loaded */
KERNEL_VMA = 0xFFFFFFFF80000000;

/* Physical address where kernel is loaded */
KERNEL_LMA = 0x100000;

SECTIONS
{
    /* Start at 1MB physical */
    . = KERNEL_LMA;
    
    /* Multiboot header must be early in the file */
    .multiboot ALIGN(4K) :
    {
        KEEP(*(.multiboot))
    }

    /* Bootstrap sections must remain in low memory for .code32 relocations */
    .text.boot ALIGN(4K) :
    {
        *(.text.boot)
        *(.text.boot.*)
    }

    .rodata.boot ALIGN(4K) :
    {
        *(.rodata.boot)
        *(.rodata.boot.*)
    }

    .data.boot ALIGN(4K) :
    {
        *(.data.boot)
        *(.data.boot.*)
    }

    .bss.boot ALIGN(4K) :
    {
        *(.bss.boot)
        *(.bss.boot.*)
    }
    
    /* Switch to virtual addresses */
    . += KERNEL_VMA;
    
    /* Code section */
    __text_start = .;
    .text ALIGN(4K) : AT(ADDR(.text) - KERNEL_VMA)
    {
        *(.text)
        *(.text.*)
    }
    __text_end = .;
    
    /* Read-only data */
    __rodata_start = .;
    .rodata ALIGN(4K) : AT(ADDR(.rodata) - KERNEL_VMA)
    {
        *(.rodata)
        *(.rodata.*)
    }
    __rodata_end = .;
    
    /* Read-write data */
    __data_start = .;
    .data ALIGN(4K) : AT(ADDR(.data) - KERNEL_VMA)
    {
        *(.data)
        *(.data.*)
    }
    __data_end = .;
    
    /* Uninitialized data */
    __bss_start = .;
    .bss ALIGN(4K) : AT(ADDR(.bss) - KERNEL_VMA)
    {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }
    __bss_end = .;
    
    /* Kernel end */
    . = ALIGN(4K);
    __kernel_end = .;
    
    /* Calculate kernel physical end */
    __kernel_start = KERNEL_VMA + KERNEL_LMA;
    __kernel_phys_end = __kernel_end - KERNEL_VMA;
    
    /* Discard unnecessary sections */
    /DISCARD/ :
    {
        *(.comment)
        *(.note)
        *(.note.*)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }
}