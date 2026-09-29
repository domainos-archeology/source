| stop/sau2/watch.s - hand-written assembly of the stopwatch module (SAU2)
|
| Everything in this file is register-argument or exception-entry code and
| therefore cannot be expressed in C:
|
|   STOP_$MEASURE_LOOP  0x00E81916  no arguments, result in D1, NO `rts` of
|                                   its own -- it falls through into
|                                   STOP_$NULL_PROC, whose `rts` returns for
|                                   it, and it exits by forcing IPL 0
|   STOP_$NULL_PROC     0x00E8193E  a bare `rts`; both the tail of the loop
|                                   above and the routine it calls 1024 times
|   STOP_$UNHOOK        0x00E819E2  A1 = slot
|   STOP_$HOOK          0x00E81A0A  A0 = patch record, A1 = slot,
|                                   A2 = parent slot or 0, D0.w = slot number
|   STOP_$WATCH_UII     0x00E81A56  A-line (unimplemented instruction) vector,
|                                   ends in `rte`
|   STOP_$WATCH_TRACE   0x00E81AB2  second-stage trace handler, ends in `rte`
|   STOP_$READ_CPUT     0x00E81B54  result in D1
|   STOP_$READ_CLOCK    0x00E81B5C  result in D1 (shares the thunk above)
|
| How the stopwatch works: STOP_$HOOK writes an A-line trap word over the
| instruction at the "entry" address (0xA000 + slot number) and, when there is
| one, over the "exit" address (0xA100 + slot number), saving the original
| words in the slot.  Executing a patched instruction traps to
| STOP_$WATCH_UII, which restores the real instruction word in place, stashes
| the registers, and arms STOP_$WATCH_TRACE_FLAG so the trap is re-taken into
| STOP_$WATCH_TRACE.  The trace handler charges PROC1_$GET_CPUT and
| TIME_$CLOCK deltas to the slot and puts the trap word back.
|
| The module's data lives immediately after this code in the original image,
| addressed as (off,A5) with A5 = 0x00E81814.  Here it is the C object
| STOP_$DATA (stop/stop_data.c, stop_$data_t in stop/stop_internal.h), which
| the link places at the block's original address, 0x00E81BEC = A5+0x3D8.
| The cells this file touches keep their old names as assembler-local
| aliases for STOP_$DATA + field offset (below), so every instruction and its
| encoding is unchanged; each reference carries the original (off,A5) or
| PC-relative form in a comment.
|
| Slot field offsets (see stopwatch_slot_t in stop/stop_internal.h):
|   0x00 patch1  0x04 patch2  0x08 parent  0x0C saved1  0x0E saved2
|   0x10 flags   0x12 owner   0x14 completions   0x18 reentries
|   0x1C cpu_time  0x20 elapsed_time  0x24 cpu_events  0x28 elapsed_events
|   0x2C entry_cput  0x30 entry_clock  0x34 entry_traps  0x38 entry_gtraps

        .section ".text.STOP_$WATCH_UII","ax",@progbits

| Cells of STOP_$DATA.  Block offset = A5 displacement - 0x3D8; the offsets
| are _Static_assert'ed against the struct in stop/stop_internal.h.
        .set    STOP_$SAVED_REGS,  STOP_$DATA + 0x000  | (0x3d8,A5) 0x00E81BEC saved_regs
        .set    STOP_$SW_OVERHEAD, STOP_$DATA + 0x01C  | (0x3f4,A5) 0x00E81C08 sw_overhead
        .set    STOP_$TRAP_COUNTS, STOP_$DATA + 0x024  | (0x3fc,A5) 0x00E81C10 trap_counts
        .set    STOPWATCH_SLOTS,   STOP_$DATA + 0x13C  | (0x514,A5) 0x00E81D28 slots

        .globl  STOP_$MEASURE_LOOP
        .globl  STOP_$NULL_PROC
        .globl  STOP_$UNHOOK
        .globl  STOP_$HOOK
        .globl  STOP_$WATCH_UII
        .globl  STOP_$WATCH_TRACE
        .globl  STOP_$READ_CPUT
        .globl  STOP_$READ_CLOCK
        .globl  stop_$measure_loop
        .globl  stop_$hook
        .globl  stop_$unhook

| ---------------------------------------------------------------------------
| STOP_$MEASURE_LOOP - 0x00E81916
|
| Times 1024 iterations of {call a bare rts, call CACHE_$CLEAR} with all
| interrupts masked and returns the PROC1_$GET_CPUT delta in D1.  Run once
| unhooked and once with slot 0 hooked onto STOP_$NULL_PROC, the difference
| is the per-trap cost of the instrumentation.
|
| Clobbers D0, D1, D2 (the original saves nothing).  The exit is
| `andi #-0x701,SR`, a forced drop to IPL 0, not a restore.
| ---------------------------------------------------------------------------
STOP_$MEASURE_LOOP:
        ori.w   #0x0700,%sr             | 00e81916
        bsr.w   STOP_$READ_CPUT         | 00e8191a
        move.l  %d1,-(%sp)              | 00e8191e
        move.l  #0x3ff,%d2              | 00e81920  1024 iterations
.Lml_loop:
        bsr.w   STOP_$NULL_PROC         | 00e81926
        jsr     CACHE_$CLEAR            | 00e8192a
        dbf     %d2,.Lml_loop           | 00e81930
        bsr.w   STOP_$READ_CPUT         | 00e81934
        sub.l   (%sp)+,%d1              | 00e81938
        andi.w  #0xf8ff,%sr             | 00e8193a  force IPL 0
        | 00e8193c: no rts -- fall through into STOP_$NULL_PROC

| ---------------------------------------------------------------------------
| STOP_$NULL_PROC - 0x00E8193E.  A bare rts, and the address the calibration
| patch record (STOP_$CALIB_PATCH) points at.
| ---------------------------------------------------------------------------
STOP_$NULL_PROC:
        rts                             | 00e8193e

| ---------------------------------------------------------------------------
| STOP_$UNHOOK - 0x00E819E2.  A1 = slot.
|
| Puts the original instruction words back, flushes the instruction cache and
| clears the slot's "running" bit, under a true SR save/restore.
| ---------------------------------------------------------------------------
STOP_$UNHOOK:
        move.w  %sr,%d1                 | 00e819e2
        ori.w   #0x0700,%sr             | 00e819e4
        movea.l (%a1),%a0               | 00e819e8  slot->patch1
        move.w  (12,%a1),(%a0)          | 00e819ea  *patch1 = slot->saved1
        move.l  (4,%a1),%d0             | 00e819ee  slot->patch2
        beq.b   .Luh_no_exit            | 00e819f2
        movea.l %d0,%a0                 | 00e819f4
        move.w  (14,%a1),(%a0)          | 00e819f6  *patch2 = slot->saved2
.Luh_no_exit:
        jsr     CACHE_$CLEAR            | 00e819fa  (only touches D0)
        bclr.b  #7,(16,%a1)             | 00e81a00  clear "running"
        move.w  %d1,%sr                 | 00e81a06
        rts                             | 00e81a08

| ---------------------------------------------------------------------------
| STOP_$HOOK - 0x00E81A0A.
| A0 = patch record, A1 = slot, A2 = parent slot or 0, D0.w = slot number.
| ---------------------------------------------------------------------------
STOP_$HOOK:
        moveq   #15,%d1                 | 00e81a0a  16 longs = the whole slot
        movea.l %a1,%a4                 | 00e81a0c
.Lhk_clear:
        clr.l   (%a4)+                  | 00e81a0e
        dbf     %d1,.Lhk_clear          | 00e81a10
        move.l  %a2,(8,%a1)             | 00e81a14  slot->parent
        movem.l (%a0),%a3-%a4           | 00e81a18  A3 = entry, A4 = exit
        move.w  %sr,%d1                 | 00e81a1c
        ori.w   #0x0700,%sr             | 00e81a1e
        move.l  %a3,(%a1)               | 00e81a22  slot->patch1
        move.w  (%a3),(12,%a1)          | 00e81a24  slot->saved1 = *entry
        move.w  #0xa000,%d2             | 00e81a28  A-line trap base
        add.w   %d0,%d2                 | 00e81a2c  + slot number
        move.w  %d2,(%a3)               | 00e81a2e
        move.l  %a4,(4,%a1)             | 00e81a30  sets Z from A4
        beq.b   .Lhk_no_exit            | 00e81a34
        bset.b  #6,(16,%a1)             | 00e81a36  "has exit patch"
        move.w  (%a4),(14,%a1)          | 00e81a3c  slot->saved2 = *exit
        bset.l  #8,%d2                  | 00e81a40  0xA100 + slot number
        move.w  %d2,(%a4)               | 00e81a44
.Lhk_no_exit:
        bset.b  #7,(16,%a1)             | 00e81a46  "running"
        jsr     CACHE_$CLEAR            | 00e81a4c  (only touches D0)
        move.w  %d1,%sr                 | 00e81a52
        rts                             | 00e81a54

| ---------------------------------------------------------------------------
| STOP_$WATCH_UII - 0x00E81A56.  A-line / unimplemented-instruction vector.
|
| The exception frame's PC is at 0x1E(SP) after the movem below (2 bytes of
| SR at 0x1C plus the 7 saved longwords = 0x1C).  The trapping instruction's
| own word is recovered from the slot and written back so the instruction can
| be re-executed; STOP_$WATCH_TRACE_FLAG then routes the re-trap into the
| trace handler.
| ---------------------------------------------------------------------------
STOP_$WATCH_UII:
        ori.w   #0x0700,%sr             | 00e81a56
        movem.l %d0-%d2/%a0-%a3,-(%sp)  | 00e81a5a
        movea.l (30,%sp),%a0            | 00e81a5e  frame PC
        move.w  (%a0),%d1               | 00e81a62  the A-line word
        clr.w   %d0                     | 00e81a64
        move.b  %d1,%d0                 | 00e81a66  low byte = slot number
        lsl.w   #6,%d0                  | 00e81a68  * 64
        lea     STOPWATCH_SLOTS,%a1     | 00e81a6a  lea (0x2bc,PC),A1
        adda.w  %d0,%a1                 | 00e81a6e
        movea.l (36,%sp),%a3            | 00e81a70
        btst.l  #8,%d1                  | 00e81a74  0xA100 = exit trap
        bne.b   .Luii_exit              | 00e81a78
        move.w  (12,%a1),(%a0)          | 00e81a7a  restore entry word
        bra.b   .Luii_armed             | 00e81a7e
.Luii_exit:
        move.w  (14,%a1),(%a0)          | 00e81a80  restore exit word
.Luii_armed:
        lea     STOP_$WATCH,%a2         | 00e81a84  lea (-0x272,PC),A2 = A5
        movem.l %d0/%a0,-(%sp)          | 00e81a88
        jsr     CACHE_$CLEAR            | 00e81a8c
        movem.l (%sp)+,%d0/%a0          | 00e81a92
        move.b  (28,%sp),%d0            | 00e81a96  saved SR, high byte
        ori.b   #0x87,(28,%sp)          | 00e81a9a  trace + IPL 7 on return
        | 00e81aa0: movem.l ...,(0x3d8,A2)
        movem.l %d0-%d2/%a0-%a3,STOP_$SAVED_REGS
        st      STOP_$WATCH_TRACE_FLAG  | 00e81aa6
        movem.l (%sp)+,%d0-%d2/%a0-%a3  | 00e81aac
        rte                             | 00e81ab0

| ---------------------------------------------------------------------------
| STOP_$WATCH_TRACE - 0x00E81AB2.  Second-stage handler, entered on the trace
| exception raised by the T bit STOP_$WATCH_UII set in the return SR.
| ---------------------------------------------------------------------------
STOP_$WATCH_TRACE:
        movem.l %d0-%d2/%a0-%a3,-(%sp)  | 00e81ab2
        | 00e81ab6: movem.l (0x132,PC),{...}
        movem.l STOP_$SAVED_REGS,%d0-%d2/%a0-%a3
        sf      STOP_$WATCH_TRACE_FLAG  | 00e81abc
        move.b  %d0,(28,%sp)            | 00e81ac2  restore the caller's SR byte
        btst.l  #8,%d1                  | 00e81ac6
        bne.w   .Ltr_exit               | 00e81aca
        | ---- entry trap ----
        move.w  %d1,(%a0)               | 00e81ace  re-arm the entry patch
        move.w  PROC1_$CURRENT,%d1      | 00e81ad0
        move.l  (8,%a1),%d0             | 00e81ad6  slot->parent
        beq.b   .Ltr_begin              | 00e81ada
        movea.l %d0,%a0                 | 00e81adc
        btst.b  #5,(16,%a0)             | 00e81ade  parent in an interval?
        beq.b   .Ltr_done               | 00e81ae4
        cmp.w   (18,%a0),%d1            | 00e81ae6  ... for this process?
        bne.b   .Ltr_done               | 00e81aea
.Ltr_begin:
        bset.b  #5,(16,%a1)             | 00e81aec  claim the interval
        beq.b   .Ltr_start              | 00e81af2
        addq.l  #1,(24,%a1)             | 00e81af4  slot->reentries++
        bra.b   .Ltr_done               | 00e81af8
.Ltr_start:
        move.w  %d1,(18,%a1)            | 00e81afa  slot->owner
        bsr.w   STOP_$READ_CPUT         | 00e81afe
        move.l  %d1,(44,%a1)            | 00e81b00  slot->entry_cput
        move.w  (18,%a1),%d0            | 00e81b04
        lsl.w   #2,%d0                  | 00e81b08
        lea     STOP_$TRAP_COUNTS,%a0   | 00e81b0a  lea (0x104,PC),A0
        move.l  (0,%a0,%d0.w),(52,%a1)  | 00e81b0e  slot->entry_traps
        addq.l  #1,(0,%a0,%d0.w)        | 00e81b14
        bsr.w   STOP_$READ_CLOCK        | 00e81b18
        move.l  %d1,(48,%a1)            | 00e81b1a  slot->entry_clock
        | 00e81b1e: move.l (0xf0,PC),(0x38,A1) -- the global trap counter
        move.l  STOP_$TRAP_COUNTS,(56,%a1)
        btst.b  #6,(16,%a1)             | 00e81b24  exit patch already in?
        bne.b   .Ltr_done               | 00e81b2a
        move.l  %a3,(4,%a1)             | 00e81b2c  patch the return address
        move.w  (%a3),(14,%a1)          | 00e81b30
        suba.l  #STOPWATCH_SLOTS,%a1    | 00e81b34  suba.l #0xe81d28,A1
        move.l  %a1,%d0                 | 00e81b3a
        lsr.w   #6,%d0                  | 00e81b3c  recover the slot number
        add.w   #0xa100,%d0             | 00e81b3e
        move.w  %d0,(%a3)               | 00e81b42
.Ltr_done:
        addq.l  #1,STOP_$TRAP_COUNTS    | 00e81b44  addq.l #1,(0x3fc,A2)
        jsr     CACHE_$CLEAR            | 00e81b48
        movem.l (%sp)+,%d0-%d2/%a0-%a3  | 00e81b4e
        rte                             | 00e81b52

| ---- exit trap (0x00E81B72) ----
.Ltr_exit:
        btst.b  #6,(16,%a1)             | 00e81b72
        beq.b   .Ltr_no_exit_patch      | 00e81b78
        move.w  %d1,(%a0)               | 00e81b7a  re-arm the exit patch
        bra.b   .Ltr_close              | 00e81b7c
.Ltr_no_exit_patch:
        clr.l   (4,%a1)                 | 00e81b7e
.Ltr_close:
        bclr.b  #5,(16,%a1)             | 00e81b82  release the interval
        beq.b   .Ltr_done               | 00e81b88
        move.w  (18,%a1),%d2            | 00e81b8a
        cmp.w   PROC1_$CURRENT,%d2      | 00e81b8e
        bne.b   .Ltr_done               | 00e81b94
        bsr.w   STOP_$READ_CPUT         | 00e81b96
        sub.l   (44,%a1),%d1            | 00e81b98  - entry_cput
        sub.l   STOP_$SW_OVERHEAD,%d1   | 00e81b9c  sub.l (0x6a,PC),D1
        bge.b   .Ltr_cpu_ok             | 00e81ba0
        clr.l   %d1                     | 00e81ba2  clamp at 0
.Ltr_cpu_ok:
        add.l   %d1,(28,%a1)            | 00e81ba4  slot->cpu_time
        lsl.w   #2,%d2                  | 00e81ba8
        lea     STOP_$TRAP_COUNTS,%a0   | 00e81baa  lea (0x64,PC),A0
        move.l  (0,%a0,%d2.w),%d1       | 00e81bae
        addq.l  #1,%d1                  | 00e81bb2
        move.l  %d1,(0,%a0,%d2.w)       | 00e81bb4
        subq.l  #2,%d1                  | 00e81bb8
        sub.l   (52,%a1),%d1            | 00e81bba  - entry_traps
        add.l   %d1,(36,%a1)            | 00e81bbe  slot->cpu_events
        bsr.w   STOP_$READ_CLOCK        | 00e81bc2
        sub.l   (48,%a1),%d1            | 00e81bc4  - entry_clock
        sub.l   STOP_$SW_OVERHEAD,%d1   | 00e81bc8  sub.l (0x3e,PC),D1
        bge.b   .Ltr_clk_ok             | 00e81bcc
        clr.l   %d1                     | 00e81bce
.Ltr_clk_ok:
        add.l   %d1,(32,%a1)            | 00e81bd0  slot->elapsed_time
        move.l  STOP_$TRAP_COUNTS,%d1   | 00e81bd4  move.l (0x3a,PC),D1
        subq.l  #1,%d1                  | 00e81bd8
        sub.l   (56,%a1),%d1            | 00e81bda  - entry_gtraps
        add.l   %d1,(40,%a1)            | 00e81bde  slot->elapsed_events
        addq.l  #1,(20,%a1)             | 00e81be2  slot->completions
        bra.w   .Ltr_done               | 00e81be6

| ---------------------------------------------------------------------------
| STOP_$READ_CPUT (0x00E81B54) / STOP_$READ_CLOCK (0x00E81B5C)
|
| Both call a Pascal procedure that fills a 6-byte (48-bit) clock through a
| by-reference argument and return the low 32 bits of that value in D1.  The
| `addq.w #6,%sp` pops the pushed pointer plus the top 2 bytes of the buffer,
| so the following `move.l (%sp)+,%d1` picks up bytes 2..5 -- the low
| longword of the 48-bit value -- and balances the 10 bytes pushed.
| A1 is saved and restored; D0, D1 and A0 are clobbered.
| ---------------------------------------------------------------------------
STOP_$READ_CPUT:
        lea     PROC1_$GET_CPUT,%a0     | 00e81b54
        bra.b   .Lrc_call               | 00e81b5a
STOP_$READ_CLOCK:
        lea     TIME_$CLOCK,%a0         | 00e81b5c
.Lrc_call:
        move.l  %a1,-(%sp)              | 00e81b62
        subq.w  #6,%sp                  | 00e81b64  the 48-bit result buffer
        pea     (%sp)                   | 00e81b66
        jsr     (%a0)                   | 00e81b68
        addq.w  #6,%sp                  | 00e81b6a
        move.l  (%sp)+,%d1              | 00e81b6c
        movea.l (%sp)+,%a1              | 00e81b6e
        rts                             | 00e81b70

| ===========================================================================
| C-callable wrappers
|
| The four routines above take their arguments in registers, so stop/watch.c
| reaches them through these shims.  They are the only additions to the
| original code in this file; each one only marshals registers and preserves
| what the SysV m68k C ABI requires (D2-D7, A2-A6).
| ===========================================================================

| int32_t stop_$measure_loop(void)
stop_$measure_loop:
        move.l  %d2,-(%sp)              | STOP_$MEASURE_LOOP clobbers D2
        bsr.w   STOP_$MEASURE_LOOP
        move.l  %d1,%d0                 | the result comes back in D1
        move.l  (%sp)+,%d2
        rts

| void stop_$hook(const stop_$patch_rec_t *rec, stopwatch_slot_t *slot,
|                 stopwatch_slot_t *parent, uint32_t slotno)
stop_$hook:
        movem.l %d2/%a2-%a4,-(%sp)      | 16 bytes
        movea.l (20,%sp),%a0            | rec
        movea.l (24,%sp),%a1            | slot
        movea.l (28,%sp),%a2            | parent
        move.w  (34,%sp),%d0            | low word of the slotno longword
        bsr.w   STOP_$HOOK
        movem.l (%sp)+,%d2/%a2-%a4
        rts

| void stop_$unhook(stopwatch_slot_t *slot)
| STOP_$UNHOOK only touches D0, D1, A0 and A1, all scratch under the C ABI,
| so this is a tail call.
stop_$unhook:
        movea.l (4,%sp),%a1
        bra.w   STOP_$UNHOOK
