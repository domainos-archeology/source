/*
 * smd/inq_disp_info.c - SMD_$INQ_DISP_INFO implementation
 *
 * Returns detailed display information for a unit.
 *
 * Original address: 0x00E70124
 *
 * Assembly:
 *   00e70124    link.w A6,-0x4
 *   00e70128    movem.l {  A5 A4 A3 A2 D2},-(SP)
 *   00e7012c    lea (0xe82b8c).l,A5
 *   00e70132    movea.l (0x8,A6),A3               ; unit
 *   00e70136    movea.l (0xc,A6),A2               ; info
 *   00e7013a    movea.l (0x10,A6),A4              ; status_ret
 *   00e7013e    lea (A2),A0
 *   00e70140    clr.l (A0)+                        ; clear info[0-3]
 *   00e70142    clr.l (A0)+                        ; clear info[4-7]
 *   00e70144    clr.w (A0)+                        ; clear info[8-9]
 *   00e70146    clr.l (A4)                         ; *status_ret = 0
 *   00e70148    subq.l #0x2,SP
 *   00e7014a    move.w (A3),-(SP)
 *   00e7014c    bsr.w 0x00e6d700                   ; validate unit
 *   00e70150    addq.w #0x4,SP
 *   00e70152    tst.b D0b
 *   00e70154    bmi.b 0x00e70160                   ; if valid, continue
 *   00e70156    move.l #0x130001,(A4)              ; status = invalid_unit
 *   00e7015c    bra.w 0x00e701e4
 *   ; ... continues with display type and resolution lookup
 */

#include "smd/smd_internal.h"

/* Forward declaration */
static int8_t smd_validate_unit(uint16_t unit);

/*
 * SMD_$INQ_DISP_INFO - Inquire display information
 *
 * Returns detailed information about a display unit including
 * type, bit depth, and resolution.
 *
 * Parameters:
 *   unit - Pointer to display unit number
 *   info - Pointer to info structure to fill
 *   status_ret - Status return
 */
void SMD_$INQ_DISP_INFO(uint16_t *unit, smd_disp_info_result_t *info, status_$t *status_ret)
{
    smd_display_unit_t *disp_unit;
    smd_display_hw_t *hw;
    uint16_t disp_type;

    /* Initialize output structure to zero */
    info->display_type = 0;
    info->bits_per_pixel = 0;
    info->num_planes = 0;
    info->height = 0;
    info->width = 0;

    /* Assume success */
    *status_ret = status_$ok;

    /* Validate unit number */
    if (smd_validate_unit(*unit) >= 0) {
        *status_ret = status_$display_invalid_unit_number;
        return;
    }

    /* Get display type from info table */
    disp_type = SMD_DISPLAY_INFO[*unit].display_type;
    info->display_type = disp_type;

    /* Get hardware info for dimensions */
    disp_unit = smd_get_unit(*unit);
    hw = disp_unit->hw;
    info->height = hw->height + 1;
    info->width = hw->width + 1;

    /*
     * Set bits_per_pixel and num_planes based on display type.
     * Original uses a switch/jump table at 0x00e701b2.
     */
    switch (disp_type) {
        case SMD_DISP_TYPE_MONO_LANDSCAPE:      /* 1 */
        case SMD_DISP_TYPE_MONO_PORTRAIT:       /* 2 */
        case SMD_DISP_TYPE_MONO_1024x1024_A:    /* 6 */
        case SMD_DISP_TYPE_MONO_1024x1024_B:    /* 8 */
        case SMD_DISP_TYPE_MONO_1024x1024_C:    /* 10 */
        case SMD_DISP_TYPE_MONO_1024x1024_D:    /* 11 */
            /* Monochrome: 4 bits/pixel, 4 planes */
            info->bits_per_pixel = 4;
            info->num_planes = 4;
            break;

        case SMD_DISP_TYPE_COLOR_1024x2048:     /* 3 */
        case SMD_DISP_TYPE_COLOR_1024x2048_B:   /* 4 */
            /* Color: 4 bits/pixel, 8 planes */
            info->bits_per_pixel = 4;
            info->num_planes = 8;
            break;

        case SMD_DISP_TYPE_HI_RES_2048x1024:    /* 5 */
        case SMD_DISP_TYPE_HI_RES_2048x1024_B:  /* 9 */
            /* Hi-res: 8 bits/pixel, 4 planes */
            info->bits_per_pixel = 8;
            info->num_planes = 4;
            break;

        default:
            /* Unknown type - leave as zero */
            break;
    }
}

/*
 * smd_validate_unit - Validate display unit number
 *
 * Internal helper to check if a unit number is valid.
 *
 * Returns:
 *   Negative value if valid, non-negative if invalid
 */
static int8_t smd_validate_unit(uint16_t unit)
{
    if (unit < SMD_MAX_DISPLAY_UNITS) {
        if (SMD_DISPLAY_INFO[unit].display_type != 0) {
            return -1;  /* Valid */
        }
    }
    return 0;  /* Invalid */
}
