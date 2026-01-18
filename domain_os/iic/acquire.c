/*
 * IIC_$ACQUIRE - Acquire an IIC device
 *
 * Attempts to acquire exclusive access to an IIC device. In this system
 * configuration, the IIC hardware is not present, so this returns an error.
 *
 * Original address: 00e70a56
 * Original size: 18 bytes
 *
 * Assembly:
 *   00e70a56    link.w A6,0x0
 *   00e70a5a    movea.l (0xc,A6),A0
 *   00e70a5e    move.l #0x2c000a,(A0)
 *   00e70a64    unlk A6
 *   00e70a66    rts
 */

#include "iic/iic_internal.h"

void IIC_$ACQUIRE(uint32_t device_id, status_$t *status_ret)
{
    (void)device_id;  /* Unused - stub function */

    *status_ret = status_$iic_device_not_in_system;
}
