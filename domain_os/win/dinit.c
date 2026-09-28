/*
 * win/dinit.c - WIN_$DINIT (0x00E19CE8, 108 bytes)
 *
 * Jump-table entry +0x08, called by DISK_$MNT_DINIT as
 * dinit(unit, controller, vol_idx_ptr, num_blocks_ptr, sec_per_track_ptr,
 * num_heads_ptr, pvlabel_info).  It takes the ML lock kept at +0x08 of the
 * CONTROLLER's unit record (`move.w (0xa,A6),D2w` is argument 2), calls
 * DISK_INIT with the two words SWAPPED (controller first, unit second:
 * 0x00E19D2C pushes (0x8,A6) then 0x00E19D30 pushes D2) and the five
 * pointers passed through, then unlocks.
 *
 * Frame (link.w A6,-0x8; A5 A2 D2 saved), A5 = 0xE2B89C:
 *   A2          the controller's 12-byte unit record
 *   D2          controller, then DISK_INIT's result across the unlock
 */

#include "win/win_internal.h"

uint32_t WIN_$DINIT(uint16_t unit, uint16_t controller, void *num_blocks,
                    void *sec_per_track, void *num_heads, void *pvlabel_info,
                    void *param_7)
{
    int16_t lock_id;                    /* (0x8,A2) */
    uint32_t result;                    /* D2 */

    /* 0x00E19CF6-0x00E19D16: the lock word of record `controller`; the
     * word result slot ML_$LOCK gets is discarded. */
    lock_id = WIN_UNIT_LOCK(controller);
    ML_$LOCK(lock_id);

    /* 0x00E19D18-0x00E19D3A */
    result = DISK_INIT(controller, unit, (int32_t *)num_blocks,
                       (uint16_t *)sec_per_track, (uint16_t *)num_heads,
                       (uint16_t *)pvlabel_info, (uint16_t *)param_7);

    /* 0x00E19D3C-0x00E19D48: the same lock word, re-read from the record. */
    ML_$UNLOCK(WIN_UNIT_LOCK(controller));

    return result;
}
