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
        /*
         * SVC_CATCHER (image 0xE7B044, size 0xE40, ending at 0xE7BE84 where
         * the PROC2 segment starts - SAU2 map domain_os.10.2.map).  In the
         * image this is ONE segment: the eight TRAP dispatchers followed
         * immediately by their nine dispatch tables, and every dispatcher
         * reaches its table with `lea (TABLE:w,%pc)' - an R_68K_PC16
         * displacement of only 0x290..0xC08 bytes.  The default
         * `*(.text .text.*)' gather scatters the tables into .data, ~0x11000
         * bytes away, and all 11 of those relocations overflow (source-a5t8).
         * Reproduce the image's adjacency: dispatchers first, in image order
         * (trap0 0xE7B044, trap1 0xE7B05C, trap2 0xE7B094, trap3 0xE7B0D8,
         * trap4 0xE7B120, trap5 0xE7B17C, trap7 0xE7B1D8, trap8 0xE7B240),
         * then .text.svc_tables (svc/svc_tables.c), which holds all nine
         * tables and is 0xBA4 bytes long - exactly the image's
         * 0xE7B2DE..0xE7BE82.  (GCC emits the objects within that section in
         * reverse definition order, so the tables are adjacent to the
         * dispatchers but not in image order among themselves; only the
         * proximity matters for the R_68K_PC16 displacements.)
         *
         * NOTE: ld assigns each input section to the FIRST output-section
         * statement that matches it, so these explicit statements must stay
         * ahead of the `*(.text .text.*)' gather below.
         */
        *svc/sau2/trap0.o(.text)
        *svc/sau2/trap1.o(.text)
        *svc/sau2/trap2.o(.text)
        *svc/sau2/trap3.o(.text)
        *svc/sau2/trap4.o(.text)
        *svc/sau2/trap5.o(.text)
        *svc/sau2/trap7.o(.text)
        *svc/sau2/trap8.o(.text)
        *(.text.svc_tables)

        /*
         * TODO(source-uwxz): the same code/data adjacency is still missing for
         * the other image-adjacent tables, whose PC16 references also overflow:
         *   PROC1_$DISPATCH_INT  <- ec/sau2/advance.s, advance_all.s
         *   PROC1_$CURRENT (0xE2611E), ADVANCE_INT <- proc1/sau2/init_stack.s,
         *                                             int_handler.s
         *   SIO2681_$PTRS        <- sio2681/sau2/int_rte.s
         *   SMD_DISPLAY_INFO     <- smd/sau2/disp1_int.s
         * Those tables are owned by proc1/sio2681/smd and need the same
         * treatment (a dedicated section named here, in image order) once
         * their owners annotate the definitions.  Placing them belongs with
         * source-0i3's `.moddata.<name>' scheme where the block is a Pascal
         * module data block pinned at its A5 address.
         */

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
