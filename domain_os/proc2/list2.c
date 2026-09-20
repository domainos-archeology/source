/*
 * PROC2_$LIST2 - List processes with a start index
 *
 * Re-emitted from the image (0x00E40402..0x00E40546, 326 bytes).
 *
 * Scans slots 1..57 (`moveq #0x38` + dbf) and collects the UID of every
 * slot whose flags carry both 0x0100 (btst #8) and 0x0080 (tst.b low
 * byte / bpl) and whose index is >= *start_index.  Reports min(found,
 * capped max); when a qualifying slot lies beyond the last one copied,
 * *more_flag is set TRUE (0xFF) and *last_index = last copied slot + 1.
 *
 * Frame (link.w A6,-0x34; A5 = 0xE7BE84):
 *   (0x8,A6)  uid_list        (0xC,A6)  max_count ptr   (0x10,A6) count ptr
 *   (0x14,A6) start_index ptr (long -> A6-0x20)
 *   (0x18,A6) more_flag ptr (byte, cleared at 0x00E40422)
 *   (0x1C,A6) last_index ptr (long, cleared at 0x00E40428)
 *   A6-0x18 FIM record  A6-0x1C FIM status  A6-0x22 last slot copied
 *   A6-0x24 last slot that qualified  A6-0x28 found  A6-0x2A capped max
 *
 * Sole caller: ASKNODE_$INTERNET_INFO 0x00E64A02.
 *
 * Original address: 0x00e40402
 */

#include "proc2/proc2_internal.h"

void PROC2_$LIST2(uid_t *uid_list, uint16_t *max_count, uint16_t *count,
                  int32_t *start_index, int8_t *more_flag, int32_t *last_index)
{
    uint8_t fim_context[0x18];   /* A6-0x18 */
    status_$t status;            /* A6-0x1C */
    int32_t start;               /* A6-0x20 */
    int16_t last_copied;         /* A6-0x22 */
    int16_t last_qualified;      /* A6-0x24 */
    uint16_t found;              /* A6-0x28 */
    uint16_t max;                /* A6-0x2A */
    uint16_t d0, d2;
    int16_t slot;                /* D1 */
    proc2_info_t *entry;         /* A0 (biased) */
    uid_t *out;                  /* A1 */
    int i;

    /* 0x00E40410-0x00E40432 */
    d0 = *max_count;
    start = *start_index;
    *more_flag = 0;
    *last_index = 0;
    last_qualified = 0;
    last_copied = 0;
    found = 0;

    /* 0x00E40436-0x00E4043E: max = min(*max_count, 57), unsigned */
    if (d0 > 0x39) {
        d0 = 0x39;
    }
    max = d0;

    /* 0x00E40442-0x00E4044E */
    status = FIM_$CLEANUP(fim_context);

    /* 0x00E40452 */
    if (status == status_$cleanup_handler_set) {
        /* 0x00E4045C-0x00E40468 */
        ML_$LOCK(PROC2_LOCK_ID);

        /* 0x00E4046A-0x00E4047C: 57 iterations from slot 1, A1 = uid_list */
        out = uid_list;
        slot = 1;
        for (i = 0; i < 57; i++, slot++) {
            entry = P2_INFO_ENTRY(slot);

            /* 0x00E40482-0x00E4048E: btst #8 then tst.b low byte / bpl */
            if ((entry->flags & 0x0100) != 0 && (entry->flags & 0x0080) != 0) {
                last_qualified = slot;                       /* 0x00E40490 */

                /* 0x00E40494-0x00E4049C: (int32)slot < start -> skip (blt) */
                if ((int32_t)slot >= start) {
                    found += 1;                              /* 0x00E4049E */
                    out++;                                   /* 0x00E404A2 */
                    /* 0x00E404A4-0x00E404AC: found > max (bhi) -> no copy */
                    if (found <= max) {
                        out[-1].high = entry->uid.high;      /* 0x00E404AE-0x00E404B6 */
                        out[-1].low = entry->uid.low;
                        last_copied = slot;                  /* 0x00E404BA */
                    }
                }
            }
        }

        /* 0x00E404C8-0x00E404E0 */
        ML_$UNLOCK(PROC2_LOCK_ID);
        FIM_$RLS_CLEANUP(fim_context);

        /* 0x00E404E2-0x00E404F2: *count = found; if (*count < max) max = *count */
        *count = found;
        d2 = *count;
        if (d2 < max) {
            max = d2;
        }

        /* 0x00E404F6-0x00E40512: a qualifying slot after the last copied one
         * (signed ble) -> *more_flag = TRUE, *last_index = last_copied + 1 */
        if (last_qualified > last_copied) {
            *more_flag = (int8_t)0xFF;                       /* 0x00E40504: st */
            *last_index = (int32_t)last_copied + 1;          /* 0x00E40506-0x00E40512 */
        }

        /* 0x00E40514-0x00E4051A: if (*count > max) *count = max */
        if (d2 > max) {
            *count = max;
        }
    } else {
        /* 0x00E40520-0x00E4053C */
        FIM_$POP_SIGNAL(fim_context);
        ML_$UNLOCK(PROC2_LOCK_ID);
        *count = 0;
    }
}
