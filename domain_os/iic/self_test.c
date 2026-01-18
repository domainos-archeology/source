/*
 * IIC_$SELF_TEST - Perform self-test on IIC device
 *
 * Performs a self-test on an IIC device. In this system configuration,
 * the IIC hardware is not present, so this returns an error.
 *
 * Original address: 00e70a8c
 * Original size: 18 bytes
 *
 * Assembly:
 *   00e70a8c    link.w A6,0x0
 *   00e70a90    movea.l (0x10,A6),A0
 *   00e70a94    move.l #0x2c000a,(A0)
 *   00e70a9a    unlk A6
 *   00e70a9c    rts
 */

#include "iic/iic_internal.h"

void IIC_$SELF_TEST(uint32_t device_id, uint32_t test_params, status_$t *status_ret)
{
    (void)device_id;    /* Unused - stub function */
    (void)test_params;  /* Unused - stub function */

    *status_ret = status_$iic_device_not_in_system;
}
