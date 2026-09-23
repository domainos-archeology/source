/*
 * IIC_$SELF_TEST - Self-test the internet interface controller
 *
 * Stub in this SAU2 image: stores status_$iic_device_not_in_system through
 * the status argument (argument 3) and returns.  Arguments 1 and 2 are
 * never read.
 *
 * Original address: 0x00E70A8C, size 18 bytes (SAU2 map: IIC module)
 *
 *   00e70a8c    link.w A6,0x0
 *   00e70a90    movea.l (0x10,A6),A0      ; arg 3 = status_ret
 *   00e70a94    move.l #0x2c000a,(A0)     ; *status_ret = device not in system
 *   00e70a9a    unlk A6
 *   00e70a9c    rts
 */

#include "iic/iic_internal.h"

void IIC_$SELF_TEST(uint32_t arg1, uint32_t arg2, status_$t *status_ret)
{
    (void)arg1;     /* (0x8,A6) never read */
    (void)arg2;     /* (0xC,A6) never read */

    /* 0x00E70A90-0x00E70A94 */
    *status_ret = status_$iic_device_not_in_system;
}
