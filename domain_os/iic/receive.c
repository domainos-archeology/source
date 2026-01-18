/*
 * IIC_$RECEIVE - Receive data from IIC bus
 *
 * Receives data from an IIC device. In this system configuration, the IIC
 * hardware is not present, so this returns an error and sets byte counts to 0.
 *
 * Original address: 00e70aba
 * Original size: 36 bytes
 *
 * Assembly:
 *   00e70aba    link.w A6,0x0
 *   00e70abe    pea (A2)                 ; save A2
 *   00e70ac0    movea.l (0x1c,A6),A0     ; status_ret (6th param, offset 0x1c)
 *   00e70ac4    move.l #0x2c000a,(A0)    ; *status_ret = error
 *   00e70aca    movea.l (0x18,A6),A1     ; bytes_received (5th param, offset 0x18)
 *   00e70ace    clr.w (A1)               ; *bytes_received = 0
 *   00e70ad0    movea.l (0x10,A6),A2     ; bytes_requested (3rd param, offset 0x10)
 *   00e70ad4    clr.w (A2)               ; *bytes_requested = 0
 *   00e70ad6    movea.l (-0x4,A6),A2     ; restore A2
 *   00e70ada    unlk A6
 *   00e70adc    rts
 */

#include "iic/iic_internal.h"

void IIC_$RECEIVE(uint32_t device_id, uint32_t address, uint16_t *bytes_requested,
                  void *buffer, uint16_t *bytes_received, status_$t *status_ret)
{
    (void)device_id;  /* Unused - stub function */
    (void)address;    /* Unused - stub function */
    (void)buffer;     /* Unused - stub function */

    *status_ret = status_$iic_device_not_in_system;
    *bytes_received = 0;
    *bytes_requested = 0;
}
