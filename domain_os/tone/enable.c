/*
 * TONE_$ENABLE - Switch the speaker tone on or off
 *
 * Hands SIO2681_$TONE the ADDRESS of a frame cell holding the address of
 * the SIO2681 channel-A record inside TERM_$DATA (TONE_$CHANNEL, +0x1268),
 * the caller's enable-byte pointer, and a status cell nobody reads.
 *
 * Parameters:
 *   enable - pointer to a byte whose bit 7 turns the tone on ((0x8,A6))
 *
 * Original address: 0x00e1ace8, 46 bytes
 *
 *   00e1acee  lea (0xe2c9f0).l,A5                 ; TERM_$DATA
 *   00e1acf4  pea (-0x4,A6)                       ; status cell (never read)
 *   00e1acf8  move.l (0x8,A6),-(SP)               ; enable
 *   00e1acfc  lea (0x1268,A5),A0 / move.l A0,(-0x8,A6)   ; cell = &TONE_$CHANNEL
 *   00e1ad04  pea (-0x8,A6)                       ; &cell
 *   00e1ad08  jsr SIO2681_$TONE                   ; args reclaimed by unlk
 *
 * SIO2681_$TONE (0x00E1D172) reads its first argument with
 * `movea.l (0x8,A6),A0 / move.l (A0),D2` - a cell holding the channel
 * address - and never touches (0x10,A6), the status cell.
 */

#include "tone/tone_internal.h"

void TONE_$ENABLE(uint8_t *enable)
{
    status_$t status;                   /* A6-0x4 */
    sio2681_channel_t *channel_cell;    /* A6-0x8 */

    /* 0x00E1ACFC..0x00E1AD00 */
    channel_cell = &TONE_$CHANNEL;

    /* 0x00E1ACF4..0x00E1AD08 */
    SIO2681_$TONE(&channel_cell, enable, &status);
}
