/*
 * AST_$PURIFY - Purify (write back dirty pages) an object
 *
 * Writes back dirty pages for an object to disk. Can operate on
 * all segments or a specific segment. Handles both local and remote
 * objects.
 *
 * Parameters:
 *   uid - Pointer to object UID
 *   flags - Operation flags (bit 0: specific segment, bit 4: by index)
 *   segment - Segment number (if flags & 1)
 *   segment_list - List of segment indices (if flags & 0x10)
 *   unused - Unused parameter
 *   status - Status return
 *
 * Returns: Flags indicating what was purified
 *
 * Original address: 0x00e0567a
 */

#include "ast.h"

/* Internal function prototypes */
extern void FUN_00e0209e(uid_t *uid);  /* Look up AOTE by UID */
extern void FUN_00e020fa(uid_t *uid, uint16_t segment, status_$t *status, int8_t force);
extern void FUN_00e013a0(aote_t *aote, uint16_t flags, status_$t *status);
extern void TIME_$CLOCK(uint32_t *clock);
extern void TIME_$ABS_CLOCK(uint32_t *clock, uint32_t delta);

uint16_t AST_$PURIFY(uid_t *uid, uint16_t flags, int16_t segment,
                     uint32_t *segment_list, uint16_t unused, status_$t *status)
{
    aote_t *aote;
    aste_t *aste;
    uid_t local_uid;
    status_$t local_status;
    uint16_t result;
    int8_t update_time;

    /* Check for invalid flags */
    if ((flags & 0x7FE0) != 0) {
        *status = status_$ast_incompatible_request;
        return flags & 0x7FE0;
    }

    /* Copy UID locally */
    local_uid.high = uid->high;
    local_uid.low = uid->low;

    result = 0;
    local_status = status_$ok;
    update_time = 0;

    PROC1_$INHIBIT_BEGIN();
    ML_$LOCK(AST_LOCK_ID);

    /* Look up AOTE by UID */
    FUN_00e0209e(&local_uid);
    aote = NULL;  /* TODO: Get from FUN_00e0209e return in A0 */

    if (aote == NULL) {
        goto done;
    }

    /* Mark AOTE as in-transition and busy */
    aote->flags |= AOTE_FLAG_IN_TRANS;
    aote->flags |= AOTE_FLAG_BUSY;

    /* Iterate through ASTEs */
    int iter_count = 4;
    aste = *(aste_t **)((char *)aote + 4);

    while (aste != NULL) {
        int match = 0;

        if ((flags & 0x10) == 0) {
            /* Not by index */
            if ((flags & 1) != 0) {
                match = (aste->segment == segment);
            } else {
                match = 1;  /* Match all */
            }
        } else {
            /* By index - check segment list */
            uint32_t seg_offset = (uint32_t)aste->segment;
            match = (seg_offset == (segment_list[iter_count - 4] >> 5));
        }

        if (match) {
            /* Process this ASTE */
            /* TODO: Implement full purification logic */
            update_time = -1;
        }

        aste = aste->next;
    }

    /* Update timestamps if needed */
    if (update_time < 0) {
        ML_$LOCK(PMAP_LOCK_ID);
        TIME_$CLOCK((uint32_t *)((char *)aote + 0x28));

        /* Copy current time to modification time */
        *(uint32_t *)((char *)aote + 0x40) = *(uint32_t *)((char *)aote + 0x28);
        *(uint16_t *)((char *)aote + 0x44) = *(uint16_t *)((char *)aote + 0x2C);

        /* Set access time */
        TIME_$ABS_CLOCK((uint32_t *)((char *)aote + 0x38), 0);

        ML_$UNLOCK(PMAP_LOCK_ID);
        aote->flags |= AOTE_FLAG_DIRTY;
    }

    /* Flush to disk */
    FUN_00e013a0(aote, 0, &local_status);

    /* Clear in-transition flag */
    aote->flags &= ~AOTE_FLAG_IN_TRANS;
    EC_$ADVANCE(&AST_$AST_IN_TRANS_EC);

    result = *(uint8_t *)((char *)aote + 0xB8);

done:
    ML_$UNLOCK(AST_LOCK_ID);
    PROC1_$INHIBIT_END();

    *status = local_status;
    return result;
}
