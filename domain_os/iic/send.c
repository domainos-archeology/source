/*
 * IIC_$SEND - Send through the internet interface controller
 *
 * Stub in this SAU2 image: stores status_$iic_device_not_in_system through
 * the status argument (argument 6) and returns.  Arguments 1-5 are never
 * read.
 *
 * Original address: 0x00E70A9E, size 18 bytes (SAU2 map: IIC module)
 *
 *   00e70a9e    link.w A6,0x0
 *   00e70aa2    movea.l (0x1c,A6),A0      ; arg 6 = status_ret
 *   00e70aa6    move.l #0x2c000a,(A0)     ; *status_ret = device not in system
 *   00e70aac    unlk A6
 *   00e70aae    rts
 */

#include "iic/iic_internal.h"

void IIC_$SEND(uint32_t arg1, uint32_t arg2, uint32_t arg3,
               uint32_t arg4, uint32_t arg5, status_$t *status_ret)
{
    (void)arg1;     /* (0x8,A6) never read */
    (void)arg2;     /* (0xC,A6) never read */
    (void)arg3;     /* (0x10,A6) never read */
    (void)arg4;     /* (0x14,A6) never read */
    (void)arg5;     /* (0x18,A6) never read */

    /* 0x00E70AA2-0x00E70AA6 */
    *status_ret = status_$iic_device_not_in_system;
}
