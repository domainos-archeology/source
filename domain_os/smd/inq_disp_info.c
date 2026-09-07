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
 *   00e70160    move.w (A3),D0w                    ; D0 = *unit
 *   00e70162    movea.l #0xe27376,A0
 *   00e70168    move.w D0w,D1w
 *   00e7016a    lsl.w #0x5,D1w                     ; unit * 32
 *   00e7016c    move.w D1w,D2w
 *   00e7016e    add.w D2w,D2w                      ; unit * 64
 *   00e70170    add.w D2w,D1w                      ; unit * 96
 *   00e70172    move.w (-0x60,A0,D1w*0x1),(A2)     ; info[unit-1].display_type
 *   00e70176    move.w D0w,D1w
 *   00e70178    movea.l #0xe2e3fc,A1
 *   00e7017e    muls.w #0x10c,D1
 *   00e70182    lea (0x0,A1,D1*0x1),A0             ; biased unit record
 *   00e70186    movea.l (-0xf4,A0),A1              ; A1 = rec->hw
 *   00e7018a    move.w (0x50,A1),D1w               ; hw->max_x
 *   00e7018e    addq.w #0x1,D1w
 *   00e70190    move.w D1w,(0x6,A2)                ; result->width
 *   00e70194    move.w (0x54,A1),D1w               ; hw->max_y
 *   00e70198    addq.w #0x1,D1w
 *   00e7019a    move.w D1w,(0x8,A2)                ; result->height
 *   00e7019e    move.w (A2),D0w
 *   00e701a0    subq.w #0x1,D0w
 *   00e701a2    cmpi.w #0xb,D0w                    ; types 1..11 only
 *   00e701a6    bcc.b 0x00e701e4
 *   00e701a8    add.w D0w,D0w
 *   00e701aa    move.w (0xe701b2,PC,D0w*0x1),D0w   ; jump table at 0x00E701B2
 *   00e701ae    jmp (0xe701b2,PC,D0w*0x1)
 *   00e701c8    move.l #0x4000400,(0x2,A2)         ; 1024 x 1024
 *   00e701d0    bra.b 0x00e701e4
 *   00e701d2    move.l #0x4000800,(0x2,A2)         ; 1024 x 2048
 *   00e701da    bra.b 0x00e701e4
 *   00e701dc    move.l #0x8000400,(0x2,A2)         ; 2048 x 1024
 *   00e701e4    movem.l (-0x18,A6),{  D2 A2 A3 A4 A5}
 *   00e701ea    unlk A6
 *   00e701ec    rts
 *
 * The jump table at 0x00E701B2 (read with gsk, 11 words):
 *   0016 0016 0020 0020 002a 0016 0032 0016 002a 0016 0016
 * i.e. types 1,2,6,8,10,11 -> 0x00E701C8; types 3,4 -> 0x00E701D2;
 * types 5,9 -> 0x00E701DC; type 7 -> 0x00E701E4 (nothing at all).
 */

#include "smd/smd_internal.h"

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
    smd_display_hw_t *hw;
    uint16_t disp_type;

    /* 0x00e7013e-0x00e70146: the whole 10-byte result is cleared first. */
    info->display_type = 0;
    info->mem_width = 0;
    info->mem_height = 0;
    info->width = 0;
    info->height = 0;
    *status_ret = status_$ok;

    /* 0x00e7014c-0x00e70156 */
    if (smd_$validate_unit(*unit) >= 0) {
        *status_ret = status_$display_invalid_unit_number;
        return;
    }

    /* 0x00e70172: the info table is 1-based on the unit number. */
    disp_type = smd_$unit_info((int16_t)*unit)->display_type;
    info->display_type = disp_type;

    /* 0x00e70178-0x00e70186 */
    hw = smd_$unit_rec((int16_t)*unit)->hw;

    /* 0x00e7018a / 0x00e70194: max_x + 1 is the width, max_y + 1 the height */
    info->width = (uint16_t)(hw->max_x + 1);
    info->height = (uint16_t)(hw->max_y + 1);

    /*
     * 0x00e7019e-0x00e701dc: the frame-buffer dimensions, written as a single
     * longword covering +0x02 and +0x04.  Types outside 1..11 - and type 7 -
     * leave both at zero.
     */
    switch (disp_type) {
        case SMD_DISP_TYPE_MONO_PORTRAIT:       /* 1 */
        case SMD_DISP_TYPE_MONO_LANDSCAPE:      /* 2 */
        case SMD_DISP_TYPE_MONO_1024x1024_A:    /* 6 */
        case SMD_DISP_TYPE_MONO_1024x1024_B:    /* 8 */
        case SMD_DISP_TYPE_MONO_1024x1024_C:    /* 10 */
        case SMD_DISP_TYPE_MONO_1024x1024_D:    /* 11 */
            /* 0x00e701c8 move.l #0x04000400 */
            info->mem_width = 0x0400;
            info->mem_height = 0x0400;
            break;

        case SMD_DISP_TYPE_COLOR_1024x2048:     /* 3 */
        case SMD_DISP_TYPE_COLOR_1024x2048_B:   /* 4 */
            /* 0x00e701d2 move.l #0x04000800 */
            info->mem_width = 0x0400;
            info->mem_height = 0x0800;
            break;

        case SMD_DISP_TYPE_HI_RES_2048x1024:    /* 5 */
        case SMD_DISP_TYPE_HI_RES_2048x1024_B:  /* 9 */
            /* 0x00e701dc move.l #0x08000400 */
            info->mem_width = 0x0800;
            info->mem_height = 0x0400;
            break;

        default:
            /* Types 7 and anything outside 1..11: left as zero */
            break;
    }
}
