/*
 * IIC_$EXISTS - Is the internet interface controller present?
 *
 * Stub in this SAU2 image: always returns false.  The `link.w A6,-0x4`
 * reserves a local the stub never uses.
 *
 * Original address: 0x00E70AB0, size 10 bytes (SAU2 map: IIC module)
 *
 *   00e70ab0    link.w A6,-0x4
 *   00e70ab4    clr.b D0b                 ; result = false
 *   00e70ab6    unlk A6
 *   00e70ab8    rts
 */

#include "iic/iic_internal.h"

boolean IIC_$EXISTS(void)
{
    /* 0x00E70AB4 */
    return false;
}
