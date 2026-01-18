/*
 * IIC_$EXISTS - Check if IIC hardware exists
 *
 * Checks whether the IIC bus controller hardware is present in the system.
 * In this system configuration, the IIC hardware is not present.
 *
 * Original address: 00e70ab0
 * Original size: 10 bytes
 *
 * Assembly:
 *   00e70ab0    link.w A6,-0x4
 *   00e70ab4    clr.b D0b            ; return false (0)
 *   00e70ab6    unlk A6
 *   00e70ab8    rts
 */

#include "iic/iic_internal.h"

boolean IIC_$EXISTS(void)
{
    return false;  /* IIC hardware not present */
}
