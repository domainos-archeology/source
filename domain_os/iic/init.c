/*
 * IIC_$INIT - Initialize the IIC subsystem
 *
 * Performs initialization of the IIC bus controller. In this system
 * configuration, the IIC hardware is not present, so this is a no-op stub.
 *
 * Original address: 00e70a54
 * Original size: 2 bytes (just RTS)
 *
 * Assembly:
 *   00e70a54    rts
 */

#include "iic/iic_internal.h"

void IIC_$INIT(void)
{
    /* No-op stub - IIC hardware not present in this system */
    return;
}
