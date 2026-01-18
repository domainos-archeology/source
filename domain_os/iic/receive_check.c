/*
 * IIC_$RECEIVE_CHECK - Check if data available on IIC bus
 *
 * Checks whether there is data available to receive from an IIC device.
 * In this system configuration, the IIC hardware is not present, so this
 * returns false and sets the error status.
 *
 * Original address: 00e70ade
 * Original size: 20 bytes
 *
 * Assembly:
 *   00e70ade    link.w A6,-0x4
 *   00e70ae2    movea.l (0xc,A6),A0      ; status_ret (2nd param)
 *   00e70ae6    move.l #0x2c000a,(A0)    ; *status_ret = error
 *   00e70aec    clr.b D0b                ; return false (0)
 *   00e70aee    unlk A6
 *   00e70af0    rts
 */

#include "iic/iic_internal.h"

boolean IIC_$RECEIVE_CHECK(uint32_t device_id, status_$t *status_ret)
{
    (void)device_id;  /* Unused - stub function */

    *status_ret = status_$iic_device_not_in_system;
    return false;  /* No data available - IIC hardware not present */
}
