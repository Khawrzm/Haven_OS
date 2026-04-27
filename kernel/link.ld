/*
 * link.ld — HAVEN OS micro-kernel linker script
 * Target: bare-metal RV32IMA
 * Memory map matches src/riscv.h:
 *   RAM: 0x80000000, 16 MB
 *
 * The kernel is loaded at RAM_BASE and jumps straight into _start
 * (which is placed in .text.boot so it sits at the very top).
 */

OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 16M
}

SECTIONS
{
    . = ORIGIN(RAM);

    .text : ALIGN(4) {
        KEEP(*(.text.boot))   /* _start must come first */
        *(.text .text.*)
    } > RAM

    .rodata : ALIGN(4) {
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
    } > RAM

    .data : ALIGN(4) {
        *(.data .data.*)
        *(.sdata .sdata.*)
    } > RAM

    .bss : ALIGN(4) {
        __bss_start = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        __bss_end = .;
    } > RAM

    /* Stack lives at the very top of RAM — see rv32_reset() in src/riscv.c */
    . = ALIGN(16);
    __heap_start = .;

    /DISCARD/ : { *(.comment) *(.note) *(.note.*) *(.eh_frame*) }
}
