/*
 * fim/sau2/bus_err.s - FIM_$BUS_ERR, the Domain/OS bus-error handler (m68k/SAU2)
 *
 * Original ROM extent: 0x00E218CA .. 0x00E21ACB
 *   0x00E218CA  JMP_TO_BUS_ERR    (6 bytes)   jmp (BUS_ERROR_SWITCH).l
 *   0x00E218CC  BUS_ERROR_SWITCH  (4 bytes)   patchable jmp operand
 *   0x00E218E8  FIM_$BUS_ERR      (484 bytes) exception vector 2 handler
 *
 * FIM_$BUS_ERR is installed as the CPU bus-error vector; the vector-table
 * cell that names it lives at 0x00E342E8, and OS_$INIT swaps it in for the
 * PROM handler once the kernel can service its own faults.  It is
 * hand-written assembly (raw SR manipulation, movem save sets, a tail jump
 * into another handler's body, and a self-modifying jmp), so it is
 * transcribed here rather than translated to C.
 *
 * ---------------------------------------------------------------------------
 * Stack layout
 * ---------------------------------------------------------------------------
 * The handler reserves an 8-byte scratch block *below* the CPU frame and then
 * saves 5 registers.  Writing S for the value of SP at the moment the CPU
 * finished pushing the exception frame:
 *
 *   S+0x00 .. S+0x?? : CPU exception frame (format 8, A or B)
 *
 * after "subq.w #8,%sp" and "movem.l %d0-%d2/%a0-%a1,-(%sp)" the frame is
 * addressed off the new SP (call it T = S-28):
 *
 *   (0x00,%sp)  saved D0
 *   (0x04,%sp)  saved D1
 *   (0x08,%sp)  saved D2
 *   (0x0C,%sp)  saved A0
 *   (0x10,%sp)  saved A1
 *   (0x14,%sp)  scratch: fault status_$t   (long)   \  the three-word fault
 *   (0x18,%sp)  scratch: BSD signal number (word)    > descriptor handed to
 *   (0x1A,%sp)  scratch: fault class flags (word)   /  FIM_$COM
 *   (0x1C,%sp)  exception frame + 0x00: SR
 *   (0x1E,%sp)  exception frame + 0x02: PC
 *   (0x22,%sp)  exception frame + 0x06: format / vector word
 *   (0x24,%sp)  exception frame + 0x08: internal register
 *   (0x26,%sp)  exception frame + 0x0A: 68010 fault address (long)
 *                                       68020 special status word (word)
 *   (0x2C,%sp)  exception frame + 0x10: 68020 data cycle fault address
 *   (0x40,%sp)  exception frame + 0x24: 68020 format B stage B address
 *
 * The scratch block is the (status, signal, flags) triple that FIM_$COM
 * consumes; FIM_$PARITY_TRAP (0x00E21F84) builds the identical triple by
 * hand before its own "pea (%sp) / pea (0xc,%sp) / jmp FIM_$COM" tail, which
 * is what fixes the field order here.
 *
 * ---------------------------------------------------------------------------
 * Exception frame formats inspected
 * ---------------------------------------------------------------------------
 * Which frame the CPU pushed is decided from bit 0 of the MMU status
 * register (0x00FFB403), not from the frame itself:
 *
 *   bit 0 = 1  MMU type 1 board (68010).  The CPU pushed a format 8 long bus
 *              fault frame; the faulting address is the longword at frame
 *              offset 0x0A.  Taken directly.
 *
 *   bit 0 = 0  MMU type 2 board (68020, "stingray").  The CPU pushed either a
 *              format A (short bus cycle fault, 16 words) or a format B (long
 *              bus cycle fault, 46 words) frame.  Both carry the special
 *              status word at frame offset 0x0A, and the faulting address is
 *              recovered from it:
 *                SSW bit 8  (DF) set  -> data cycle fault address, frame+0x10
 *                otherwise, format A  -> PC + 2, and + 2 more if SSW bit 14
 *                                        (FB, fault on pipe stage B) is set
 *                otherwise, format B  -> stage B address, frame+0x24, less 2
 *                                        if SSW bit 15 (FC, fault on pipe
 *                                        stage C) is set
 *              Format A is recognised by the high byte of the format/vector
 *              word being 0xA0.
 *
 * The MMU status byte read at entry (and cleared immediately, so the next
 * fault sees fresh state) also classifies the fault.  Bits used, in the order
 * the handler tests them:
 *
 *   bit 0  MMU type: 1 = type 1 / 68010 frame, 0 = type 2 / 68020 frame
 *   bit 6  page not resident -> ask MST to touch the page in
 *   bit 7  protection violation -> SIGSEGV to the faulting process
 *   bit 5  bus timeout / nonexistent memory
 *   bit 3  (with bit 5, on a type 2 MMU) memory error - crash the system
 *
 * ---------------------------------------------------------------------------
 * Encoding differences from the ROM image
 * ---------------------------------------------------------------------------
 * Disassembling this file and comparing it instruction for instruction with
 * the image (124 instructions on both sides) leaves 17 differing encodings,
 * in three classes.  None of them changes what the routine does.
 *
 * (a) Four cross-file references the ROM reached PC-relatively.  A 16-bit
 *     displacement cannot survive independent assembly and linking, so these
 *     are re-encoded as absolute (or as a wider branch):
 *
 *       0x00E219B8  jmp (FIM_$EXIT,%pc)           4efa 0f02 -> 4ef9 00e228bc
 *       0x00E219CE  bsr.w FIM_$DELIVER_TRACE_FAULT
 *                                                 6100 0e96 -> 4eb9 00e22866
 *       0x00E219DC  lea (FIM_$TRACE_STS,%pc),%a0  41fa 09c4 -> 41f9 00e223a2
 *       0x00E21ACA  bra.b fim_fline_switch        600a      -> 6000 xxxx
 *
 *     Each grows by two bytes, so the emitted routine is 492 bytes rather
 *     than the original 484.  An absolute jmp/jsr and a PC-relative one
 *     differ only in how the destination is spelled; bra.w differs from
 *     bra.b only in displacement width; and bsr and jsr push the same
 *     return address (the branch here is a call that returns normally).
 *
 * (b) Five immediate-operand instructions where the Apollo assembler chose
 *     the CMP/SUB/ADD "immediate effective address" encoding and gas chooses
 *     CMPI/SUBI/ADDI.  Same operation, same operand, same size, same
 *     condition codes:
 *
 *       0x00E2195A  cmp.l #0x00FC0000,%d2   b4bc ... -> 0c82 ...
 *       0x00E21962  cmp.l #0x00FE0000,%d2   b4bc ... -> 0c82 ...
 *       0x00E21974  sub.l #0x00FC0000,%d2   94bc ... -> 0482 ...
 *       0x00E2197E  add.w #128,%d2          d47c ... -> 0642 ...
 *       0x00E219C0  cmp.l #0x0004000A,%d0   b0bc ... -> 0c80 ...
 *
 * (c) Eight branches whose opcode and destination are unchanged but whose
 *     displacement shifts, because (a) made the code eight bytes longer:
 *     the branches at 0x00E21912, 0x00E2194E, 0x00E219B0, 0x00E219C6,
 *     0x00E219E8, 0x00E21A62, 0x00E21A84 and 0x00E21AC2.
 * ==================================================================== */

        .text
        .even

/* ====================================================================
 * External references - code and data outside this file
 * ==================================================================== */

        /* Process/scheduling data */
        .equ    PROC1_AS_ID,        0x00E2060A  /* Current address space ID (word) */

        /* FIM code and data (emitted in fim/sau2/fim.s or not yet emitted) */
        .equ    FIM_EXIT,           0x00E228BC  /* RTE */
        .equ    FIM_COM,            0x00E213A4  /* Common fault delivery entry */
        .equ    FIM_DELIVER_TRACE_FAULT, 0x00E22866
        .equ    FIM_TRACE_STS,      0x00E223A2  /* Per-AS trace fault status, 4 bytes/AS */
        .equ    FP_SAVEP,           0x00E218D0  /* Non-zero if FPU hardware present */

        /* Other subsystems */
        .equ    MMU_INSTALL,        0x00E24048  /* MMU_$INSTALL */
        .equ    MST_TOUCH,          0x00E0DD40  /* MST_$TOUCH */
        .equ    CACHE_CLEAR,        0x00E242D4  /* CACHE_$CLEAR */
        .equ    CRASH_SYSTEM,       0x00E1E700  /* CRASH_SYSTEM(&status) */

        /* Hardware registers */
        .equ    MMU_CSR_LOW,        0x00FFB401  /* Low byte of MMU CSR (0xFFB400) */
        .equ    FP_HW_OWNER,        0x00FFB402  /* Hardware FPU owner register */
        .equ    MMU_STATUS_REG,     0x00FFB403  /* MMU status / fault classification */
        .equ    MMU_ERR_SRC_REG,    0x00FFB40A  /* Memory error source; written to clear */
                                                /* TODO(source-nx70): verify the name - */
                                                /* only this handler touches the register */

/* ====================================================================
 * Constants
 * ==================================================================== */

        /* I/O window that the handler maps on demand (see .bus_err_map_io) */
        .equ    IO_WINDOW_BASE,     0x00FC0000
        .equ    IO_WINDOW_LIMIT,    0x00FE0000
        .equ    IO_WINDOW_PPN,      128         /* First page frame of the window */

        /* status_$t values */
        .equ    STATUS_MST_GUARD_FAULT,  0x0004000A /* mst guard page touched */
        .equ    STATUS_FAULT_PROTECTION, 0x00120011 /* MMU protection violation */
        .equ    STATUS_FAULT_BUS_TIMEOUT,0x0012000C /* No device answered */
        .equ    STATUS_MMU_ERR_4,        0x00070004
        .equ    STATUS_MMU_ERR_5,        0x00070005
        .equ    STATUS_MMU_ERR_6,        0x00070006

        /* BSD signal numbers placed in the fault descriptor */
        .equ    SIGBUS,             10
        .equ    SIGSEGV,            11

        /* Fault descriptor flag word used for memory access faults */
        .equ    FAULT_CLASS_ACCESS, 0x3000


/* ====================================================================
 * JMP_TO_BUS_ERR / BUS_ERROR_SWITCH - installable bus error trampoline
 *
 * BUS_ERROR_SWITCH is the *operand* of the jmp at JMP_TO_BUS_ERR: the
 * handler transfers control by branching to JMP_TO_BUS_ERR, which jumps
 * to whatever address the cell currently holds.  io_$probe (0x00E29138)
 * plants its recovery label there before poking at a possibly absent
 * controller and the bus error handler routes to it when the fault is a
 * bus timeout, which is how a probe of empty address space returns
 * "device not present" instead of crashing.
 *
 * Zero means "nobody is fielding bus timeouts"; the handler then delivers
 * a SIGBUS fault instead.
 *
 * Assembly (0x00E218CA, 6 bytes):
 * ==================================================================== */
        /* In ROM the trampoline sits at the odd-longword address 0x00E218CA,
         * so that BUS_ERROR_SWITCH - the jmp's operand - lands longword
         * aligned at 0x00E218CC.  The word of padding reproduces that parity;
         * it is the tail of the zero fill that runs up to 0x00E218CA. */
        .balign 4
        .short  0

        .global JMP_TO_BUS_ERR
JMP_TO_BUS_ERR:
        .short  0x4EF9                  /* jmp (BUS_ERROR_SWITCH).l - the */
                                        /* operand is the patchable cell below */
        .global BUS_ERROR_SWITCH
BUS_ERROR_SWITCH:
        .long   0                       /* 0x00E218CC: recovery handler, or 0 */


/* ====================================================================
 * FIM_$BUS_ERR - bus error (vector 2) handler
 *
 * Entered from the exception vector with the CPU frame on the supervisor
 * stack.  Masks interrupts, snapshots and clears the MMU status byte, works
 * out the faulting address from whichever frame format the CPU pushed, and
 * then dispatches on the fault class:
 *
 *   page not resident   -> MMU_$INSTALL for the I/O window, else MST_$TOUCH,
 *                          then RTE to retry the faulting instruction
 *   guard page          -> FIM_$DELIVER_TRACE_FAULT + record the status in
 *                          FIM_$TRACE_STS, then RTE
 *   protection violation-> SIGSEGV fault delivered through FIM_$COM
 *   memory error        -> CRASH_SYSTEM
 *   bus timeout         -> BUS_ERROR_SWITCH if armed, else SIGBUS
 *   FPU access fault    -> lazy FP owner switch through FIM_$FLINE's body
 *
 * Registers saved/restored: D0-D2, A0-A1.
 *
 * Assembly (0x00E218E8, 484 bytes):
 * ==================================================================== */
        .global FIM_$BUS_ERR
FIM_$BUS_ERR:
        ori.w   #0x0700,%sr             /* Mask all interrupts (IPL 7) */
        subq.w  #8,%sp                  /* Reserve the fault descriptor scratch */
        movem.l %d0-%d2/%a0-%a1,-(%sp)  /* Save working registers */

        move.b  (MMU_STATUS_REG).l,%d1  /* D1 = MMU fault status */
        clr.b   (MMU_STATUS_REG).l      /* Clear it for the next fault */

        btst    #0,%d1                  /* MMU type 1 (68010 frame)? */
        beq.b   .bus_err_frame_68020
        move.l  (0x26,%sp),%d2          /* D2 = frame+0x0A, the fault address */
        bra.b   .bus_err_classify

/* --------------------------------------------------------------------
 * 68020 ("stingray") frame decode - recover the faulting address from
 * the special status word at frame+0x0A.
 * Assembly (0x00E2190A):
 * -------------------------------------------------------------------- */
.bus_err_frame_68020:
        btst    #3,(MMU_CSR_LOW).l      /* FPU access cycle? */
        bne.w   .bus_err_fp
        move.w  (0x26,%sp),%d0          /* D0 = special status word */

        btst    #8,%d0                  /* SSW DF: data cycle faulted? */
        beq.b   .bus_err_not_data
        move.l  (0x2c,%sp),%d2          /* D2 = data cycle fault address */
        bra.b   .bus_err_classify

.bus_err_not_data:
        cmpi.b  #0xa0,(0x22,%sp)        /* Format A (short bus cycle fault)? */
        bne.b   .bus_err_format_b
        move.l  (0x1e,%sp),%d2          /* D2 = frame PC */
        addq.l  #2,%d2                  /* Advance past the pipe stage C word */
        btst    #14,%d0                 /* SSW FB: fault on pipe stage B? */
        beq.b   .bus_err_classify
        addq.l  #2,%d2                  /* Then it is one more word out */
        bra.b   .bus_err_classify

.bus_err_format_b:
        move.l  (0x40,%sp),%d2          /* D2 = stage B address (format B only) */
        btst    #15,%d0                 /* SSW FC: fault on pipe stage C? */
        beq.b   .bus_err_classify
        subq.l  #2,%d2                  /* Stage C is one word behind stage B */

/* --------------------------------------------------------------------
 * Common path: D1 = MMU fault status, D2 = faulting address.
 * Assembly (0x00E2194A):
 * -------------------------------------------------------------------- */
.bus_err_classify:
        btst    #6,%d1                  /* Page not resident? */
        beq.w   .bus_err_check_prot

        btst    #13,(0x1c,%sp)          /* Saved SR bit 13: supervisor mode? */
        beq.b   .bus_err_touch          /* User fault - always go through MST */

        /* A supervisor reference into the I/O window is mapped on the spot
         * rather than paged in, so device registers can be touched from
         * interrupt level without entering the pager. */
        cmp.l   #IO_WINDOW_BASE,%d2
        bcs.b   .bus_err_touch
        cmp.l   #IO_WINDOW_LIMIT,%d2
        bcc.b   .bus_err_touch

.bus_err_map_io:
        move.w  #22,-(%sp)              /* MMU_$INSTALL arg 4 */
        move.w  #0,-(%sp)               /* MMU_$INSTALL arg 3 */
        move.l  %d2,-(%sp)              /* MMU_$INSTALL arg 2: virtual address */
        sub.l   #IO_WINDOW_BASE,%d2     /* Offset within the window */
        lsr.l   #8,%d2                  /* /1024: 1KB pages */
        lsr.l   #2,%d2
        add.w   #IO_WINDOW_PPN,%d2      /* Window starts at page frame 128 */
        move.l  %d2,-(%sp)              /* MMU_$INSTALL arg 1: page frame */
        jsr     (MMU_INSTALL).l
        lea     (12,%sp),%sp            /* Pop the four arguments */
        bra.b   .bus_err_return

/* --------------------------------------------------------------------
 * Ask MST to make the page resident.  Interrupts are re-enabled (IPL 0)
 * across the call - the pager blocks.
 * Assembly (0x00E21990):
 * -------------------------------------------------------------------- */
.bus_err_touch:
        move.l  #0,-(%sp)               /* MST_$TOUCH arg 3 */
        pea     (0x18,%sp)              /* MST_$TOUCH arg 2: &scratch status */
        move.l  %d2,-(%sp)              /* MST_$TOUCH arg 1: faulting address */
        andi.w  #0xf8ff,%sr             /* Drop to IPL 0 */
        jsr     (MST_TOUCH).l
        adda.l  #12,%sp                 /* Pop the three arguments */
        move.l  (0x14,%sp),%d0          /* D0 = status MST_$TOUCH returned */
        bne.b   .bus_err_touch_failed

/* --------------------------------------------------------------------
 * Retry the faulting instruction.
 * Assembly (0x00E219B2):
 * -------------------------------------------------------------------- */
.bus_err_return:
        movem.l (%sp)+,%d0-%d2/%a0-%a1  /* Restore working registers */
        addq.w  #8,%sp                  /* Discard the fault descriptor scratch */
        jmp     (FIM_EXIT).l            /* RTE (was "jmp (FIM_$EXIT,%pc)") */

/* --------------------------------------------------------------------
 * MST_$TOUCH could not satisfy the fault.
 * Assembly (0x00E219BC):
 * -------------------------------------------------------------------- */
.bus_err_touch_failed:
        move.w  #SIGSEGV,%d1
        cmp.l   #STATUS_MST_GUARD_FAULT,%d0
        bne.b   .bus_err_deliver        /* Anything else becomes a SIGSEGV */

        /* A guard page was touched: turn it into a trace fault for the
         * current address space and let the process run on. */
        move.w  (PROC1_AS_ID).l,-(%sp)
        jsr     (FIM_DELIVER_TRACE_FAULT).l  /* was "bsr.w" (PC-relative) */
        addq.l  #2,%sp
        move.w  (PROC1_AS_ID).l,%d0
        lsl.w   #2,%d0                  /* 4 bytes of trace status per AS */
        lea     (FIM_TRACE_STS).l,%a0   /* was "lea (FIM_$TRACE_STS,%pc),%a0" */
        move.l  #STATUS_MST_GUARD_FAULT,(0,%a0,%d0.w)
        bra.b   .bus_err_return

/* --------------------------------------------------------------------
 * Not a page fault.  Protection violation?
 * Assembly (0x00E219EA):
 * -------------------------------------------------------------------- */
.bus_err_check_prot:
        btst    #7,%d1
        beq.b   .bus_err_check_timeout
        move.l  #STATUS_FAULT_PROTECTION,%d0
        move.w  #SIGSEGV,%d1
        /* fall through */

/* --------------------------------------------------------------------
 * Deliver the fault in D0 (status) / D1 (signal) through FIM_$COM.
 * The descriptor triple is written into the scratch block, the working
 * registers are restored, and FIM_$COM is entered with
 *   (0x00,%sp) = &exception frame
 *   (0x04,%sp) = &fault descriptor
 * exactly as FIM_$PARITY_TRAP (0x00E21F84) does.
 * Assembly (0x00E219FA):
 * -------------------------------------------------------------------- */
.bus_err_deliver:
        andi.w  #0xf8ff,%sr             /* Drop to IPL 0 */
        move.l  %d0,(0x14,%sp)          /* descriptor: status */
        move.w  %d1,(0x18,%sp)          /* descriptor: signal number */
        move.w  #FAULT_CLASS_ACCESS,(0x1a,%sp)  /* descriptor: fault class */
        movem.l (%sp)+,%d0-%d2/%a0-%a1  /* SP now points at the descriptor */
        pea     (%sp)                   /* Push &fault descriptor */
        pea     (12,%sp)                /* Push &exception frame */
        jmp     (FIM_COM).l

/* --------------------------------------------------------------------
 * Bus timeout / memory error.
 * Assembly (0x00E21A1C):
 * -------------------------------------------------------------------- */
.bus_err_check_timeout:
        btst    #5,%d1                  /* Bus timeout? */
        beq.w   .bus_err_fp             /* Neither - treat as an FPU cycle */
        btst    #0,%d1                  /* Type 1 MMU has no error source reg */
        bne.b   .bus_err_switch
        btst    #3,%d1                  /* Memory error reported alongside? */
        beq.b   .bus_err_switch

        /* Hard memory error: pick the status the error source register
         * indicates and crash. */
        lea     (.bus_err_sts_4,%pc),%a0
        btst    #5,(MMU_ERR_SRC_REG).l
        bne.b   .bus_err_crash
        lea     (.bus_err_sts_5,%pc),%a0
        btst    #4,(MMU_ERR_SRC_REG).l
        bne.b   .bus_err_crash
        lea     (.bus_err_sts_6,%pc),%a0

.bus_err_crash:
        move.l  %a0,-(%sp)              /* CRASH_SYSTEM takes &status_$t */
        jsr     (CRASH_SYSTEM).l
        addq.w  #4,%sp
        move.w  #0x4000,(MMU_ERR_SRC_REG).l  /* Acknowledge the error */
        bra.w   .bus_err_return         /* CRASH_SYSTEM can return */

        /* Status constants, addressed PC-relatively above.
         * Assembly (0x00E21A66, 0x00E21A6A, 0x00E21A6E): */
.bus_err_sts_4:
        .long   STATUS_MMU_ERR_4
.bus_err_sts_5:
        .long   STATUS_MMU_ERR_5
.bus_err_sts_6:
        .long   STATUS_MMU_ERR_6

/* --------------------------------------------------------------------
 * Plain bus timeout: hand off to whoever armed BUS_ERROR_SWITCH.
 * Assembly (0x00E21A72):
 * -------------------------------------------------------------------- */
.bus_err_switch:
        tst.l   (BUS_ERROR_SWITCH).l
        beq.b   .bus_err_no_switch
        jsr     (CACHE_CLEAR).l         /* Drop anything the aborted cycle cached */
        movem.l (%sp)+,%d0-%d2/%a0-%a1  /* Scratch + frame stay on the stack; */
                                        /* the switch target unwinds them */
        bra.w   JMP_TO_BUS_ERR

.bus_err_no_switch:
        move.w  #SIGBUS,%d1
        move.l  #STATUS_FAULT_BUS_TIMEOUT,%d0
        bra.w   .bus_err_deliver

/* --------------------------------------------------------------------
 * FPU access fault.  Reached when the 68020 frame decoder saw the FPU
 * cycle bit in the MMU CSR, and when the fault matched none of the MMU
 * status bits above.
 *
 * With no FPU present the hardware owner register is simply cleared and
 * the instruction retried.  Otherwise, if this really was an FPU cycle
 * from a live address space, fall into FIM_$FLINE's body to run the lazy
 * FP owner switch - the fault is the 68881 refusing a process that does
 * not currently own it.
 * Assembly (0x00E21A96):
 * -------------------------------------------------------------------- */
.bus_err_fp:
        tst.l   (FP_SAVEP).l            /* FPU hardware present? */
        bne.b   .bus_err_fp_check
        move.b  #0,(FP_HW_OWNER).l      /* No FPU: disown and retry */
        bra.b   .bus_err_fp_done

.bus_err_fp_check:
        btst    #3,(MMU_CSR_LOW).l      /* FPU access cycle? */
        beq.b   .bus_err_fp_done
        move.w  (PROC1_AS_ID).l,%d0     /* Running in a real address space? */
        beq.b   .bus_err_fp_done
        movem.l (%sp)+,%d0-%d2/%a0-%a1  /* Undo this handler's frame entirely */
        addq.w  #8,%sp
        bra.b   .bus_err_fp_switch

.bus_err_fp_done:
        bra.w   .bus_err_return

/*
 * Alternate entry into FIM_$FLINE: FP_$SAVEP has already been tested, so
 * the FLINE prologue is repeated with FLINE's save set and control is
 * handed to the body past that test.
 * Assembly (0x00E21AC6, the last 6 bytes of the region):
 */
.bus_err_fp_switch:
        movem.l %d0-%d3/%a0-%a1,-(%sp)  /* FIM_$FLINE's save set */
        bra.w   fim_fline_switch        /* was "bra.b" (PC-relative, in range */
                                        /* only because FIM_$FLINE follows) */
