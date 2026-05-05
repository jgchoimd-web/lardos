/* Linker script for LARDX user-mode executables.
 * Load at 0x401000 (below LDLL at 0x410000), stack at 0x7FF000.
 */
ENTRY(_start)

SECTIONS
{
    . = 0x401000;

    .text : {
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        *(.bss*)
        *(COMMON)
    }

    _end = .;
}
