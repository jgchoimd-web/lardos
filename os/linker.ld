ENTRY(_start)

SECTIONS
{
    /* Load kernel at 1MB physical address */
    . = 1M;

    __kernel_start = .;

    .text ALIGN(4K) : {
        __text_start = .;
        *(.text*)
        __text_end = .;
    }

    .rodata ALIGN(4K) : {
        __rodata_start = .;
        *(.rodata*)
        __rodata_end = .;
    }

    .data ALIGN(4K) : {
        __data_start = .;
        *(.data*)
        __data_end = .;
    }

    /* Writable JIT code buffer (made executable by MMU setup). */
    .jit ALIGN(4K) : {
        __jit_start = .;
        *(.jit*)
        __jit_end = .;
    }

    .bss ALIGN(4K) : {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        __bss_end = .;
    }

    /* First address after the kernel image in memory. */
    __kernel_end = .;
}

