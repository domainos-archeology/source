/*
 * MAC_$CLOSE - Close a MAC channel
 *
 * Closes a previously opened MAC channel and releases resources.
 *
 * Original address: 0x00E0BA6C
 * Original size: 166 bytes
 */

#include "mac/mac_internal.h"

void MAC_$CLOSE(uint16_t *channel, status_$t *status_ret)
{
    uint16_t chan;
    uint32_t chan_offset;
    uint16_t flags;
    uint16_t socket_num;
    uint8_t owner_asid;
    status_$t status;

    *status_ret = status_$ok;

    /* Enter exclusion region for channel table access */
#if defined(ARCH_M68K)
    ML_$EXCLUSION_START(&mac_$exclusion_lock);

    chan = *channel;

    /*
     * Validate channel number and ownership.
     * Channel must be < 10, flag bit 9 (0x200) must be set (channel open),
     * and owner ASID (bits 2-7 of flags >> 2) must match current ASID.
     */
    if (chan >= MAC_MAX_CHANNELS) {
        ML_$EXCLUSION_STOP(&mac_$exclusion_lock);
        *status_ret = status_$mac_channel_not_open;
        return;
    }

    /*
     * Calculate channel table offset.
     * Original: D0 = chan << 2, D1 = D0 << 2, D0 = D0 + D1 = chan * 20
     */
    chan_offset = (uint32_t)chan * 20;

    /* Read flags from channel entry at offset 0x7B2 */
    flags = *(uint16_t *)(MAC_$DATA_BASE + 0x7B2 + chan_offset);

    /* Check if channel is open (bit 9 / 0x200) */
    if ((flags & 0x200) == 0) {
        ML_$EXCLUSION_STOP(&mac_$exclusion_lock);
        *status_ret = status_$mac_channel_not_open;
        return;
    }

    /* Check ownership: bits 2-7 >> 2 gives owner ASID */
    owner_asid = (flags & 0xFC) >> 2;
    if (owner_asid != PROC1_$AS_ID) {
        ML_$EXCLUSION_STOP(&mac_$exclusion_lock);
        *status_ret = status_$mac_channel_not_open;
        return;
    }

    /*
     * Close the socket if allocated (not 0xE1).
     * Socket number is at offset 0x7A8 in channel entry.
     */
    socket_num = *(uint16_t *)(MAC_$DATA_BASE + 0x7A8 + chan_offset);
    if (socket_num != MAC_NO_SOCKET) {
        SOCK_$CLOSE(socket_num);
    }

    /* Mark socket as unallocated */
    *(uint16_t *)(MAC_$DATA_BASE + 0x7A8 + chan_offset) = MAC_NO_SOCKET;

    ML_$EXCLUSION_STOP(&mac_$exclusion_lock);
#else
    ML_$EXCLUSION_START(&mac_$exclusion_lock);
    /* TODO: Non-M68K channel table access */
    chan = *channel;
    ML_$EXCLUSION_STOP(&mac_$exclusion_lock);
#endif

    /* Call MAC_OS_$CLOSE to release OS-level resources */
    MAC_OS_$CLOSE(channel, &status);
    *status_ret = status;
}
