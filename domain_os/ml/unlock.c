/*
 * ML_$UNLOCK - Release a resource lock
 *
 * Releases a resource lock and wakes any waiting processes.  May trigger
 * rescheduling if a higher-priority process was waiting.
 *
 * Original address: 0x00E20B62 (136 bytes, plus the shared epilogue at
 * 0x00E20EB0-0x00E20EEE which it branches into).
 *
 * Full instruction trace:
 *   00e20b62  lea (0x60,PC),A0          ; A0 = 0xE20BC4 = ML_$LOCK_BYTES
 *   00e20b66  move.w (0x4,SP),D0w       ; resource_id
 *   00e20b6a  ori #0x700,SR             ; raise to IPL 7 (no SR saved)
 *   00e20b6e  bclr.b #0x0,(0x0,A0,D0w)  ; release the lock byte
 *   00e20b74  lsl.w #0x4,D0w            ; 16-byte stride into LOCK_EVENTS
 *   00e20b76  move.l (0x20,A0,D0w),D1   ; ev->ec.value   (0xE20BE4 + off)
 *   00e20b7a  cmp.l (0x2c,A0,D0w),D1    ; ev->wait_count (0xE20BE4 + off + 0xC)
 *   00e20b7e  beq.b 0x00e20b88
 *   00e20b80  lea (0x20,A0,D0w),A0
 *   00e20b84  bsr.w 0x00e2072c          ; ADVANCE_INT(&ev->ec)
 *   00e20b88  movea.l (-0x20c2,PC),A1   ; A1 = PROC1_$CURRENT_PCB (0xE1EAC8)
 *   00e20b8c  move.w (0x4,SP),D0w
 *   00e20b90  bra.b 0x00e20b9e
 *   00e20b9e  move.l (0x40,A1),D1       ; resource_locks_held
 *   00e20ba2  bclr.l D0,D1              ; clear bit (resource_id mod 32)
 *   00e20ba4  beq.b 0x00e20b56          ; bit was clear -> CRASH_SYSTEM
 *   00e20ba6  move.l D1,(0x40,A1)
 *   00e20baa  subq.w #0x1,(0x5a,A1)     ; --nesting_depth
 *   00e20bae  beq.w 0x00e20eb0          ; ==0: clear resource_locks_held bit 0
 *   00e20bb2  bra.w 0x00e20eb6          ; !=0: straight into the shared tail
 *
 *   (0x00e20b56: pea (0x28c,PC) -> the status cell at 0x00E20DE4
 *                jsr CRASH_SYSTEM
 *                bra.b 0x00e20b56       ; loops forever)
 *
 * The function shares its exit path (0x00E20EB6) with ML_$EXCLUSION_STOP;
 * see proc1_$release_tail() in proc1/proc1.h.
 */

#include "ml/ml_internal.h"

/*
 * Status cells passed to CRASH_SYSTEM by `pea (d,PC)`.
 *
 * These are constant longwords in this module's own code region, not
 * shared globals; the cell address is part of each name.  Names come from
 * the SR10.4 status-code database.
 */
/*
 * Reached through the shared crash tail at 0x00E20B56 (`beq.b 0x00E20B56` at
 * 0x00E20BA4): `pea (0x28c,PC)` -> the cell at 0x00E20DE4.  The same cell
 * serves proc1_$set_lock_body's lock-ordering check (`bls.b 0x00E20B56` at
 * 0x00E20AF8) and PROC1_$CLR_LOCK; it is emitted once, as the global
 * `Illegal_lock_err`, in proc1/sau2/set_lock.s.  This file-static copy
 * exists only because ML_$UNLOCK is C and must hand CRASH_SYSTEM the
 * address of a cell with that value.
 */
static const status_$t proc1_$illegal_lock_00e20de4 = 0x000A0002;

void ML_$UNLOCK(int16_t resource_id)
{
    proc1_t *pcb;
    ml_$lock_event_t *evp;
    uint32_t lock_mask;
    uint32_t locks;

    /* 0x00E20B6A: ori #0x700,SR -- no SR is saved; the exit forces IPL 0 */
    SET_IPL7();

    /* 0x00E20B6E: bclr.b #0,(0,A0,D0w) */
    ML_$LOCK_BYTES[resource_id] &= (uint8_t)~0x01;

    /*
     * 0x00E20B76/0x00E20B7A: if the event count has not caught up with the
     * number of waits issued, wake the waiters.
     */
    evp = &ML_$LOCK_EVENTS[resource_id];
    if (evp->ec.value != evp->wait_count) {
        /* 0x00E20B84 `lea &ev->ec,A0; bsr ADVANCE_INT`: a register call,
         * made from C through the inline wrapper in ec/ec.h (source-rg5a) */
        ADVANCE_INT(&evp->ec);
    }

    /* 0x00E20B88 */
    pcb = PROC1_$CURRENT_PCB;

    /* 0x00E20BA2: bclr.l D0,D1 -- the bit number is taken modulo 32 */
    lock_mask = 1U << (resource_id & 0x1F);
    locks = pcb->resource_locks_held;

    if ((locks & lock_mask) == 0) {
        /* 0x00E20B56: crash, and the crash site loops back onto itself */
        for (;;) {
            CRASH_SYSTEM(&proc1_$illegal_lock_00e20de4);
        }
    }

    /* 0x00E20BA6 */
    pcb->resource_locks_held = locks & ~lock_mask;

    /* 0x00E20BAA */
    pcb->nesting_depth--;

    if (pcb->nesting_depth == 0) {
        /*
         * 0x00E20EB0: bclr.b #0x0,(0x43,A1).  0x43 is the least significant
         * byte of the longword at 0x40 (big-endian), so this clears bit 0
         * of resource_locks_held.
         */
        pcb->resource_locks_held &= ~1u;
    }

    /* 0x00E20EB6: shared epilogue; ends with a forced IPL 0. */
    proc1_$release_tail(pcb);
}
