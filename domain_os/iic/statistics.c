/*
 * IIC_$STATISTICS - Get IIC device statistics
 *
 * Retrieves statistics for an IIC device. In this system configuration,
 * the IIC hardware is not present, so this returns an error.
 *
 * Original address: 00e70a7a
 * Original size: 18 bytes
 *
 * Assembly:
 *   00e70a7a    link.w A6,0x0
 *   00e70a7e    movea.l (0x10,A6),A0
 *   00e70a82    move.l #0x2c000a,(A0)
 *   00e70a88    unlk A6
 *   00e70a8a    rts
 */

#include "iic/iic_internal.h"

void IIC_$STATISTICS(uint32_t device_id, void *stats_buf, status_$t *status_ret)
{
    (void)device_id;  /* Unused - stub function */
    (void)stats_buf;  /* Unused - stub function */

    *status_ret = status_$iic_device_not_in_system;
}
