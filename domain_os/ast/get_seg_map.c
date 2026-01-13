/*
 * AST_$GET_SEG_MAP - Get segment map entries for an object
 *
 * Retrieves the segment map entries for a range of pages in an object.
 * Used for debugging, diagnostics, and inter-node operations.
 *
 * Parameters:
 *   uid - Pointer to object UID info
 *   start_offset - Starting byte offset
 *   unused - Unused parameter
 *   vol_uid - Volume UID
 *   count - Number of pages to retrieve
 *   flags - Operation flags
 *   output - Output buffer for segment map data
 *   status - Status return
 *
 * Original address: 0x00e06b1e
 */

#include "ast.h"

/* Internal function prototypes */
extern void FUN_00e0209e(uid_t *uid);  /* Look up AOTE by UID */
extern void FUN_00e020fa(uid_t *uid, uint16_t segment, status_$t *status, int8_t force);
extern aste_t* FUN_00e0250c(aote_t *aote, int16_t segment);
extern aste_t* FUN_00e0255c(aote_t *aote, int16_t segment, status_$t *status);

void AST_$GET_SEG_MAP(uint32_t *uid_info, uint32_t start_offset, uint32_t unused,
                      uid_t *vol_uid, uint32_t count, uint16_t flags,
                      uint32_t *output, status_$t *status)
{
    aote_t *aote;
    aste_t *aste;
    uint32_t *segmap_ptr;
    uint16_t start_segment;
    uint16_t end_segment;
    int i;

    *status = status_$ok;

    /* Clear output buffer (8 uint32_t = 32 bytes header) */
    for (i = 0; i < 8; i++) {
        output[i] = 0;
    }

    /* Calculate segment range */
    vol_uid->high = start_offset << 15;
    uint32_t aligned_offset = start_offset & 0xFFFFFC00;
    start_segment = (uint16_t)(start_offset >> 15);
    end_segment = start_segment;

    if (count > 0x20) {
        end_segment = start_segment + (uint16_t)((count << 10) >> 15) - 1;
    }

    if (start_segment > end_segment) {
        return;
    }

    /* Process each segment */
    int16_t segments_remaining = end_segment - start_segment;

    do {
        PROC1_$INHIBIT_BEGIN();
        ML_$LOCK(AST_LOCK_ID);

        /* Look up AOTE */
        FUN_00e0209e((uid_t *)uid_info);
        aote = NULL;  /* TODO: Get from FUN_00e0209e return in A0 */

        if (aote == NULL) {
            /* Try to load the AOTE */
            FUN_00e020fa((uid_t *)uid_info, 0, status, 0);
            aote = NULL;  /* TODO: Get from FUN_00e020fa return in A0 */

            if (aote == NULL) {
                ML_$UNLOCK(AST_LOCK_ID);
                PROC1_$INHIBIT_END();
                return;
            }
        } else {
            aote->flags |= AOTE_FLAG_BUSY;
        }

        /* Find or create ASTE for this segment */
        aste = FUN_00e0250c(aote, start_segment);
        if (aste == NULL) {
            /* Create new ASTE */
            aste = FUN_00e0255c(aote, start_segment, status);
            if (aste == NULL) {
                ML_$UNLOCK(AST_LOCK_ID);
                PROC1_$INHIBIT_END();
                return;
            }
        }

        /* Get segment map pointer */
        segmap_ptr = (uint32_t *)((uint32_t)aste->seg_index * 0x80 + SEGMAP_BASE - 0x80);

        ML_$LOCK(PMAP_LOCK_ID);

        /* Copy segment map entries to output */
        for (i = 0; i < 32 && i < (int)count; i++) {
            /* Wait for page not in transition */
            while (*(int16_t *)segmap_ptr < 0) {
                /* Page in transition - wait */
                ML_$UNLOCK(PMAP_LOCK_ID);
                ML_$UNLOCK(AST_LOCK_ID);
                PROC1_$INHIBIT_END();
                /* Brief pause then retry */
                PROC1_$INHIBIT_BEGIN();
                ML_$LOCK(AST_LOCK_ID);
                ML_$LOCK(PMAP_LOCK_ID);
            }

            output[i + 8] = *segmap_ptr;  /* Skip header */
            segmap_ptr++;
        }

        ML_$UNLOCK(PMAP_LOCK_ID);
        ML_$UNLOCK(AST_LOCK_ID);
        PROC1_$INHIBIT_END();

        start_segment++;
        segments_remaining--;
        output += 32;
        count -= 32;

    } while (segments_remaining >= 0 && count > 0);
}
