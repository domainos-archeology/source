/*
 * AST_$GET_SEG_MAP - Get segment map entries for an object
 *
 * Retrieves the segment map entries for a range of pages in an object.
 * Used for debugging, diagnostics, and inter-node operations.
 *
 * Parameters:
 *   uid - Pointer to the object UID
 *   start_offset - Starting byte offset
 *   unused - Unused parameter
 *   seg_count - Number of 32KB segments, passed BY VALUE
 *               (`pea (0x1).w` at 0x00E4BB16, read with
 *               `move.l (0x14,A6),D1` at 0x00E06B44)
 *   map_size - Bitmap size, passed BY VALUE (`pea (0x20).w` at
 *              0x00E4BB12, read with `cmpi.l #0x20,(0x18,A6)` at 0x00E06B66)
 *   flags - Operation flags
 *   output - Output buffer for segment map data
 *   status - Status return
 *
 * Original address: 0x00e06b1e
 */

#include "ast/ast_internal.h"
#include "proc1/proc1.h"

void AST_$GET_SEG_MAP(uid_t *uid, uint32_t start_offset, uint32_t unused,
                      uint32_t seg_count, uint32_t map_size, uint16_t flags,
                      uint32_t *output, status_$t *status)
{
    uint32_t count = map_size;
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

    /* Calculate segment range.  0x00E06B44 keeps seg_count << 15 in a frame
     * local (A6-0x40); it is not stored through any caller pointer. */
    uint32_t seg_span = seg_count << 15;
    (void)seg_span;
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
        aote = ast_$lookup_aote_by_uid(uid);

        if (aote == NULL) {
            /* Try to load the AOTE */
            aote = ast_$force_activate_segment(uid, 0, status, 0);

            if (aote == NULL) {
                ML_$UNLOCK(AST_LOCK_ID);
                PROC1_$INHIBIT_END();
                return;
            }
        } else {
            aote->flags |= AOTE_FLAG_BUSY;
        }

        /* Find or create ASTE for this segment */
        aste = ast_$lookup_aste(aote, start_segment);
        if (aste == NULL) {
            /* Create new ASTE */
            aste = ast_$lookup_or_create_aste(aote, start_segment, status);
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
