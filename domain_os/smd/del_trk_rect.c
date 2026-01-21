/*
 * smd/del_trk_rect.c - SMD_$DEL_TRK_RECT implementation
 *
 * Deletes one or more tracking rectangles from the tracking list.
 *
 * Original address: 0x00E6E614
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
 * SMD_$DEL_TRK_RECT - Delete tracking rectangles
 *
 * Deletes one or more tracking rectangles from the tracking list.
 * Rectangles are matched by their coordinates (x1, y1, x2, y2).
 * When a rectangle is found and deleted, the last rectangle in the
 * list is moved to fill the gap (swap-and-pop deletion).
 *
 * Parameters:
 *   rects      - Pointer to array of rectangles to delete
 *   count      - Pointer to number of rectangles to delete
 *   status_ret - Pointer to status return
 *
 * Returns:
 *   status_$ok if all rectangles were found and deleted
 *   status_$display_bad_tracking_rectangle if some rectangles were not found
 *
 * Original address: 0x00E6E614
 *
 * Assembly overview:
 *   - Save the initial tracking_rect_count
 *   - Begin exclusion lock
 *   - For each rectangle to delete:
 *     - Search tracking_rects array from index 1 to tracking_rect_count
 *     - If found: copy last rect to this position, decrement count
 *     - Note: Uses swap-and-pop, so may find duplicates multiple times
 *   - End exclusion lock
 *   - Check if (initial_count - deleted_count) == tracking_rect_count + count_to_delete
 *   - If mismatch, some rectangles weren't found - return bad_tracking_rectangle
 *   - Set cursor_pending_flag and call SHOW_CURSOR
 */
void SMD_$DEL_TRK_RECT(smd_track_rect_t *rects, uint16_t *count, status_$t *status_ret)
{
    uint16_t initial_count;
    uint16_t delete_count;
    uint16_t i, j;
    int matched;
    smd_track_rect_t *search_ptr;
    smd_track_rect_t *dest_ptr;

    *status_ret = status_$ok;
    delete_count = *count;

    /* Begin exclusion - protect tracking rect data */
    ML_$EXCLUSION_START(&ml_$exclusion_t_00e2e520);

    /* Save initial count for verification */
    initial_count = SMD_GLOBALS.tracking_rect_count;

    /* For each rectangle to delete */
    for (i = 0; i < delete_count; i++) {
        /*
         * Search tracking_rects for a matching rectangle.
         * Original uses 1-based indexing; we use 0-based.
         *
         * The original algorithm:
         * - Searches from index 1 to tracking_rect_count
         * - When found, copies last rect to this position (swap-and-pop)
         * - Decrements tracking_rect_count
         * - Continues searching from same position (may find duplicates)
         */
        j = 0;
        while (j < SMD_GLOBALS.tracking_rect_count) {
            search_ptr = &SMD_GLOBALS.tracking_rects[j];
            dest_ptr = &SMD_GLOBALS.tracking_rects[j];

            /* Compare both 32-bit halves of the rectangle (x1,y1 and x2,y2) */
            matched = (*(uint32_t *)&rects[i].x1 == *(uint32_t *)&search_ptr->x1) &&
                      (*(uint32_t *)&rects[i].x2 == *(uint32_t *)&search_ptr->x2);

            if (matched) {
                /* Found - swap with last rect and decrement count */
                uint16_t last_idx = SMD_GLOBALS.tracking_rect_count - 1;
                if (j != last_idx) {
                    /* Copy last rect to this position */
                    *dest_ptr = SMD_GLOBALS.tracking_rects[last_idx];
                }
                SMD_GLOBALS.tracking_rect_count--;
                /* Don't increment j - need to check swapped-in rect */
            } else {
                j++;
            }
        }
    }

    /* End exclusion */
    ML_$EXCLUSION_STOP(&ml_$exclusion_t_00e2e520);

    /*
     * Verify all rectangles were found:
     * If we deleted delete_count rects from initial_count,
     * we should now have (initial_count - delete_count) rects.
     * If tracking_rect_count + delete_count != initial_count,
     * some rectangles were not found.
     */
    if ((uint32_t)SMD_GLOBALS.tracking_rect_count + delete_count != (uint32_t)initial_count) {
        *status_ret = status_$display_bad_tracking_rectangle;
    }

    /* Set cursor pending flag to force cursor update */
    SMD_GLOBALS.cursor_pending_flag = (uint8_t)0xFF;

    /* Update cursor display */
    SHOW_CURSOR((uint32_t *)&SMD_GLOBALS.default_cursor_pos,
                (int16_t *)&cursor_lock_data_1,
                (int8_t *)&cursor_lock_data_2);
}
