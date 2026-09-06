/*
 * stop/watch.c - STOP_$WATCH
 *
 * Original address: 0x00E81814, 352 bytes.
 *
 * One entry point with two unrelated jobs:
 *
 *   operations 0 and 1  start/stop one of 16 stopwatch slots
 *   operations 2 .. 7   privileged physical peek/poke, dispatched through
 *                       the two-byte branch table at 0x00E81854 and, for the
 *                       three poke operations, gated by DISK_$DIAG
 *
 * The whole body runs under a FIM cleanup handler established at 0x00E81824.
 *
 * Every basic block of the original is accounted for below; the addresses in
 * the comments are the instructions each statement stands for.
 */

#include "stop/stop_internal.h"

#include "arch/arch.h"
#include "fim/fim.h"
#include "mst/mst.h"

/*
 * Ghidra names for the branch-table arms (all inside STOP_$WATCH's body):
 *   0x00E8186A  diagnostic gate, with a non-local escape
 *   0x00E8187E  peek byte      0x00E81882  poke byte
 *   0x00E81888  peek word      0x00E8188C  poke word
 *   0x00E81892  peek long      0x00E81862  poke long
 */

void STOP_$WATCH(int16_t *operation, uint16_t *slot, int16_t *parent, void *p4,
                 void *p5, status_$t *status)
{
    status_$t d2_status;  /* D2 */
    int16_t d3_operation; /* D3 */
    uint8_t cleanup_ctx[20]; /* (-0x14,A6), handed to FIM_$CLEANUP */

    /*
     * 0x00E81820-0x00E81834: establish the cleanup handler.  On the fault
     * return FIM_$CLEANUP gives back something other than
     * status_$cleanup_handler_set and we drop straight to the status store
     * at 0x00E81968 without releasing the handler.
     */
    d2_status = FIM_$CLEANUP(cleanup_ctx);
    if (d2_status != status_$cleanup_handler_set) {
        *status = d2_status;
        return;
    }

    d3_operation = *operation; /* 0x00E81838-0x00E8183C */

    /*
     * 0x00E8183E-0x00E81854: anything above 1 goes through the branch table.
     * Note the original only tests `cmp.w #1,D3 / ble` -- there is no upper
     * bound, so operations >= 8 branch into the middle of the table's own
     * instruction stream.
     */
    if (d3_operation > 1) {
        /*
         * 0x00E81844-0x00E8184E: A1 = *(uint32 *)p4 is the address to
         * touch, D1 = *(uint32 *)p5 is the value to poke.  D2 (the returned
         * status, and the peek result register) is cleared first.
         */
        volatile uint8_t *addr = (volatile uint8_t *)(*(uint32_t **)p4);
        uint32_t d1_value = *(uint32_t *)p5;
        uint32_t d2_value = 0; /* 0x00E81850: clr.l D2 */
        boolean is_poke;
        boolean is_peek;

        switch (d3_operation) {
        case STOP_OP_PEEK_BYTE: /* 2 */
        case STOP_OP_PEEK_WORD: /* 4 */
        case STOP_OP_PEEK_LONG: /* 6 */
            is_peek = true;
            is_poke = false;
            break;
        case STOP_OP_POKE_BYTE: /* 3 */
        case STOP_OP_POKE_WORD: /* 5 */
        case STOP_OP_POKE_LONG: /* 7 */
            is_peek = false;
            is_poke = true;
            break;
        default:
            /*
             * TODO(source-3ena): operations >= 8 index past the end of the
             * branch table at 0x00E81854 (whose last entry, for operation 7,
             * is the four-byte `bsr.w` at 0x00E81862) and land in the middle
             * of instructions.  The original has no bound check; there is no
             * defined behaviour to reproduce, so nothing is done here and
             * the status stays 0.
             */
            is_peek = false;
            is_poke = false;
            break;
        }

        if (is_poke < 0) {
            /*
             * 0x00E8186A-0x00E8187C: the gate is a `bsr` that either
             * returns (diagnostics enabled) or pops its own return address
             * (`addq.l #4,SP`), loads 0x300004 into D2 and branches straight
             * to the cleanup release.  DISK_$DIAG is compared against zero
             * with `tst.b / bne`, not with the usual boolean `< 0`.
             */
            if (DISK_$DIAG == 0) {
                d2_status = status_$stop_not_diag;
                goto release_cleanup;
            }
            switch (d3_operation) {
            case STOP_OP_POKE_BYTE: /* 0x00E81884: move.b D1,(A1) */
                *addr = (uint8_t)d1_value;
                break;
            case STOP_OP_POKE_WORD: /* 0x00E8188E: move.w D1,(A1) */
                *(volatile uint16_t *)addr = (uint16_t)d1_value;
                break;
            default: /* 0x00E81866: move.l D1,(A1) */
                *(volatile uint32_t *)addr = d1_value;
                break;
            }
        } else if (is_peek < 0) {
            switch (d3_operation) {
            case STOP_OP_PEEK_BYTE:
                /* 0x00E8187E: move.b (A1),D2b -- D2 was cleared, so the
                 * byte is zero extended into the full longword. */
                d2_value = *addr;
                break;
            case STOP_OP_PEEK_WORD: /* 0x00E81888: move.w (A1),D2w */
                d2_value = *(volatile uint16_t *)addr;
                break;
            default: /* 0x00E81892: move.l (A1),D2 */
                d2_value = *(volatile uint32_t *)addr;
                break;
            }
            /* 0x00E81894: move.l D2,(A0) -- the result goes back through p5 */
            *(uint32_t *)p5 = d2_value;
        }

        d2_status = status_$ok; /* 0x00E81896: clr.l D2 */
        goto release_cleanup;   /* 0x00E81898: bra.w 0x00E8195C */
    }

    /*
     * 0x00E8189C-0x00E818C0: wire the stopwatch region down the first time
     * through, so the trace handler never takes a page fault.  The five
     * arguments are all PC-relative cells in this module's data, plus a
     * 16-byte scratch buffer carved out of the stack (`suba.w #0x10,SP`).
     */
    if (STOPWATCH_WIRED == 0) {
        uint8_t wire_buf[16];
        MST_$WIRE_AREA(&PTR_STOP_$WATCH, &PTR_OS_DATA_SHUTWIRED, wire_buf,
                       &STOPWATCH_WIRE_COUNT, &STOPWATCH_WIRED);
    }

    /*
     * 0x00E818C4-0x00E81900: one-time calibration.  There is no separate
     * "initialised" flag: `tst.l (0x3f8,A5)` tests STOP_$CALIBRATION, whose
     * low word is what the calibration stores.
     */
    if (STOP_$CALIBRATION == 0) {
        int32_t base;    /* D3, saved across the block on the stack */
        int32_t measured;/* D1 */

        base = stop_$measure_loop(); /* 0x00E818CC-0x00E818CE */

        /*
         * 0x00E818D0-0x00E818E0: hook slot 0 onto the bare rts that the
         * measurement loop calls (A0 = &STOP_$CALIB_PATCH, A1 =
         * &STOPWATCH_SLOTS[0], A2 = NULL, D0 = 0).
         */
        stop_$hook(&STOP_$CALIB_PATCH, &STOPWATCH_SLOTS[0], NULL, 0);

        /* 0x00E818E4-0x00E818E6: run it again, now instrumented */
        measured = stop_$measure_loop() - base;

        /*
         * 0x00E818E8-0x00E818EC: `divu.w #0x800,D1` -- the loop runs 1024
         * iterations and each takes an entry trap, so the divisor of 2048
         * charges half a trap's cost per event.  Only the low word of the
         * quotient is stored, into the low half of STOP_$CALIBRATION.
         */
        STOP_$CALIBRATION =
            (STOP_$CALIBRATION & (int32_t)0xFFFF0000) |
            (int32_t)(uint16_t)(((uint32_t)measured) / 0x800);

        /*
         * 0x00E818F0-0x00E818F8: the same interval as the stopwatch itself
         * accumulated it.  A1 still points at slot 0 across the calls above
         * (stop_$measure_loop saves and restores A1 around PROC1_$GET_CPUT
         * and CACHE_$CLEAR only touches D0).  Divisor 0x400 = the 1024
         * iterations.
         */
        STOP_$SW_OVERHEAD =
            (STOP_$SW_OVERHEAD & (int32_t)0xFFFF0000) |
            (int32_t)(uint16_t)(((uint32_t)STOPWATCH_SLOTS[0].cpu_time) /
                                0x400);

        stop_$unhook(&STOPWATCH_SLOTS[0]); /* 0x00E818FC */
        /* 0x00E81900 restores D3 (the operation) from the stack */
    }

    /* 0x00E81902-0x00E81914: unsigned bound check against 15 */
    {
        uint16_t slot_num = *slot;
        stopwatch_slot_t *sl;

        if (slot_num > STOP_MAX_SLOTS - 1) {
            d2_status = status_$stop_bad_slot;
            goto release_cleanup;
        }

        /* 0x00E81940-0x00E81948: A1 = &STOPWATCH_SLOTS[slot_num] */
        sl = &STOPWATCH_SLOTS[slot_num];

        /* 0x00E8194A: tst.w D3 / ble -> the stop path */
        if (d3_operation > 0) {
            /* ---- START (operation 1) ---------------------------------- */

            /* 0x00E8194E: btst.b #7,(0x10,A1) */
            if ((sl->flags & STOP_SLOT_RUNNING) != 0) {
                d2_status = status_$stop_already_running; /* 0x00E81956 */
                goto release_cleanup;
            }

            /*
             * 0x00E81978-0x00E81992: A0 = p4 (the patch record), A2 = the
             * parent slot or NULL.  Only `blt` guards the parent index: a
             * negative value means "no parent" and anything non-negative is
             * scaled by 64 and used, with no upper bound.
             */
            {
                stopwatch_slot_t *parent_slot = NULL;
                int16_t parent_idx = *parent;

                if (parent_idx >= 0) {
                    /*
                     * TODO(source-3ena): the original has no upper bound on
                     * the parent index here (0x00E81988 is `blt` only), so a
                     * parent >= 16 indexes past STOPWATCH_SLOTS.  Reproduced
                     * as written.
                     */
                    parent_slot = &STOPWATCH_SLOTS[parent_idx];
                }

                stop_$hook((const stop_$patch_rec_t *)p4, sl, parent_slot,
                           slot_num);
            }
            d2_status = status_$ok; /* 0x00E81994: clr.l D2 */
        } else {
            /* ---- STOP (operation 0, or any negative operation) --------- */

            /* 0x00E81998: btst.b #7,(0x10,A1) */
            if ((sl->flags & STOP_SLOT_RUNNING) == 0) {
                /* 0x00E8199E branches back to the 0x300001 store */
                d2_status = status_$stop_bad_slot;
                goto release_cleanup;
            }

            /*
             * 0x00E819A2-0x00E819D8: a true SR save/restore around the
             * whole read-modify-write, so the trace handler cannot run in
             * the middle of it.
             */
            {
                uint16_t saved_sr; /* D2 in the original */
                uint32_t calibration; /* D0, then D1 */
                uint32_t overhead;
                int32_t *src;
                int32_t *dst;
                int16_t i;

                DISABLE_INTERRUPTS(saved_sr);

                /*
                 * 0x00E819A8-0x00E819C2.  `mulu.w` multiplies the low words
                 * only: the low word of STOP_$CALIBRATION by the low word of
                 * the event count.  Each `clr.l` clears a whole longword
                 * event counter, and does so after the multiply has read it.
                 */
                calibration = (uint32_t)STOP_$CALIBRATION;

                overhead = (uint32_t)(uint16_t)calibration *
                           (uint32_t)(uint16_t)sl->cpu_events;
                sl->cpu_events = 0;               /* clr.l (0x24,A1) */
                sl->cpu_time -= (int32_t)overhead;/* sub.l D0,(0x1c,A1) */

                overhead = (uint32_t)(uint16_t)calibration *
                           (uint32_t)(uint16_t)sl->elapsed_events;
                sl->elapsed_events = 0;               /* clr.l (0x28,A1) */
                sl->elapsed_time -= (int32_t)overhead;/* sub.l D1,(0x20,A1) */

                /*
                 * 0x00E819C6-0x00E819D4: copy four longwords from slot+0x14
                 * into *p5, zeroing the slot as it goes.  `moveq #3 / dbf`
                 * is four iterations.
                 */
                src = &sl->completions;
                dst = (int32_t *)p5;
                for (i = 3; i >= 0; i--) {
                    *dst++ = *src;
                    *src++ = 0;
                }

                ENABLE_INTERRUPTS(saved_sr); /* 0x00E819D8: move D2w,SR */
            }

            /*
             * 0x00E819DA-0x00E819E0: operation 0 leaves the patches in
             * place (the slot keeps running, its totals just having been
             * harvested); a negative operation additionally unhooks it.
             */
            if (d3_operation != 0) {
                stop_$unhook(sl);
            }
            d2_status = status_$ok; /* 0x00E81994 */
        }
    }

release_cleanup:
    /* 0x00E8195C-0x00E81966 */
    FIM_$RLS_CLEANUP(cleanup_ctx);

    /* 0x00E81968-0x00E8196C */
    *status = d2_status;
}
