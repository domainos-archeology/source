/*
 * smd/blt_u.c - User-mode BLT (Block Transfer) operations
 *
 * SMD_$BLT_U validates user-supplied BLT parameters and calls the
 * internal SMD_$BLT function to perform the block transfer.
 *
 * Original address: 0x00E6FAE2
 */

#include "smd/smd_internal.h"

/*
 * smd_$is_valid_blt_ctl - Validate BLT control register value
 *
 * Checks if a BLT control register value is one of the valid magic values.
 * The valid values appear to encode source/destination plane configurations.
 *
 * Original address: 0x00E6FAA8
 *
 * Assembly:
 *   00e6faa8    link.w A6,-0x8
 *   00e6faac    move.l D2,-(SP)
 *   00e6faae    move.l (0x8,A6),D0
 *   00e6fab2    cmpi.l #0x2020020,D0
 *   00e6fab8    seq D1b
 *   00e6faba    cmpi.l #0x2020060,D0
 *   00e6fac0    seq D2b
 *   00e6fac2    or.b D2b,D1b
 *   00e6fac4    cmpi.l #0x6060020,D0
 *   00e6faca    seq D2b
 *   00e6facc    or.b D2b,D1b
 *   00e6face    cmpi.l #0x6060060,D0
 *   00e6fad4    seq D2b
 *   00e6fad6    or.b D2b,D1b
 *   00e6fad8    move.l (-0xc,A6),D2
 *   00e6fadc    move.b D1b,D0b
 *   00e6fade    unlk A6
 *   00e6fae0    rts
 */
uint8_t smd_$is_valid_blt_ctl(uint32_t ctl_reg)
{
    /*
     * SEQ sets the byte to 0xFF if the condition (equal) is true, 0 otherwise.
     * ORing all results together gives 0xFF if any match, 0 if none match.
     * The high bit being set (negative when treated as signed) indicates valid.
     */
    return (ctl_reg == SMD_BLT_CTL_VALID_1) ? 0xFF :
           (ctl_reg == SMD_BLT_CTL_VALID_2) ? 0xFF :
           (ctl_reg == SMD_BLT_CTL_VALID_3) ? 0xFF :
           (ctl_reg == SMD_BLT_CTL_VALID_4) ? 0xFF : 0;
}

/*
 * SMD_$BLT_U - User-mode bit block transfer
 *
 * Validates user-supplied BLT parameters and performs the block transfer.
 * This is the user-callable interface that ensures parameters are safe
 * before invoking the internal BLT operation.
 *
 * Original address: 0x00E6FAE2
 *
 * Assembly:
 *   00e6fae2    link.w A6,-0x4
 *   00e6fae6    movem.l {  A5 A3 A2 D2},-(SP)
 *   00e6faea    lea (0xe82b8c).l,A5        ; SMD globals (unused here)
 *   00e6faf0    movea.l (0x8,A6),A2        ; blt_ctl
 *   00e6faf4    movea.l (0xc,A6),A3        ; status_ret
 *   00e6faf8    clr.l (A3)                 ; *status_ret = 0
 *   00e6fafa    move.w (A2),(-0x2,A6)      ; local copy of mode
 *   00e6fafe    btst.b #0x6,(-0x1,A6)      ; test bit 6 of mode
 *   00e6fb04    bne.b 0x00e6fb0c          ; if set, invalid
 *   00e6fb06    tst.b (-0x1,A6)            ; test sign bit (bit 7)
 *   00e6fb0a    bpl.b 0x00e6fb12          ; if clear, continue
 *   00e6fb0c    move.l #0x13001a,(A3)      ; invalid mode register
 *   00e6fb12    tst.l (A3)
 *   00e6fb14    bne.b 0x00e6fb8c          ; exit if error
 *   00e6fb16    move.l (0x2,A2),-(SP)      ; push ctl_reg_1
 *   00e6fb1a    bsr.b 0x00e6faa8          ; validate ctl_reg_1
 *   00e6fb1c    addq.w #0x4,SP
 *   00e6fb1e    move.b D0b,(-0x4,A6)       ; save result
 *   00e6fb22    move.l (0x6,A2),-(SP)      ; push ctl_reg_2
 *   00e6fb26    bsr.b 0x00e6faa8          ; validate ctl_reg_2
 *   00e6fb28    addq.w #0x4,SP
 *   00e6fb2a    and.b (-0x4,A6),D0b        ; AND results
 *   00e6fb2e    move.b D0b,D2b
 *   00e6fb30    bmi.b 0x00e6fb3a          ; if negative (valid), continue
 *   00e6fb32    move.l #0x13001b,(A3)      ; invalid control register
 *   00e6fb38    bra.b 0x00e6fb7a
 *   00e6fb3a    cmpi.w #0x3ff,(0x16,A2)    ; check dst_width <= 1023
 *   00e6fb40    bhi.b 0x00e6fb72          ; if > 1023, invalid
 *   00e6fb42    tst.w (0xe,A2)             ; check dst_y >= 0
 *   00e6fb46    bmi.b 0x00e6fb72
 *   00e6fb48    cmpi.w #0x3ff,(0x12,A2)    ; check src_width <= 1023
 *   00e6fb4e    bhi.b 0x00e6fb72
 *   00e6fb50    tst.w (0xa,A2)             ; check src_y >= 0
 *   00e6fb54    bmi.b 0x00e6fb72
 *   00e6fb56    cmpi.w #0x3ff,(0x18,A2)    ; check dst_height <= 1023
 *   00e6fb5c    bhi.b 0x00e6fb72
 *   00e6fb5e    tst.w (0x10,A2)            ; check dst_x >= 0
 *   00e6fb62    bmi.b 0x00e6fb72
 *   00e6fb64    tst.w (0xc,A2)             ; check src_x >= 0
 *   00e6fb68    bmi.b 0x00e6fb72
 *   00e6fb6a    cmpi.w #0x3ff,(0x14,A2)    ; check src_height <= 1023
 *   00e6fb70    bls.b 0x00e6fb78          ; if <= 1023, valid
 *   00e6fb72    move.l #0x13001e,(A3)      ; invalid coordinates
 *   00e6fb78    tst.l (A3)
 *   00e6fb7a    bne.b 0x00e6fb8c          ; exit if error
 *   00e6fb7c    pea (A3)                   ; push status_ret
 *   00e6fb7e    pea (-0x2254,PC)           ; push placeholder (null)
 *   00e6fb82    pea (-0x20c,PC)            ; push placeholder (null)
 *   00e6fb86    pea (A2)                   ; push blt_ctl
 *   00e6fb88    bsr.w 0x00e6ec6e          ; call SMD_$BLT
 *   00e6fb8c    movem.l (-0x14,A6),{  D2 A2 A3 A5}
 *   00e6fb92    unlk A6
 *   00e6fb94    rts
 */
void SMD_$BLT_U(smd_blt_ctl_t *blt_ctl, status_$t *status_ret)
{
    uint8_t ctl1_valid;
    uint8_t ctl2_valid;

    /* Initialize status to success */
    *status_ret = status_$ok;

    /*
     * Validate mode register:
     * - Bit 7 (sign bit) must be clear
     * - Bit 6 must be clear
     */
    if ((blt_ctl->mode & SMD_BLT_MODE_RESERVED_MASK) != 0) {
        *status_ret = status_$display_invalid_blt_mode_register;
    }

    if (*status_ret == status_$ok) {
        /* Validate both control registers */
        ctl1_valid = smd_$is_valid_blt_ctl(blt_ctl->ctl_reg_1);
        ctl2_valid = smd_$is_valid_blt_ctl(blt_ctl->ctl_reg_2);

        /*
         * Both must be valid (have high bit set).
         * ANDing them: if both are 0xFF, result is 0xFF (negative).
         * If either is 0, result is 0 (non-negative).
         */
        if ((int8_t)(ctl1_valid & ctl2_valid) >= 0) {
            *status_ret = status_$display_invalid_blt_control_register;
        } else {
            /*
             * Validate screen coordinates:
             * - Source coordinates must be non-negative
             * - Width/height values must not exceed 0x3FF (1023 pixels)
             */
            if (blt_ctl->dst_width > SMD_BLT_MAX_COORD ||
                blt_ctl->dst_y < 0 ||
                blt_ctl->src_width > SMD_BLT_MAX_COORD ||
                blt_ctl->src_y < 0 ||
                blt_ctl->dst_height > SMD_BLT_MAX_COORD ||
                blt_ctl->dst_x < 0 ||
                blt_ctl->src_x < 0 ||
                blt_ctl->src_height > SMD_BLT_MAX_COORD) {
                *status_ret = status_$display_invalid_screen_coordinates_in_blt;
            }
        }

        if (*status_ret == status_$ok) {
            /*
             * All validation passed - call internal BLT function.
             * The second and third parameters are placeholder/null values
             * in the original code (PC-relative addresses pointing to zeros).
             */
            SMD_$BLT(blt_ctl, NULL, NULL, status_ret);
        }
    }
}
