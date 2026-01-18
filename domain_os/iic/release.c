/*
 * IIC_$RELEASE - Release an IIC device
 *
 * Releases exclusive access to a previously acquired IIC device. In this
 * system configuration, the IIC hardware is not present, so this returns
 * an error.
 *
 * Original address: 00e70a68
 * Original size: 18 bytes
 *
 * Assembly:
 *   00e70a68    link.w A6,0x0
 *   00e70a6c    movea.l (0xc,A6),A0
 *   00e70a70    move.l #0x2c000a,(A0)
 *   00e70a76    unlk A6
 *   00e70a78    rts
 */

#include "iic/iic_internal.h"

void IIC_$RELEASE(uint32_t device_id, status_$t *status_ret)
{
    (void)device_id;  /* Unused - stub function */

    *status_ret = status_$iic_device_not_in_system;
}
