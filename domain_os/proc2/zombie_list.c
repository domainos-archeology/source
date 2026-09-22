/*
 * PROC2_$ZOMBIE_LIST - List zombie processes with a start index
 *
 * Re-emitted from the image (0x00E40548..0x00E4068C, 326 bytes).  The
 * same shape as PROC2_$LIST2 (0x00E40402) with the filter flags 0x0100
 * AND 0x2000 (btst #8 / btst #13 on the word at +0x2A).
 *
 * Frame (link.w A6,-0x34; A5 = 0xE7BE84): (0x8,A6) uid_list, (0xC,A6)
 * max_count ptr, (0x10,A6) count ptr, (0x14,A6) start_index ptr (long ->
 * A6-0x20), (0x18,A6) more_flag byte (cleared 0x00E40568), (0x1C,A6)
 * last_index long (cleared 0x00E4056E); A6-0x22 last copied slot, A6-0x24
 * last qualifying slot, A6-0x28 found, A6-0x2A capped max.
 *
 * Callers: ASKNODE_$INTERNET_INFO 0x00E649E0.
 *
 * Original address: 0x00e40548
 */

#include "proc2/proc2_internal.h"

void PROC2_$ZOMBIE_LIST(uid_t *uid_list, uint16_t *max_count, uint16_t *count,
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

    /* 0x00E40556-0x00E40578 */
    d0 = *max_count;
    start = *start_index;
    *more_flag = 0;
    *last_index = 0;
    last_qualified = 0;
    last_copied = 0;
    found = 0;

    /* 0x00E4057C-0x00E40584: max = min(*max_count, 57), unsigned */
    if (d0 > 0x39) {
        d0 = 0x39;
    }
    max = d0;

    /* 0x00E40588-0x00E40594 */
    status = FIM_$CLEANUP(fim_context);

    /* 0x00E40598 */
    if (status == status_$cleanup_handler_set) {
        /* 0x00E405A2-0x00E405AE */
        ML_$LOCK(PROC2_LOCK_ID);

        /* 0x00E405B0-0x00E405C2: 57 iterations from slot 1 */
        out = uid_list;
        slot = 1;
        for (i = 0; i < 57; i++, slot++) {
            entry = P2_INFO_ENTRY(slot);

            /* 0x00E405C6-0x00E405D4: btst #8 then btst #13 */
            if ((entry->flags & 0x0100) != 0 && (entry->flags & PROC2_FLAG_ZOMBIE) != 0) {
                last_qualified = slot;                       /* 0x00E405D6 */
                /* 0x00E405DA-0x00E405E2: (int32)slot < start -> skip */
                if ((int32_t)slot >= start) {
                    found += 1;                              /* 0x00E405E4 */
                    out++;                                   /* 0x00E405E8 */
                    if (found <= max) {                      /* 0x00E405EA-0x00E405F2 */
                        out[-1].high = entry->uid.high;      /* 0x00E405F4-0x00E405FC */
                        out[-1].low = entry->uid.low;
                        last_copied = slot;                  /* 0x00E40600 */
                    }
                }
            }
        }

        /* 0x00E4060E-0x00E40626 */
        ML_$UNLOCK(PROC2_LOCK_ID);
        FIM_$RLS_CLEANUP(fim_context);

        /* 0x00E40628-0x00E40638 */
        *count = found;
        d2 = *count;
        if (d2 < max) {
            max = d2;
        }

        /* 0x00E4063C-0x00E40658 */
        if (last_qualified > last_copied) {
            *more_flag = (int8_t)0xFF;                       /* 0x00E4064A: st */
            *last_index = (int32_t)last_copied + 1;          /* 0x00E4064C-0x00E40658 */
        }

        /* 0x00E4065A-0x00E40660 */
        if (d2 > max) {
            *count = max;
        }
    } else {
        /* 0x00E40666-0x00E40682 */
        FIM_$POP_SIGNAL(fim_context);
        ML_$UNLOCK(PROC2_LOCK_ID);
        *count = 0;
    }
}
