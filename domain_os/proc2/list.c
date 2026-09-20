/*
 * PROC2_$LIST - List process UIDs
 *
 * Re-emitted from the image (0x00E402F0..0x00E40400, 274 bytes).
 *
 * Under a FIM cleanup handler and the PROC2 lock: uid_list[0] is entry 1's
 * UID (0xEA551C), then every allocated entry whose flags low byte has bit 7
 * set (0x0080) and whose ASID is not 1 is appended.  The count reported is
 * min(total qualifying + 1, capped max).
 *
 * Frame (link.w A6,-0x28; A5 = 0xE7BE84):
 *   (0x8,A6)  uid_list       (0xC,A6) max_count ptr   (0x10,A6) count ptr
 *   A6-0x18   FIM cleanup record (0x18 bytes)   A6-0x1C  FIM's status
 *   A6-0x20   capped max     A6-0x22  running count (starts at 1)
 *
 * Callers: ASKNODE_$INTERNET_INFO 0x00E6499A, SVC table 0x00E7B6F6.
 *
 * Original address: 0x00e402f0
 */

#include "proc2/proc2_internal.h"

void PROC2_$LIST(uid_t *uid_list, uint16_t *max_count, uint16_t *count)
{
    uint8_t fim_context[0x18];   /* A6-0x18 */
    status_$t status;            /* A6-0x1C */
    uint16_t max;                /* A6-0x20 */
    uint16_t n;                  /* A6-0x22 */
    int16_t index;               /* D0 */
    proc2_info_t *entry;         /* A0 (biased) */
    uid_t *out;                  /* A1 */
    uint16_t d0;

    /* 0x00E402FE-0x00E40312: n = 1; max = min(*max_count, 57) (bls, unsigned) */
    d0 = *max_count;
    n = 1;
    if (d0 > 0x39) {
        d0 = 0x39;
    }
    max = d0;

    /* 0x00E40316-0x00E40322 */
    status = FIM_$CLEANUP(fim_context);

    /* 0x00E40326: cmpi.l #0x120035 */
    if (status == status_$cleanup_handler_set) {
        /* 0x00E40330-0x00E4033C */
        ML_$LOCK(PROC2_LOCK_ID);

        /* 0x00E4033E-0x00E40350: uid_list[0] = entry(1)->uid when max != 0 */
        if (max != 0) {
            uid_list[0].high = P2_INFO_ENTRY(1)->uid.high;
            uid_list[0].low = P2_INFO_ENTRY(1)->uid.low;
        }

        /* 0x00E40354-0x00E4035E: D0 = alloc ptr, A1 = &uid_list[1]; beq
         * tests the move.w (addq.l to An sets no flags) */
        index = (int16_t)P2_INFO_ALLOC_PTR;
        out = &uid_list[1];
        while (index != 0) {
            entry = P2_INFO_ENTRY(index);                    /* 0x00E40360-0x00E4036C */

            /* 0x00E40370: tst.b (-0xb9,A0) / bpl -- LOW byte bit 7 = 0x0080
             * 0x00E40376-0x00E4037C: asid == 1 -> skip */
            if ((entry->flags & 0x0080) != 0 && entry->asid != 1) {
                n += 1;                                      /* 0x00E4037E */
                out++;                                       /* 0x00E40382 */
                /* 0x00E40384-0x00E4038C: n > max (bhi) -> no copy */
                if (n <= max) {
                    out[-1].high = entry->uid.high;          /* 0x00E4038E-0x00E40396 */
                    out[-1].low = entry->uid.low;
                }
            }
            index = (int16_t)entry->next_index;              /* 0x00E4039A */
        }

        /* 0x00E403A0-0x00E403B8 */
        ML_$UNLOCK(PROC2_LOCK_ID);
        FIM_$RLS_CLEANUP(fim_context);

        /* 0x00E403BA-0x00E403D4: *count = n; if (*count < max) max = *count;
         * if (*count > max) *count = max  (both unsigned) */
        *count = n;
        d0 = *count;
        if (d0 < max) {
            max = d0;
        }
        if (d0 > max) {
            *count = max;
        }
    } else {
        /* 0x00E403DA-0x00E403F6: the handler fired -- pop it, unlock, 0 */
        FIM_$POP_SIGNAL(fim_context);
        ML_$UNLOCK(PROC2_LOCK_ID);
        *count = 0;
    }
}
