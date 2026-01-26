/*
 * fim/sau2/crash.s - FIM Crash Handler (m68k/SAU2)
 *
 * System crash handler that displays crash information and
 * invokes CRASH_SYSTEM.
 *
 * Original address: 0x00E1E864 (158 bytes)
 */

        .text
        .even

/*
 * External references
 */
        .equ    CRASH_SYSTEM,       0x00E1E700  /* System crash function */
        .equ    CRASH_PUTS,         0x00E1E7C8  /* Console output function */

/*
 * FIM_$CRASH - System crash handler
 *
 * Called when a fault cannot be delivered or is fatal.
 * Formats and displays crash information including:
 * - Exception type (SR format code)
 * - Fault address
 * - Saved registers
 *
 * Parameters:
 *   6(SP) - Pointer to exception frame
 *   A(SP) - Pointer to saved register area
 *   E(SP) - Pointer to status
 *
 * Stack on entry (after MOVE SR,-(SP)):
 *   0(SP) - Saved SR
 *   2(SP) - Return address (to caller)
 *   6(SP) - Exception frame pointer
 *   A(SP) - Register save area pointer
 *   E(SP) - Status pointer
 *
 * Assembly (0x00E1E864):
 */
        .global FIM_$CRASH
FIM_$CRASH:
        move    %sr,-(%sp)              /* Save SR */
        ori     #0x0700,%sr             /* Disable interrupts */
        lea     (crash_data,%pc),%a5    /* A5 = crash data area */
        movea.l (0x6,%sp),%a2           /* A2 = exception frame */

        /* Save exception frame info */
        move.l  %a2,(crash_frame_ptr,%a5) /* Save frame pointer */
        move.w  (%a2),(crash_sr,%a5)    /* Save SR from frame */
        move.l  (0x2,%a2),(crash_pc,%a5) /* Save PC from frame */
        move.w  (0x6,%a2),%d0           /* D0 = format/vector word */
        move.w  %d0,(crash_format,%a5)  /* Save format word */

        /* Extract format code and look up exception type */
        lsr.w   #2,%d0                  /* D0 = format >> 2 */
        lea     (exc_type_table,%pc),%a0 /* A0 = type lookup table */

        /* Check if format code is in valid range */
        cmp.b   #0x20,%d0               /* Less than 0x20? */
        blt.b   crash_lookup_type       /* Yes, use lookup */
        cmp.b   #0x30,%d0               /* Less than 0x30? */
        blt.b   crash_use_table         /* Yes, use table directly */
crash_lookup_type:
        /* Scan exception type string for matching code */
        lea     (exc_type_chars,%pc),%a0
crash_scan:
        move.b  (%a0)+,%d1              /* Get type char */
        beq.b   crash_use_table         /* End of string, use default */
        cmp.b   %d0,%d1                 /* Match? */
        bne.b   crash_scan              /* No, try next */
crash_use_table:
        move.b  (%a0),(crash_type,%a5)  /* Store exception type char */

        /* Check if we need to extract additional fault info */
        moveq   #0x25,%d4               /* D4 = '%' (no additional info) */
        cmp.b   #0x03,%d0               /* Format < 4? */
        bgt.b   crash_no_extra          /* No extra info for format >= 4 */

        /* Extract fault address from short bus error frame */
        moveq   #0x20,%d4               /* D4 = ' ' (has additional info) */
        move.w  (0x8,%a2),%d5           /* D5 = instruction word */
        move.l  (0xA,%a2),%d6           /* D6 = fault address */

        /* Check frame format byte for format A/B distinction */
        cmpi.b  #0x80,(0x6,%a2)         /* Format 0x8x? */
        beq.b   crash_store_addr        /* Yes, use this address */

        /* Format B frame - different layout */
        move.w  (0xA,%a2),%d5           /* D5 = SSW */
        move.l  (0x10,%a2),%d6          /* D6 = fault address */
crash_store_addr:
        move.w  %d5,(crash_ssw,%a5)     /* Save SSW/instruction */
        move.l  %d6,(crash_fault_addr,%a5) /* Save fault address */

crash_no_extra:
        move.b  %d4,(crash_has_addr,%a5) /* Store has-address flag */

        /* Display crash message */
        lea     (crash_msg,%pc),%a0     /* A0 = message string */
        jsr     (CRASH_PUTS).l          /* Output to console */

        /* Set up for CRASH_SYSTEM call */
        movea.l (0xA,%sp),%a0           /* A0 = register save area */
        move.l  (0x3C,%a0),(crash_sp,%a5) /* Save SP from save area */

        /* Restore registers from save area and call CRASH_SYSTEM */
        move.l  (0x20,%a0),-(%sp)       /* Push additional context */
        movem.l (%a0)+,%d0-%d7/%a0-%a6  /* Restore all registers */
        movea.l (%sp)+,%a0              /* Pop to A0 */
        move.l  (0xE,%sp),-(%sp)        /* Push status pointer */
        jsr     (CRASH_SYSTEM).l        /* Call crash handler */
        addq.w  #4,%sp                  /* Clean up stack */

        move    (%sp)+,%sr              /* Restore SR */
        rts

/*
 * Crash data area
 */
crash_data:
crash_frame_ptr:
        .long   0                       /* Exception frame pointer */
crash_sr:
        .word   0                       /* Status register */
crash_pc:
        .long   0                       /* Program counter */
crash_format:
        .word   0                       /* Format/vector word */
crash_type:
        .byte   0                       /* Exception type character */
crash_has_addr:
        .byte   0x25                    /* '%' or ' ' */
crash_fault_addr:
        .long   0                       /* Fault address */
crash_ssw:
        .word   0                       /* Special status word */
crash_sp:
        .long   0                       /* Stack pointer */
        .even

/*
 * Exception type character lookup table
 * Maps format codes to display characters
 */
exc_type_chars:
        .ascii  "S0O1O2O3O4O5O6O"       /* Exception type codes */
        .byte   0
        .even

/*
 * Exception type table
 * Direct lookup for format codes >= 0x20
 */
exc_type_table:
        .ascii  "ABCDEFGHIJKLMNOP"
        .byte   0
        .even

/*
 * Crash message string
 */
crash_msg:
        .ascii  "FAULT IN DOMAIN_OS "
        .byte   0
        .even

        .end
