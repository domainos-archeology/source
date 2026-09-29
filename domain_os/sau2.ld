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
     *
     * The RFC image is contiguous (settled 2026-09-29, source-702z): .text
     * (code and MODULE_DATA blocks, in map order), .rodata and .data each
     * start where the previous one ends, apart from alignment padding, and
     * their load addresses follow the same way, so the flat binary has no
     * holes and .bss follows it.  Nothing is placed at a fixed address any
     * more (the owner's decision of 2026-09-28 replaced pinning with map
     * order), so the question of sparse VMAs in a flat RFC image, which
     * pinning blocks at their original addresses would have raised, does
     * not arise.  The only absolute addresses left are hardware / PROM
     * cells, and the image addresses that the guards exempted in
     * tools/check_guards.py and the class-4 literals of the hand-written
     * assembly (tools/asm_abs_addrs.md) still use; those name memory this
     * link gives to something else (in the 2026-09-29 scratch link .text
     * covers 0xE1DC80 and 0xE7DC00, .bss 0xEB2800..0xEB2C00) and are tracked
     * by their beads.
     */

    .text _rfc_load_addr : AT(0x0C) {
        /*
         * Link order (source-0i3; the owner's decision of 2026-09-28): the
         * code and the Pascal module data blocks are laid out in the order
         * of the SAU2 link map (domain_os.10.2.map), not at the image's
         * addresses.  A block that follows its module's code in the image
         * follows it here, and modules come in the image's sequence;
         * absolute addresses and sizes differ from the image.
         *
         * layout.ld is build/sau2/layout.ld, written before every link by
         * tools/gen_layout_ld.py (make passes -L build/sau2 so INCLUDE
         * finds it; it is not checked in, so it cannot drift from the
         * sources).  It lists one input-section statement per C function
         * (`.text.<symbol>', -ffunction-sections), per hand-written routine
         * (`.section ".text.<symbol>"' in the <subsystem>/sau2 assembly
         * files) and per MODULE_DATA
         * block (`.moddata.<name>', ordered by its declared original
         * address), in map order, and ends with the `*(.text .text.*)'
         * catch-all for code the map does not name.  `make check' links a
         * scratch ELF and proves the order.
         *
         * The map order also gives the adjacencies that 16-bit and 8-bit
         * PC-relative references inside the image's hand-written segments
         * need, which this script used to spell out object by object:
         *   SVC_CATCHER 0xE7B044: the eight trap dispatchers, then the nine
         *     dispatch tables (.text.svc_tables, anchored at 0xE7B2DE)
         *     reached with `lea (TABLE:w,%pc)' (source-a5t8);
         *   PROC1_ASM 0xE1EAC8: PROC1_$TSVV / PROC1_$CURRENT, EC_$WAIT ..
         *     ADVANCE / ADVANCE_INT, the PROC1 routines; `bsr.w' / `bsr.b'
         *     and `move.w (d,PC)' between them (source-uwxz, source-mc3k,
         *     source-0ke7);
         *   SIO_INT 0xE2DF80: SIO2681_$PTRS, then the two 2681 stubs
         *     (source-uwxz);
         *   SMD_WIRED 0xE26F20: SMD_$DISP1_INT, the cursor/scroll/BLT
         *     thunks, SMD_$DISPLAY_COM (C name SMD_DISPLAY_INFO, anchored)
         *     and SMD_TIME_$COM (source-uwxz, source-xlo9).
         * ld assigns each input section to the first statement that matches
         * it, so the ordered list precedes the catch-all.
         */
        INCLUDE layout.ld
    }

    .rodata : AT(LOADADDR(.text) + SIZEOF(.text)) {
        *(.rodata .rodata.*)
    }

    .data : AT(ADDR(.data) - _rfc_load_addr + 0x0C) {
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
