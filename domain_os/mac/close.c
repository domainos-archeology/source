/*
 * MAC_$CLOSE - Close a MAC channel
 *
 * Closes a previously opened MAC channel and releases resources.
 *
 * Original address: 0x00E0BA6C
 * Original size: 166 bytes
 */

#include "mac/mac_internal.h"
#include "mac_os/mac_os.h"

void MAC_$CLOSE(uint16_t *channel, status_$t *status_ret)
{
    uint16_t chan;
    uint16_t flags;
    uint16_t socket_num;
    uint8_t owner_asid;
    status_$t status;

    *status_ret = status_$ok;

    /* Enter exclusion region for channel table access (0x00E0BA84) */
    ML_$EXCLUSION_START(&MAC_OS_$EXCLUSION);

    chan = *channel;

    /*
     * Validate channel number and ownership.  0x00E0BA90:
     *   cmpi.w #0xa,(A2) ; bcc  -> not open
     * The comparison is unsigned, so any channel >= 10 fails.
     */
    if (chan >= MAC_MAX_CHANNELS) {
        ML_$EXCLUSION_STOP(&MAC_OS_$EXCLUSION);
        *status_ret = status_$mac_channel_not_open;
        return;
    }

    /*
     * 0x00E0BA96:
     *   move.w (A2),D0w ; lsl.w #0x2,D0w
     *   move.w D0w,D1w  ; lsl.w #0x2,D1w ; add.w D1w,D0w
     *   lea (0x0,A5,D0w*0x1),A4
     *   move.w (0x7b2,A4),D1w
     * chan*4 + chan*16 = chan*20, and A5+0x7B2 = MAC_OS_$CHANNEL_TABLE
     * (A5+0x7A0) entry .flags (offset 0x12).
     */
    flags = MAC_OS_$CHANNEL_TABLE[chan].flags;

    /* Channel in use?  0x00E0BAA8: btst.l #0x9,D1 */
    if ((flags & MAC_OS_CHANNEL_IN_USE) == 0) {
        ML_$EXCLUSION_STOP(&MAC_OS_$EXCLUSION);
        *status_ret = status_$mac_channel_not_open;
        return;
    }

    /*
     * Ownership check.  0x00E0BAAE:
     *   move.w #0xfc,D0w ; and.b (0x7b2,A4),D0b ; lsr.w #0x2,D0w
     *   cmp.w (0x00e2060a).l,D0w
     * The `and.b` operand at 0x7B2 is the *high* byte of the flags word, so
     * the AS id occupies word bits 10..15, not bits 2..7.
     */
    owner_asid = (uint8_t)((flags & MAC_OS_CHANNEL_OWNER_MASK) >> MAC_OS_CHANNEL_OWNER_SHIFT);
    if (owner_asid != PROC1_$AS_ID) {
        ML_$EXCLUSION_STOP(&MAC_OS_$EXCLUSION);
        *status_ret = status_$mac_channel_not_open;
        return;
    }

    /*
     * Close the socket if one is allocated.  0x00E0BAD2:
     *   cmpi.w #0xe1,(0x7a8,A4) ; beq -> skip
     *   move.w (0x7a8,A4),-(SP) ; jsr SOCK_$CLOSE
     * A5+0x7A8 is entry .socket (offset 0x08).
     */
    socket_num = MAC_OS_$CHANNEL_TABLE[chan].socket;
    if (socket_num != MAC_NO_SOCKET) {
        SOCK_$CLOSE(socket_num);
    }

    /* Mark the channel's socket slot free (0x00E0BAE8) */
    MAC_OS_$CHANNEL_TABLE[chan].socket = MAC_NO_SOCKET;

    ML_$EXCLUSION_STOP(&MAC_OS_$EXCLUSION);

    /* Call MAC_OS_$CLOSE to release OS-level resources */
    /* MAC_OS_$CLOSE takes int16_t *; MAC_$CLOSE receives the channel as
     * uint16_t *.  Same 16-bit word is passed by address in the original. */
    MAC_OS_$CLOSE((int16_t *)channel, &status);
    *status_ret = status;
}
