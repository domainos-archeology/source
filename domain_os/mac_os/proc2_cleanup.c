/*
 * MAC_OS_$PROC2_CLEANUP - Process cleanup for MAC_OS
 *
 * Called during process termination to clean up any MAC channels
 * owned by the terminating process.
 *
 * Original address: 0x00E0BFDE
 * Original size: 240 bytes
 */

#include "mac_os/mac_os_internal.h"

/*
 * MAC_OS_$PROC2_CLEANUP
 *
 * This function iterates through all channels and closes any that
 * belong to the specified address space (process). It:
 * - Clears the OPEN flag
 * - Closes the socket if allocated
 * - Calls driver close callback
 * - Removes packet type entries from the port's table
 *
 * Parameters:
 *   as_id - Address space ID of the terminating process
 *
 * Assembly notes:
 *   - Uses A5 = 0xE22990 (MAC_OS_$DATA base)
 *   - Loops through 10 channels checking owner AS_ID
 *   - AS_ID is stored in channel flags bits 2-7 (shifted left by 2)
 */
/*
 * The port driver's close entry, the same shape MAC_OS_$CLOSE calls
 * (0x00E0C04C-0x00E0C064: a word result slot, then status, &callback_data and
 * the line number).
 */
typedef int16_t (*mac_os_$cleanup_close_fn_t)(uint16_t line_number,
                                              uint16_t *callback_data,
                                              status_$t *status_ret);

void MAC_OS_$PROC2_CLEANUP(uint16_t as_id)
{
    int16_t channel;
    int16_t port;
    int16_t entry_idx;
    mac_os_$channel_t *chan;
    mac_os_$port_pkt_table_t *port_table;
    status_$t dummy_status;

    /* Enter exclusion region */
    ML_$EXCLUSION_START(&MAC_OS_$EXCLUSION);

    /* Check each channel */
    for (channel = 0; channel < MAC_OS_MAX_CHANNELS; channel++) {
        chan = &MAC_OS_$CHANNEL_TABLE[channel];

        /* 0x00E0C004: move.w (0x7b2,A2),D0w / btst.l #0x9,D0 */
        if ((chan->flags & MAC_OS_CHANNEL_IN_USE) == 0) {
            continue;
        }

        /*
         * 0x00E0C010: move.w #0xfc,D1w / and.b (0x7b2,A2),D1b / lsr.w #0x2,D1w
         * The and.b works on the flags word's HIGH byte, so the owner is word
         * bits 10..15.
         */
        if (((chan->flags & MAC_OS_CHANNEL_OWNER_MASK) >> MAC_OS_CHANNEL_OWNER_SHIFT)
            != as_id) {
            continue;
        }

        /* 0x00E0C020: bclr.b #0x1,(0x7b2,A2) - word bit 9, IN_USE */
        chan->flags &= (uint16_t)~MAC_OS_CHANNEL_IN_USE;

        /* Close the socket if allocated */
        if (chan->socket != MAC_OS_NO_SOCKET) {
            SOCK_$CLOSE(chan->socket);
        }
        chan->socket = MAC_OS_NO_SOCKET;

        /*
         * 0x00E0C042-0x00E0C064.  The driver record pointer is dereferenced
         * unconditionally ("movea.l (0x7a4,A2),A0 / tst.l (0x40,A0)"); only
         * the close entry itself is tested, and unlike MAC_OS_$CLOSE a missing
         * entry is silently skipped rather than reported (bead source-ijlf).
         */
        {
            mac_os_$cleanup_close_fn_t driver_close = (mac_os_$cleanup_close_fn_t)
                *(void **)((uint8_t *)chan->driver_info + MAC_OS_DRIVER_CLOSE_OFFSET);

            if (driver_close != NULL) {
                (void)(*driver_close)(chan->line_number, &chan->callback_data,
                                      &dummy_status);
            }
        }

        /* Remove packet type entries for this channel from port's table */
        port = chan->port_index;
        port_table = &MAC_OS_$PORT_PKT_TABLES[port];

        entry_idx = 0;
        while (entry_idx < port_table->entry_count) {
            mac_os_$pkt_type_entry_t *entry = &port_table->entries[entry_idx];

            if (entry->channel_index == channel) {
                /*
                 * 0x00E0C08A: lea (0x4,A0,D1w*0x1),A3 with A0 = &table and
                 * D1 = 12 * entry_count, i.e. &entries[entry_count] - ONE PAST
                 * the last live entry.  MAC_OS_$CLOSE's identical loop uses
                 * "lea (-0x8,A0,D1w*0x1)" = &entries[entry_count - 1]
                 * (0x00E0B4E4).  The one-past index is what this image does;
                 * it is reproduced here rather than corrected.
                 */
                mac_os_$pkt_type_entry_t *last_entry =
                    (mac_os_$pkt_type_entry_t *)
                    ((uint8_t *)port_table +
                     offsetof(mac_os_$port_pkt_table_t, entries) +
                     (int32_t)port_table->entry_count * MAC_OS_PKT_TYPE_ENTRY_SIZE);

                entry->range_low = last_entry->range_low;
                entry->range_high = last_entry->range_high;
                entry->channel_index = last_entry->channel_index;
                entry->reserved = last_entry->reserved;

                port_table->entry_count--;
                /* Don't increment - check swapped entry */
            } else {
                entry_idx++;
            }
        }

        /* Clear channel entry */
        chan->driver_info = NULL;
        chan->callback = NULL;
    }

    ML_$EXCLUSION_STOP(&MAC_OS_$EXCLUSION);
}
