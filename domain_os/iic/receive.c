/*
 * IIC_$RECEIVE - Receive from the internet interface controller
 *
 * Stub in this SAU2 image: stores status_$iic_device_not_in_system through
 * the status argument and clears the two 16-bit out parameters (arguments
 * 5 and 3, in that order).  Arguments 1, 2 and 4 are never read.
 *
 * Original address: 0x00E70ABA, size 36 bytes (SAU2 map: IIC module)
 *
 *   00e70aba    link.w A6,0x0
 *   00e70abe    pea (A2)                  ; save A2
 *   00e70ac0    movea.l (0x1c,A6),A0      ; arg 6 = status_ret
 *   00e70ac4    move.l #0x2c000a,(A0)     ; *status_ret = device not in system
 *   00e70aca    movea.l (0x18,A6),A1      ; arg 5 = count_ret5
 *   00e70ace    clr.w (A1)                ; *count_ret5 = 0
 *   00e70ad0    movea.l (0x10,A6),A2      ; arg 3 = count_ret3
 *   00e70ad4    clr.w (A2)                ; *count_ret3 = 0
 *   00e70ad6    movea.l (-0x4,A6),A2      ; restore A2
 *   00e70ada    unlk A6
 *   00e70adc    rts
 */

#include "iic/iic_internal.h"

void IIC_$RECEIVE(uint32_t arg1, uint32_t arg2, uint16_t *count_ret3,
                  uint32_t arg4, uint16_t *count_ret5, status_$t *status_ret)
{
    (void)arg1;     /* (0x8,A6) never read */
    (void)arg2;     /* (0xC,A6) never read */
    (void)arg4;     /* (0x14,A6) never read */

    /* 0x00E70AC0-0x00E70AC4 */
    *status_ret = status_$iic_device_not_in_system;
    /* 0x00E70ACA-0x00E70ACE */
    *count_ret5 = 0;
    /* 0x00E70AD0-0x00E70AD4 */
    *count_ret3 = 0;
}
