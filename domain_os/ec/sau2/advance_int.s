/*
 * ADVANCE / ADVANCE_INT / ADVANCE_ALL_INT - the eventcount advance engine
 *
 * SAU2 map domain_os.10.2.map, PROC1_ASM segment (0xE1EAC8, size 0x24A4):
 *
 *      E20718  EC_$ADVANCE_WITHOUT_DISPATCH     MARKED
 *      E20728  ADVANCE                          MARKED
 *      E207D4  PROC1_$REORDER_READY
 *
 * so the map's ADVANCE object is 0xE20728..0xE207D3, 0xAC bytes, and it holds
 * all three entries below.  Only ADVANCE is a map symbol; ADVANCE_INT
 * (0xE2072C) and ADVANCE_ALL_INT (0xE207C6) are interior labels.
 *
 * This is hand-written assembly, not compiled Pascal:
 *   - ADVANCE_INT and ADVANCE_ALL_INT take the eventcount in %a0, a register
 *     argument, and are reached by bsr from ec/sau2/advance.s,
 *     ec/sau2/advance_all.s, ec/sau2/advance_without_dispatch.s and
 *     proc1/sau2/int_handler.s.
 *   - There is no link/unlk frame; %d7 and %a3 are saved with two separate
 *     `move.l ...,-(%sp)' rather than a compiler's movem prologue.
 *   - ADVANCE_ALL_INT branches into the MIDDLE of ADVANCE_INT (0xE20736),
 *     past ADVANCE_INT's own %d7 setup - impossible to express in Pascal or C.
 *   - The call to PROC1_$SET_TS is bracketed by a hand-picked
 *     `movem.l %d7/%a0-%a1/%a3' save/restore pair around it.
 *   - ADVANCE is a 4-byte entry that only loads the stack argument into %a0
 *     and then FALLS THROUGH into ADVANCE_INT.
 *
 * ADVANCE is the C-callable wrapper; its callers in the image are 0xE14760,
 * 0xE7566E, 0xE26FE6 and 0xE27060.
 *
 * The three eventcount fields used here (ec_$eventcount_t):
 *      0x00  value             (int32)
 *      0x04  waiter_list_head  (circular, the ec itself is the sentinel)
 * and of a waiter (ec_$eventcount_waiter_t):
 *      0x00  wait_val          (int32)
 *      0x04  prev_waiter
 *      0x0c  pcb
 *
 * Section note: the four ec/sau2 objects use their own `.text.ec_*' sections
 * rather than plain `.text'.  gas fixes the pre-created .text section's
 * alignment at 2**2 and offers no directive to lower it, which pads
 * advance_all.o (0x16 bytes, ending 2-mod-4) out to 0x18 and shifts everything
 * after it off the image's gaps.  A section created by `.section' starts at
 * 2**0 and `.balign 2' raises it to exactly the m68k requirement, so the run
 * links contiguously.  sau2.ld names these sections explicitly.
 *
 * Original address: 0x00e20728
 */

        .section .text.ec_advance_int,"ax",@progbits
        .balign 2

/*
 * gas always spells `cmp.l #imm,%dN' as CMPI.L (0x0C80); the image uses the
 * CMP.L #imm,Dn form (0xB0BC), for which gas offers no syntax.  Same macro as
 * svc/sau2/svc_macros.inc's cmp_l_imm, kept local so the generic %.o: %.s rule
 * needs no .include dependency.  <dreg> is a register NUMBER, not %dN.
 */
        .macro  cmp_l_imm imm, dreg
        .short  0xb0bc | (\dreg << 9)   /* CMP.L #<imm>,D<dreg> */
        .long   \imm
        .endm

        .globl  ADVANCE
        .globl  _ADVANCE
        .globl  ADVANCE_INT
        .globl  _ADVANCE_INT
        .globl  ADVANCE_ALL_INT
        .globl  _ADVANCE_ALL_INT

/*
 * ADVANCE(ec) - C-callable entry.  Loads the eventcount pointer from the
 * stack and falls through into the register-argument ADVANCE_INT.
 */
ADVANCE:
_ADVANCE:
        movea.l (4,%sp),%a0             /* 0xE20728  20 6f 00 04           */

/*
 * ADVANCE_INT - %a0 = eventcount.  Bumps the value and releases every waiter
 * whose wait_val has been reached.  Called with interrupts already disabled.
 */
ADVANCE_INT:
_ADVANCE_INT:
        move.l  %d7,-(%sp)              /* 0xE2072C  2f 07                 */
        move.l  %a3,-(%sp)              /* 0xE2072E  2f 0b                 */
        move.l  (%a0),%d7               /* 0xE20730  2e 10  d7 = ec->value */
        addq.l  #1,%d7                  /* 0xE20732  52 87                 */
        move.l  %d7,(%a0)               /* 0xE20734  20 87                 */

        /* ADVANCE_ALL_INT joins here, with %d7 left as the caller had it. */
advance_scan:
        movea.l (4,%a0),%a3             /* 0xE20736  26 68 00 04  head     */
        cmpa.l  %a3,%a0                 /* 0xE2073A  b1 cb  list empty?    */
        beq.w   advance_done            /* 0xE2073C  67 00 00 82           */

advance_loop:
        move.l  %d7,%d0                 /* 0xE20740  20 07                 */
        sub.l   (%a3),%d0               /* 0xE20742  90 93  - wait_val     */
        tst.l   %d0                     /* 0xE20744  4a 80  (redundant)    */
        blt.b   advance_next            /* 0xE20746  6d 70  not reached    */

        movea.l (0xc,%a3),%a1           /* 0xE20748  22 6b 00 0c  pcb      */
        bclr.b  #0,(0x55,%a1)           /* 0xE2074C  08 a9 00 00 00 55     */
        beq.b   advance_next            /* 0xE20752  67 64  was not waiting*/

        move.l  (TIME_$CLOCKH).l,%d0    /* 0xE20754  20 39 00 e2 b0 d4     */
        sub.l   (0x3c,%a1),%d0          /* 0xE2075A  90 a9 00 3c  wait time*/
        cmp_l_imm 1, 0                  /* 0xE2075E  b0 bc 00 00 00 01     */
        bcs.b   advance_ready           /* 0xE20764  65 42  waited 0 ticks */

        cmp_l_imm 0x11, 0               /* 0xE20766  b0 bc 00 00 00 11     */
        bhi.b   advance_use_max         /* 0xE2076C  62 0a                 */
        add.w   (0x52,%a1),%d0          /* 0xE2076E  d0 69 00 52           */
        cmp.w   (0x58,%a1),%d0          /* 0xE20772  b0 69 00 58           */
        ble.b   advance_set_ts          /* 0xE20776  6f 0a                 */

advance_use_max:
        move.w  (0x58,%a1),%d0          /* 0xE20778  30 29 00 58           */
        cmp.w   (0x52,%a1),%d0          /* 0xE2077C  b0 69 00 52           */
        ble.b   advance_store_only      /* 0xE20780  6f 22  no raise       */

advance_set_ts:
        move.w  %d0,(0x52,%a1)          /* 0xE20782  33 40 00 52           */
        movem.l %d7/%a0-%a1/%a3,-(%sp)  /* 0xE20786  48 e7 01 d0           */
        add.w   %d0,%d0                 /* 0xE2078A  d0 40  word index     */
        /*
         * PROC1_$TSVV is the map's name for the 18-entry word table at
         * 0xE205D2 (0xE205D2..0xE205F5), indexed 1-based by the new priority -
         * the same 0x12 bound the `cmp.l #0x11' above enforces (proc1/proc1_data.c).
         */
        lea     (PROC1_$TSVV:w,%pc),%a0 /* 0xE2078C  41 fa fe 44 -> E205D2 */
        move.w  (-2,%a0,%d0.w),-(%sp)   /* 0xE20790  3f 30 00 fe  tsvv[pri]*/
        move.l  %a1,-(%sp)              /* 0xE20794  2f 09  pcb            */
        jsr     (PROC1_$SET_TS).l       /* 0xE20796  4e b9 00 e1 4a 08     */
        addq.w  #6,%sp                  /* 0xE2079C  5c 4f                 */
        movem.l (%sp)+,%d7/%a0-%a1/%a3  /* 0xE2079E  4c df 0b 80           */
        bra.b   advance_ready           /* 0xE207A2  60 04                 */

advance_store_only:
        move.w  %d0,(0x52,%a1)          /* 0xE207A4  33 40 00 52           */

advance_ready:
        btst.b  #1,(0x55,%a1)           /* 0xE207A8  08 29 00 01 00 55     */
        bne.b   advance_next            /* 0xE207AE  66 08  inhibited      */
        move.l  %a0,-(%sp)              /* 0xE207B0  2f 08  save ec        */
        bsr.w   proc1_$insert_into_ready_list /* 0xE207B2  61 00 00 90  ->E20844 */
        movea.l (%sp)+,%a0              /* 0xE207B6  20 5f                 */

advance_next:
        movea.l (4,%a3),%a3             /* 0xE207B8  26 6b 00 04  prev     */
        cmpa.l  %a3,%a0                 /* 0xE207BC  b1 cb                 */
        bne.b   advance_loop            /* 0xE207BE  66 80                 */

advance_done:
        movea.l (%sp)+,%a3              /* 0xE207C0  26 5f                 */
        move.l  (%sp)+,%d7              /* 0xE207C2  2e 1f                 */
        rts                             /* 0xE207C4  4e 75                 */

/*
 * ADVANCE_ALL_INT - %a0 = eventcount.  Slams the value to 0x7FFFFFFF and
 * rejoins the scan.
 *
 * NOTE (image behaviour, preserved): the branch target is advance_scan, which
 * is PAST ADVANCE_INT's `move.l (%a0),%d7'.  %d7 is saved and restored here,
 * so the wait_val comparison at advance_loop runs against whatever %d7 held on
 * entry, NOT against 0x7FFFFFFF.  The only caller is EC_$ADVANCE_ALL
 * (0xE20702), which does not set %d7 either.  ec/advance_all_int.c used to
 * model this as `new_value = 0x7FFFFFFF', which is not what the image does.
 */
ADVANCE_ALL_INT:
_ADVANCE_ALL_INT:
        move.l  %d7,-(%sp)              /* 0xE207C6  2f 07                 */
        move.l  %a3,-(%sp)              /* 0xE207C8  2f 0b                 */
        move.l  #0x7fffffff,(%a0)       /* 0xE207CA  20 bc 7f ff ff ff     */
        bra.w   advance_scan            /* 0xE207D0  60 00 ff 64           */

        /* end 0xE207D4 = PROC1_$REORDER_READY; 0xAC bytes total */
