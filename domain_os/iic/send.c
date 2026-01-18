/*
 * IIC_$SEND - Send data over IIC bus
 *
 * Sends data to an IIC device. In this system configuration, the IIC
 * hardware is not present, so this returns an error.
 *
 * Original address: 00e70a9e
 * Original size: 18 bytes
 *
 * Assembly:
 *   00e70a9e    link.w A6,0x0
 *   00e70aa2    movea.l (0x1c,A6),A0     ; status_ret is at offset 0x1c (6th param)
 *   00e70aa6    move.l #0x2c000a,(A0)
 *   00e70aac    unlk A6
 *   00e70aae    rts
 */

#include "iic/iic_internal.h"

void IIC_$SEND(uint32_t device_id, uint32_t address, void *data,
               uint32_t length, uint32_t flags, status_$t *status_ret)
{
    (void)device_id;  /* Unused - stub function */
    (void)address;    /* Unused - stub function */
    (void)data;       /* Unused - stub function */
    (void)length;     /* Unused - stub function */
    (void)flags;      /* Unused - stub function */

    *status_ret = status_$iic_device_not_in_system;
}
