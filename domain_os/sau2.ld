/*
 * RFC (Run File Converter) linker script for SAU2 domain_os kernel
 *
 * Output format: raw binary with 12-byte RFC header
 *   Offset 0:  Load address (where program is loaded in memory)
 *   Offset 4:  Start address (entry point - OS_$INIT)
 *   Offset 8:  Machine type (2 for SAU2)
 *   Offset 12: Program binary (loaded at load address)
 */

OUTPUT_FORMAT("binary")
OUTPUT_ARCH(m68k)

/* RFC header constants */
_rfc_load_addr = 0x00e00000;
_rfc_machine_type = 2;

SECTIONS {
    /*
     * RFC Header - 12 bytes at file offset 0
     * This is metadata, not loaded into memory
     */
    .rfc_header 0 : {
        LONG(_rfc_load_addr);       /* byte 0-3:  load address */
        LONG(OS_$INIT);             /* byte 4-7:  start address (entry point) */
        LONG(_rfc_machine_type);    /* byte 8-11: machine type (SAU2 = 2) */
    }

    /*
     * Program sections - VMA at load address, LMA immediately after header
     * The AT() directive places these at file offset 12 (0x0C) onward,
     * but addresses are resolved as if loaded at 0x00e00000
     */

    .text _rfc_load_addr : AT(0x0C) {
        *(.text .text.*)
    }

    .rodata : AT(LOADADDR(.text) + SIZEOF(.text)) {
        *(.rodata .rodata.*)
    }

    .data : AT(LOADADDR(.rodata) + SIZEOF(.rodata)) {
        _data_start = .;
        *(.data .data.*)
        _data_end = .;
    }

    .bss (NOLOAD) : {
        _bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        _bss_end = .;
    }

    _end = .;

    /* Discard debug sections and other unnecessary stuff */
    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.debug*)
    }
}
