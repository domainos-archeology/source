/*
 * smd/clr_trk_rect.c - SMD_$CLR_TRK_RECT implementation
 *
 * Clears all tracking rectangles from the tracking list.
 *
 * Original address: 0x00E6E718
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
 * SMD_$CLR_TRK_RECT - Clear all tracking rectangles
 *
 * Clears all tracking rectangles from the tracking list. This is
 * typically called before loading a new set of tracking rectangles
 * or when tracking is no longer needed.
 *
 * Parameters:
 *   status_ret - Pointer to status return
 *
 * Original address: 0x00E6E718
 *
 * Assembly:
 *   00e6e718    link.w A6,0x0
 *   00e6e71c    pea (A5)
 *   00e6e71e    lea (0xe82b8c).l,A5
 *   00e6e724    pea (0xe2e520).l              ; exclusion lock
 *   00e6e72a    jsr 0x00e20df8.l              ; ML_$EXCLUSION_START
 *   00e6e730    addq.w #0x4,SP
 *   00e6e732    clr.w (0xe6,A5)               ; tracking_rect_count = 0
 *   00e6e736    pea (0xe2e520).l              ; exclusion lock
 *   00e6e73c    jsr 0x00e20e7e.l              ; ML_$EXCLUSION_STOP
 *   00e6e742    addq.w #0x4,SP
 *   00e6e744    st (0x1744,A5)                ; cursor_pending_flag = 0xFF
 *   00e6e748    pea (-0x2f2,PC)               ; lock_data_2
 *   00e6e74c    pea (-0x1b4,PC)               ; lock_data_1
 *   00e6e750    pea (0x1d94,A5)               ; &default_cursor_pos
 *   00e6e754    bsr.w 0x00e6e1cc              ; SHOW_CURSOR
 *   00e6e758    movea.l (0x8,A6),A0           ; A0 = status_ret
 *   00e6e75c    clr.l (A0)                    ; *status_ret = 0
 *   00e6e75e    movea.l (-0x4,A6),A5
 *   00e6e762    unlk A6
 *   00e6e764    rts
 */
void SMD_$CLR_TRK_RECT(status_$t *status_ret)
{
    /* Begin exclusion - protect tracking rect data */
    ML_$EXCLUSION_START(&ml_$exclusion_t_00e2e520);

    /* Clear all tracking rectangles */
    SMD_GLOBALS.tracking_rect_count = 0;

    /* End exclusion */
    ML_$EXCLUSION_STOP(&ml_$exclusion_t_00e2e520);

    /* Set cursor pending flag to force cursor update */
    SMD_GLOBALS.cursor_pending_flag = (uint8_t)0xFF;

    /* Update cursor display */
    SHOW_CURSOR((uint32_t *)&SMD_GLOBALS.default_cursor_pos,
                (int16_t *)&cursor_lock_data_1,
                (int8_t *)&cursor_lock_data_2);

    *status_ret = status_$ok;
}
