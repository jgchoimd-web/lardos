/* Linker script for LARDX user-mode executables.
 * Load at 0x601000 (below LDLL at 0x610000), stack at 0x7FF000.
 */
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x601000;

    .text : {
        *(.text*)
    } :text

    . = ALIGN(0x1000);
    .rodata : {
        *(.rodata*)
    } :rodata

    . = ALIGN(0x1000);
    .data : {
        *(.data*)
    } :data

    .bss : {
        *(.bss*)
        *(COMMON)
    } :data

    _end = .;
}
