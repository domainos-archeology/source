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
         * PROC1_ASM (image 0xE1EAC8, size 0x24A4 - SAU2 map domain_os.10.2.map)
         *
         * One image segment holds the EC and PROC1 assembly routines together
         * with a handful of data cells, and five of the cross-references
         * inside it are 16-bit PC-relative:
         *
         *   0xE206F8 bsr.w PROC1_$DISPATCH_INT  (ec/sau2/advance.s,   +0x326)
         *   0xE2070E bsr.w PROC1_$DISPATCH_INT  (ec/sau2/advance_all.s, +0x310)
         *   0xE208FA bsr.w ADVANCE_INT          (proc1/sau2/int_handler.s,
         *                                        twice, -0x1D0 and -0x236)
         *   0xE20AD8 move.w (-0x4d2,PC),-(SP)   (proc1/sau2/init_stack.s ->
         *                                        PROC1_$CURRENT at 0xE20608)
         *
         * The default gather puts ec/ and proc1/ far apart (and PROC1_$CURRENT
         * in .bss ~0x11000 away), so all five overflow (source-uwxz).  Emit the
         * segment's contents together, in image order: the data cell first
         * (0xE20608), then ec (EC_$WAIT 0xE20610 ... ADVANCE 0xE20728), then
         * proc1 (PROC1_$REMOVE_READY 0xE206D2 ... 0xE20F6C).  Whole subsystems
         * are listed rather than the five referencing objects alone, because
         * pulling single objects out would in turn strand their intra-ec and
         * intra-proc1 bsr.w/bra.w partners (e.g. ec/sau2/advance_all.s ->
         * ADVANCE_ALL_INT at 0xE207C6, proc1/sau2/clr_lock.s ->
         * PROC1_$DISPATCH_INT2).
         *
         * PROC1_$CURRENT is a code-segment cell here, not a Pascal A5 module
         * block, so it uses the source-a5t8 `.text.<name>' technique and is
         * deliberately not part of source-0i3's `.moddata.<name>' scheme.
         */
        *(.text.proc1_asm_data)
        */ec/*.o(.text)
        */proc1/*.o(.text)

        /*
         * SIO_INT (image 0xE2DF80, size 0x8C).  SIO2681_$PTRS is the first
         * thing in the segment and the two 2681 stubs follow it at 0xE2DFA0
         * and 0xE2DFB0, each reaching an entry with `lea (-0x2a,PC),A0'
         * (0xE2DFA8 -> 0xE2DF80, 0xE2DFB8 -> 0xE2DF90).  Table first, then the
         * stubs - image order (source-uwxz).
         */
        *(.text.sio2681_ptrs)
        */sio2681/sau2/int_rte.o(.text)

        /*
         * SMD_WIRED (image 0xE26F20, size 0x5E0).  SMD_$DISP1_INT opens the
         * segment and its constant/common tables close it; the routine reaches
         * SMD_$DISPLAY_COM (= SMD_DISPLAY_INFO, 0xE27376) with
         * `lea (0x446,PC),A1' at 0xE26F2E.  Code first, then the table - image
         * order (source-uwxz).
         *
         * SMD_$XOR_CURSOR/$OR_CURSOR, $START_SCROLL/$CONTINUE_SCROLL and
         * $START_BLT all reach SMD_$DISP1_INT with their own
         * `lea (SMD_$DISP1_INT:w,PC),A0', so the whole run of SMD_WIRED
         * routines has to travel with it; they are listed in image order.
         */
        */smd/sau2/disp1_int.o(.text)       /* SMD_$DISP1_INT      0xE26F20 */
        */smd/sau2/cursor_thunks.o(.text)   /* SMD_$XOR_CURSOR     0xE2720E */
        */smd/blink_cursor_1.o(.text)       /* SMD_$BLINK_CURSOR_1 0xE2722C */
        */smd/interrupt_init.o(.text)       /* SMD_$INTERRUPT_INIT 0xE27284 */
        */smd/sau2/scroll.o(.text)          /* SMD_$START_SCROLL   0xE272A8 */
        */smd/sau2/start_blt.o(.text)       /* SMD_$START_BLT      0xE272BC */
        *(.text.smd_display_info)           /* SMD_$DISPLAY_COM    0xE27376 */

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
