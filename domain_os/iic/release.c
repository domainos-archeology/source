/*
 * IIC_$RELEASE - Release the internet interface controller
 *
 * Stub in this SAU2 image: stores status_$iic_device_not_in_system through
 * the status argument and returns.  The first argument is never read.
 *
 * Original address: 0x00E70A68, size 18 bytes (SAU2 map: IIC module)
 *
 *   00e70a68    link.w A6,0x0
 *   00e70a6c    movea.l (0xc,A6),A0       ; arg 2 = status_ret
 *   00e70a70    move.l #0x2c000a,(A0)     ; *status_ret = device not in system
 *   00e70a76    unlk A6
 *   00e70a78    rts
 */

#include "iic/iic_internal.h"

void IIC_$RELEASE(uint32_t arg1, status_$t *status_ret)
{
    (void)arg1;     /* (0x8,A6) is never read by the image */

    /* 0x00E70A6C-0x00E70A70 */
    *status_ret = status_$iic_device_not_in_system;
}
