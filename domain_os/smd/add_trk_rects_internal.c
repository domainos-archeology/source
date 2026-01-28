/*
 * smd/add_trk_rects_internal.c - smd_$add_trk_rects_internal implementation
 *
 * Internal helper function for adding tracking rectangles.
 * Used by SMD_$ADD_TRK_RECT and SMD_$CLR_AND_LOAD_TRK_RECT.
 *
 * Original address: 0x00E6E4D4
 */

#include "smd/smd_internal.h"
#include "ml/ml.h"

/* Exclusion lock for tracking rectangle operations */
extern ml_$exclusion_t ml_$exclusion_t_00e2e520;

/*
 * Lock data addresses from original code:
 *   0x00E6E59A - Lock data 1
 *   0x00E6E458 - Lock data 2
 */
static const uint32_t cursor_lock_data_1 = 0x00E6E59A;
static const uint32_t cursor_lock_data_2 = 0x00E6E458;

/*
 * smd_$add_trk_rects_internal - Internal function to add tracking rectangles
 *
 * Common implementation for SMD_$CLR_AND_LOAD_TRK_RECT and SMD_$ADD_TRK_RECT.
 * If clear_flag is negative (0xFF), clears existing rectangles first.
 *
 * Parameters:
 *   clear_flag - If negative (0xFF), clear existing rects first
 *   rects      - Pointer to array of tracking rectangles
 *   count      - Number of rectangles to add
 *
 * Returns:
 *   Negative value (0xFF) if successful, 0 if list would overflow
 *
 * Original address: 0x00E6E4D4
 *
 * Assembly:
 *   00e6e4d4    link.w A6,-0x18
 *   00e6e4d8    movem.l {  A3 A2 D5 D4 D3 D2},-(SP)
 *   00e6e4dc    move.b (0x8,A6),D4b           ; D4 = clear_flag
 *   00e6e4e0    movea.l (0xa,A6),A2           ; A2 = rects
 *   00e6e4e4    move.w (0xe,A6),D2w           ; D2 = count
 *   ...
 *   ; Check if there's room:
 *   ; If clear_flag < 0: compare count to 200
 *   ; Else: compare (tracking_rect_count + count) to 200
 *   ...
 *   ; If clear_flag < 0, set tracking_rect_count = 0
 *   ; Copy rectangles to tracking_rects array starting at index (tracking_rect_count + 1)
 *   ; Update tracking_rect_count
 */
int8_t smd_$add_trk_rects_internal(int8_t clear_flag, smd_track_rect_t *rects, uint16_t count)
{
    int8_t success;
    uint16_t total_count;
    uint16_t i;
    uint16_t dest_index;

    /* Begin exclusion - protect tracking rect data */
    ML_$EXCLUSION_START(&ml_$exclusion_t_00e2e520);

    /* Check if there's room for the new rectangles */
    if (clear_flag < 0) {
        /* If clearing, just check count against max */
        success = (count <= SMD_MAX_TRACKING_RECTS) ? (int8_t)0xFF : 0;
    } else {
        /* If adding, check current + new against max */
        total_count = SMD_GLOBALS.tracking_rect_count + count;
        success = (total_count <= SMD_MAX_TRACKING_RECTS) ? (int8_t)0xFF : 0;
    }

    if (success < 0) {
        /* Clear existing rectangles if requested */
        if (clear_flag < 0) {
            SMD_GLOBALS.tracking_rect_count = 0;
        }

        /* Copy new rectangles to the array */
        if (count != 0) {
            /*
             * Original code uses 1-based indexing for the array:
             * Index starts at (tracking_rect_count + 1), which maps to
             * tracking_rects[tracking_rect_count] in 0-based C indexing.
             */
            dest_index = SMD_GLOBALS.tracking_rect_count;
            for (i = 0; i < count; i++) {
                SMD_GLOBALS.tracking_rects[dest_index + i] = rects[i];
            }
        }

        /* Update count */
        SMD_GLOBALS.tracking_rect_count += count;
    }

    /* End exclusion */
    ML_$EXCLUSION_STOP(&ml_$exclusion_t_00e2e520);

    /* Update cursor display */
    SHOW_CURSOR(&SMD_GLOBALS.cursor_pos_sentinel,
                (int16_t *)&cursor_lock_data_1,
                (int8_t *)&cursor_lock_data_2);

    return success;
}
