/*
 * PROC2_$LIST_PGROUP - List the members of a process group
 *
 * Re-emitted from the image (0x00E401EA..0x00E402EE, 262 bytes).
 *
 * Frame (link.w A6,-0x2C; A5 = 0xE7BE84):
 *   (0x8,A6)  pgroup_uid ptr (pushed by value to UID_TO_PGROUP_INDEX)
 *   (0xC,A6)  uid_list   (0x10,A6) max_count ptr   (0x14,A6) count ptr
 *   A6-0x18 FIM record  A6-0x1C FIM status  A6-0x22 capped max  A6-0x24 found
 *
 * Only reference: the SVC table entry at 0x00E7B9E2.
 *
 * Original address: 0x00e401ea
 */

#include "proc2/proc2_internal.h"

void PROC2_$LIST_PGROUP(uid_t *pgroup_uid, uid_t *uid_list, uint16_t *max_count,
                        uint16_t *count)
{
    uint8_t fim_context[0x18];   /* A6-0x18 */
    status_$t status;            /* A6-0x1C */
    uint16_t max;                /* A6-0x22 */
    uint16_t n;                  /* A6-0x24 */
    int16_t pgroup_idx;          /* D0 */
    int16_t index;               /* D1 */
    proc2_info_t *entry;         /* A0 (biased) */
    uid_t *out;                  /* A1 */
    uint16_t d0;

    /* 0x00E401F8-0x00E4020A: n = 0; max = min(*max_count, 57), unsigned */
    d0 = *max_count;
    n = 0;
    if (d0 > 0x39) {
        d0 = 0x39;
    }
    max = d0;

    /* 0x00E4020E-0x00E4021A */
    status = FIM_$CLEANUP(fim_context);

    /* 0x00E4021E */
    if (status == status_$cleanup_handler_set) {
        /* 0x00E40228-0x00E40234 */
        ML_$LOCK(PROC2_LOCK_ID);

        /* 0x00E40236-0x00E40242: result slot; a zero index skips the walk */
        pgroup_idx = PROC2_$UID_TO_PGROUP_INDEX(pgroup_uid);
        if (pgroup_idx != 0) {
            /* 0x00E40244-0x00E4024E: D1 = alloc ptr (the beq tests that move) */
            index = (int16_t)P2_INFO_ALLOC_PTR;
            out = uid_list;
            while (index != 0) {
                entry = P2_INFO_ENTRY(index);                /* 0x00E40250-0x00E4025C */

                /* 0x00E40260: tst.b (-0xb9,A0) / bpl = flags & 0x0080;
                 * 0x00E40266: entry+0x10 == pgroup_idx */
                if ((entry->flags & 0x0080) != 0 &&
                    entry->pgroup_table_idx == (uint16_t)pgroup_idx) {
                    n += 1;                                  /* 0x00E4026C */
                    out++;                                   /* 0x00E40270 */
                    /* 0x00E40272-0x00E4027A: n > max (bhi) -> no copy */
                    if (n <= max) {
                        out[-1].high = entry->uid.high;      /* 0x00E4027C-0x00E40284 */
                        out[-1].low = entry->uid.low;
                    }
                }
                index = (int16_t)entry->next_index;          /* 0x00E40288 */
            }
        }

        /* 0x00E4028E-0x00E402A6 */
        ML_$UNLOCK(PROC2_LOCK_ID);
        FIM_$RLS_CLEANUP(fim_context);

        /* 0x00E402A8-0x00E402C2: *count = n; if (*count < max) max = *count;
         * if (*count > max) *count = max */
        *count = n;
        d0 = *count;
        if (d0 < max) {
            max = d0;
        }
        if (d0 > max) {
            *count = max;
        }
    } else {
        /* 0x00E402C8-0x00E402E4 */
        FIM_$POP_SIGNAL(fim_context);
        ML_$UNLOCK(PROC2_LOCK_ID);
        *count = 0;
    }
}
