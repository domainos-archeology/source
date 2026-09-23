/*
 * IIC_$RECEIVE_CHECK - Poll the internet interface controller for input
 *
 * Stub in this SAU2 image: stores status_$iic_device_not_in_system through
 * the status argument and returns false.  The first argument is never read;
 * `link.w A6,-0x4` reserves a local the stub never uses.
 *
 * Original address: 0x00E70ADE, size 20 bytes (SAU2 map: IIC module)
 *
 *   00e70ade    link.w A6,-0x4
 *   00e70ae2    movea.l (0xc,A6),A0       ; arg 2 = status_ret
 *   00e70ae6    move.l #0x2c000a,(A0)     ; *status_ret = device not in system
 *   00e70aec    clr.b D0b                 ; result = false
 *   00e70aee    unlk A6
 *   00e70af0    rts
 */

#include "iic/iic_internal.h"

boolean IIC_$RECEIVE_CHECK(uint32_t arg1, status_$t *status_ret)
{
    (void)arg1;     /* (0x8,A6) is never read by the image */

    /* 0x00E70AE2-0x00E70AE6 */
    *status_ret = status_$iic_device_not_in_system;
    /* 0x00E70AEC */
    return false;
}
