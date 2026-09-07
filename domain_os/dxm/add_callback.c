/*
 * dxm/add_callback.c - DXM_$ADD_CALLBACK implementation
 *
 * Adds a callback to a deferred execution queue.
 *
 * Original address: 0x00E16FE0  (372 bytes)
 */

#include "dxm/dxm_internal.h"

/*
 * ===========================================================================
 * PC-relative constant cells inside the function's own code region
 * ===========================================================================
 *
 * Both of these are reached with `pea (d16,PC)`; the target address is the
 * address of the extension word (instruction + 2) plus d16.
 *
 *   0x00E1700A  pea (0x158,PC)   -> 0x00E1700C + 0x158 = 0x00E17164
 *   0x00E170CC  pea (0x86,PC)    -> 0x00E170CE + 0x86  = 0x00E17154
 *
 * 0x00E17164 holds the four bytes 00 17 00 02, i.e. the status longword
 * 0x00170002 = "datum too large for deferred execution" (stcode.db.10.4,
 * subsystem 0x17 "deferred execution module manager").  It is a status
 * constant passed by reference to CRASH_SYSTEM, not a string; the previous
 * translation modelled it as a character array (audit failure mode 1).
 *
 * 0x00E17154 holds the crash-console string
 *   28 44 58 4d 29 20 4e 6f 20 72 6f 6f 6d 25
 *   '('  'D' 'X' 'M' ')' ' ' 'N' 'o' ' ' 'r' 'o' 'o' 'm' '%'
 * The trailing '%' (0x25) is crash_puts_string's end-of-string marker, so the
 * console shows "(DXM) No room" followed by CR LF.  (The two bytes that
 * follow at 0x00E17162, 0x20 0x48, are padding before the status cell and are
 * not part of the string.)
 */
static const status_$t dxm_$datum_too_large_00e17164 = 0x00170002;
static const char dxm_$no_room_msg_00e17154[] = "(DXM) No room%";

/*
 * DXM_$ADD_CALLBACK - Add a callback to a deferred execution queue
 *
 * Pascal parameter list, recovered from the callee's A6 displacements and
 * confirmed against all six call sites (each cleans up exactly 20 bytes):
 *
 *   +0x08  queue       longword   0x00E16FEE  movea.l (0x8,A6),A2
 *   +0x0C  callback    longword   0x00E1705C  movea.l (0xc,A6),A3
 *   +0x10  data        longword   0x00E16FF2  movea.l (0x10,A6),A3
 *   +0x14  data_size   word       0x00E16FF6  move.w  (0x14,A6),D4w
 *   +0x16  check_dup   boolean    0x00E16FFA  move.b  (0x16,A6),D2b
 *   +0x18  status_ret  longword   0x00E16FFE  movea.l (0x18,A6),A0
 *
 * check_dup occupies a two-byte stack slot whose *high* byte carries the
 * Domain boolean; callers push it with `st -(SP)` (kbd, tty, ast, suma) or
 * `move.b (d,A6),-(SP)` (DXM_$ADD_SIGNAL, TIME_$Q_SCAN_QUEUE), both of which
 * predecrement A7 by two and land the byte at the slot's low address.
 *
 * `callback` points at a dxm_$callback_t cell holding the callback's 4-byte
 * code address (source-wy9y: a native function pointer would make the queue
 * entry 24 bytes on a 64-bit host); `data` points at a cell holding the
 * address of the bytes to copy.
 *
 * Behaviour:
 *   - data_size > 12 crashes (unsigned compare at 0x00E17004).
 *   - The data is copied into a 12-byte frame local before the lock is taken.
 *   - With check_dup true the queue is scanned from head to tail for an entry
 *     with the same callback address and the same data_size leading bytes; a
 *     match suppresses the insert.
 *   - A full queue sets *status_ret, prints on the crash console, calls
 *     CRASH_SYSTEM and bumps DXM_$OVERRUNS.
 *   - A successful insert advances the tail and signals the queue eventcount.
 */
void DXM_$ADD_CALLBACK(dxm_queue_t *queue, const dxm_$callback_t *callback,
                       void **data, uint16_t data_size, boolean check_dup,
                       status_$t *status_ret)
{
    uint32_t num_words;      /* D5 */
    ml_$spin_token_t token;  /* (-0x22,A6) */
    uint16_t idx;            /* D3: scan cursor */
    uint16_t next_tail;      /* D0 */
    int16_t i;
    dxm_entry_t *entry;
    boolean found_dup;       /* (-0x24,A6), set with `st`, cleared with clr.b */

    /*
     * The frame reserves 12 bytes at (-0x10,A6) for the copy.  Note that if
     * CRASH_SYSTEM below ever returned with data_size > 12 the original would
     * copy past this local too; the overflow is reproduced, not guarded.
     */
    uint32_t local_data[3];  /* (-0x10,A6) .. (-0x5,A6) */

    /* 0x00E17002  clr.l (A0) */
    *status_ret = status_$ok;

    /* 0x00E17004  cmpi.w #0xc,D4w / bls -- unsigned */
    if (data_size > DXM_MAX_DATA_SIZE) {
        CRASH_SYSTEM(&dxm_$datum_too_large_00e17164);
    }

    /*
     * 0x00E17016  clr.l D5 / move.w D4w,D5w / addq.l #3,D5 / lsr.l #2,D5
     * num_words = (data_size + 3) / 4, computed on the zero-extended word.
     */
    num_words = ((uint32_t)data_size + 3) >> 2;

    /*
     * 0x00E1701E  tst.w D5w / beq ... / dbf loop at 0x00E1702C.
     * dst walks (-0x10,A6) upward, src walks *data upward.
     */
    if ((uint16_t)num_words != 0) {
        int16_t count = (int16_t)(uint16_t)num_words;
        for (i = 0; i < count; i++) {
            local_data[i] = ((const uint32_t *)*data)[i];
        }
    }

    /* 0x00E1703C  pea (0x8,A2) / jsr ML_$SPIN_LOCK / move.w D0w,(-0x22,A6) */
    token = ML_$SPIN_LOCK(&queue->lock);

    /* 0x00E1704C  tst.b D2b / bpl -- Domain boolean, true is negative */
    if (check_dup < 0) {
        /* 0x00E17050  move.w (A2),D3w ; idx = head */
        for (idx = queue->head; idx != queue->tail;
             idx = (uint16_t)(idx + 1) & queue->mask) {
            /*
             * 0x00E17056  movea.l (0x18,A2),A0 / move.w D3w,D0w
             * 0x00E17060  lsl.l #0x4,D0 / lea (0x0,A0,D0*0x1),A0
             * The scan index is scaled as a *longword* (no truncation).
             */
            entry = (dxm_entry_t *)((uint8_t *)queue->entries +
                                    ((uint32_t)idx << 4));

            /* 0x00E17066  movea.l (A0),A1 / cmpa.l (A3),A1 */
            if (entry->callback != *callback) {
                continue;
            }

            /* 0x00E1706C  st (-0x24,A6) */
            found_dup = true;

            /* 0x00E1707A  tst.w D4w / beq -- zero-length data always matches */
            if (data_size != 0) {
                const uint8_t *local_bytes = (const uint8_t *)local_data;
                int16_t n = (int16_t)data_size;

                /* 0x00E17088 .. 0x00E1709E, D1 runs 1..data_size */
                for (i = 0; i < n; i++) {
                    if (local_bytes[i] != entry->data[i]) {
                        found_dup = false; /* 0x00E17096 clr.b (-0x24,A6) */
                        break;
                    }
                }
            }

            /* 0x00E170A2  tst.b (-0x24,A6) / bmi 0x00E170E8 */
            if (found_dup < 0) {
                goto unlock_and_return;
            }
        }
    }

    /* 0x00E170B4  next_tail = (tail + 1) & mask */
    next_tail = (uint16_t)(queue->tail + 1) & queue->mask;

    /* 0x00E170BE  cmp.w (A2),D0w / bne -- queue full when it meets head */
    if (next_tail == queue->head) {
        /* 0x00E170C6  move.l #0x170001,(A0) */
        *status_ret = status_$dxm_no_more_deferred_execution_queue_slots;
        /* 0x00E170CC  pea (0x86,PC) / jsr CRASH_SHOW_STRING */
        CRASH_SHOW_STRING(dxm_$no_room_msg_00e17154);
        /* 0x00E170D8  move.l (0x18,A6),-(SP) / jsr CRASH_SYSTEM */
        CRASH_SYSTEM(status_ret);
        /* 0x00E170E4  addq.l #0x1,(0x600,A5) */
        DXM_$OVERRUNS++;
        goto unlock_and_return;
    }

    /*
     * 0x00E170FA  move.w (0x2,A2),D1w / lsl.w #0x4,D1w
     *             lea (0x0,A0,D1w*0x1),A0
     * Unlike the scan above, the insert index is scaled as a *word* and then
     * used as a sign-extended word displacement.  Reproduced exactly.
     */
    entry = (dxm_entry_t *)((uint8_t *)queue->entries +
                            (int32_t)(int16_t)(uint16_t)(queue->tail << 4));

    /* 0x00E17110  move.l (A3),(A0) */
    entry->callback = *callback;

    /* 0x00E17112  tst.w D5w / dbf loop at 0x00E17120 */
    if ((uint16_t)num_words != 0) {
        uint32_t *dst = (uint32_t *)entry->data;
        int16_t count = (int16_t)(uint16_t)num_words;
        for (i = 0; i < count; i++) {
            dst[i] = local_data[i];
        }
    }

    /* 0x00E1712A  move.w D0w,(0x2,A2) */
    queue->tail = next_tail;

    /* 0x00E1712E  ML_$SPIN_UNLOCK / 0x00E17140  EC_$ADVANCE_WITHOUT_DISPATCH */
    ML_$SPIN_UNLOCK(&queue->lock, token);
    EC_$ADVANCE_WITHOUT_DISPATCH(&queue->ec);
    return;

unlock_and_return:
    /* 0x00E170E8 */
    ML_$SPIN_UNLOCK(&queue->lock, token);
}
