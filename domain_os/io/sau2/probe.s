/*
 * io_$probe - Hardware probe with bus error recovery
 *
 * Touches a device register with interrupts off and a temporary bus-error
 * handler installed, and reports whether the access completed.  Used by
 * flp/, prom/, peb/, win/ and smd/ to decide whether a controller is
 * present.
 *
 * Original address: 0x00E29138
 * Extent: 0x00E29138 .. 0x00E29197 (the 54-byte entry Ghidra knows about
 * plus the jump table, the four probe bodies, the common exit and the
 * bus-error recovery stub that follow it).
 *
 * Image bytes (`gsk read 0xe29138 0x60`):
 *   00e29138  48 e7 10 28 20 6f 00 10  30 10 e3 48 22 6f 00 14
 *   00e29148  22 51 24 6f 00 18 40 c3  00 7c 07 00 41 fa 00 3c
 *   00e29158  23 c8 00 e2 18 cc 22 0f  42 39 00 ff b4 03 41 fa
 *   00e29168  00 06 4e f0 00 00 60 0c  60 0e 60 04 32 92 60 0a
 *   00e29178  12 92 60 06 14 91 60 02  34 91 50 c0 42 b9 00 e2
 *   00e29188  18 cc 46 c3 4c df 14 08  4e 75 2e 41 51 c0 60 ec
 *
 * Register usage:
 *   A1 = the hardware address (two levels of indirection from param_2)
 *   A2 = the caller's data buffer
 *   D0 = jump table byte offset (*param_1 * 2), then the result byte
 *   D1 = SP saved for the bus-error stub to restore
 *   D3 = saved SR
 *
 * Parameters (stack, all by reference):
 *   param_1 = pointer to the probe-type word (0..3)
 *   param_2 = pointer to a pointer to the hardware location
 *   param_3 = pointer to the data buffer read into or written from
 *
 * Probe types (the four jump-table cases):
 *   0 = read  byte:  move.b (A1),(A2)
 *   1 = read  word:  move.w (A1),(A2)
 *   2 = write byte:  move.b (A2),(A1)
 *   3 = write word:  move.w (A2),(A1)
 * The type word is not range checked; a value outside 0..3 jumps into the
 * middle of the code that follows the table.
 *
 * Returns:
 *   D0.b = 0xFF (st) if the access completed, 0 (sf) if it bus-errored.
 *   Domain booleans are 0xFF/0x00 and are tested with tst.b / bmi, so
 *   "found" is the negative value.
 *
 * This is hand-written assembly -- it takes a SR snapshot, patches the
 * global bus-error vector cell, longjmps back through a saved SP, and
 * dispatches through a table of 2-byte branches -- so it is transcribed
 * rather than translated (see CLAUDE.md).
 */

        .section ".text.io_$probe","ax",@progbits
        .even

        /* Patch cell read by FIM_$BUS_ERR: when non-zero the bus-error
         * handler jumps here instead of crashing (fim/sau2/bus_err.s). */
        .equ    BUS_ERROR_SWITCH,   0x00E218CC
        .equ    MMU_STATUS_REG,     0x00FFB403

        .globl  io_$probe
        .globl  _io_$probe
io_$probe:
_io_$probe:
        movem.l %d3/%a2/%a4, -(%sp)     /* 0x00E29138 12 bytes saved       */

        /* Arguments are at SP0+4/+8/+0xC; SP is now 12 lower. */
        movea.l (0x10,%sp), %a0         /* 0x00E2913C A0 = &type           */
        move.w  (%a0), %d0              /* 0x00E29140 D0.w = type          */
        lsl.w   #1, %d0                 /* 0x00E29142 * 2 = table offset   */

        movea.l (0x14,%sp), %a1         /* 0x00E29144 A1 = &hw_addr        */
        movea.l (%a1), %a1              /* 0x00E29148 A1 = hw_addr         */

        movea.l (0x18,%sp), %a2         /* 0x00E2914A A2 = data buffer     */

        move    %sr, %d3                /* 0x00E2914E save SR              */
        ori     #0x0700, %sr            /* 0x00E29150 IPL 7                */

        lea     .Lbus_error(%pc), %a0   /* 0x00E29154 -> 0x00E29192        */
        move.l  %a0, BUS_ERROR_SWITCH   /* 0x00E29158 arm the recovery     */

        move.l  %sp, %d1                /* 0x00E2915E SP for the stub      */

        clr.b   MMU_STATUS_REG          /* 0x00E29160 fresh fault status   */

        lea     .Ljump_table(%pc), %a0  /* 0x00E29166 -> 0x00E2916E        */
        jmp     (0,%a0,%d0.w)           /* 0x00E2916A                      */

.Ljump_table:                           /* 0x00E2916E                      */
        bra.b   .Lread_byte             /* 0x00E2916E type 0 -> 0x00E2917C */
        bra.b   .Lread_word             /* 0x00E29170 type 1 -> 0x00E29180 */
        bra.b   .Lwrite_byte            /* 0x00E29172 type 2 -> 0x00E29178 */
                                        /* type 3 falls into the body at   */
                                        /* 0x00E29174, which is the table  */
                                        /* entry itself                    */
.Lwrite_word:
        move.w  (%a2), (%a1)            /* 0x00E29174                      */
        bra.b   .Lprobe_ok              /* 0x00E29176 -> 0x00E29182        */

.Lwrite_byte:
        move.b  (%a2), (%a1)            /* 0x00E29178                      */
        bra.b   .Lprobe_ok              /* 0x00E2917A                      */

.Lread_byte:
        move.b  (%a1), (%a2)            /* 0x00E2917C                      */
        bra.b   .Lprobe_ok              /* 0x00E2917E                      */

.Lread_word:
        move.w  (%a1), (%a2)            /* 0x00E29180                      */
                                        /* falls through                   */
.Lprobe_ok:
        st      %d0                     /* 0x00E29182 D0.b = 0xFF          */

.Lexit:
        clr.l   BUS_ERROR_SWITCH        /* 0x00E29184 disarm the recovery  */
        move    %d3, %sr                /* 0x00E2918A restore SR/IPL       */
        movem.l (%sp)+, %d3/%a2/%a4     /* 0x00E2918C                      */
        rts                             /* 0x00E29190                      */

.Lbus_error:                            /* 0x00E29192, entered from        */
                                        /* FIM_$BUS_ERR, not by a branch   */
        movea.l %d1, %sp                /* 0x00E29192 unwind to the probe  */
        sf      %d0                     /* 0x00E29194 D0.b = 0x00          */
        bra.b   .Lexit                  /* 0x00E29196 -> 0x00E29184        */
