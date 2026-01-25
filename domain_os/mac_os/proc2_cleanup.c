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
void MAC_OS_$PROC2_CLEANUP(uint16_t as_id)
{
#if defined(M68K)
    int16_t channel;
    int16_t port;
    int16_t entry_idx;
    mac_os_$channel_t *chan;
    mac_os_$port_pkt_table_t *port_table;
    status_$t dummy_status;

    /* Enter exclusion region */
    ML_$EXCLUSION_START((ml_$exclusion_t *)MAC_OS_$EXCLUSION);

    /* Check each channel */
    for (channel = 0; channel < MAC_OS_MAX_CHANNELS; channel++) {
        chan = &MAC_OS_$CHANNEL_TABLE[channel];

        /* Check if channel is in use (bit 9 set) */
        if ((chan->flags & MAC_OS_FLAG_IN_USE) == 0) {
            continue;
        }

        /* Check if channel belongs to this AS_ID */
        /* AS_ID is in bits 2-7, so mask with 0xFC and shift right by 2 */
        if (((chan->flags & MAC_OS_FLAG_ASID_MASK) >> MAC_OS_FLAG_ASID_SHIFT) != as_id) {
            continue;
        }

        /* Clear the OPEN flag */
        chan->flags &= ~MAC_OS_FLAG_OPEN;

        /* Close the socket if allocated */
        if (chan->socket != MAC_OS_NO_SOCKET) {
            SOCK_$CLOSE(chan->socket);
        }
        chan->socket = MAC_OS_NO_SOCKET;

        /* Call driver close callback if available */
        if (chan->driver_info != NULL) {
            void *close_fn = *(void **)((uint8_t *)chan->driver_info + MAC_OS_DRIVER_CLOSE_OFFSET);
            if (close_fn != NULL) {
                void (*driver_close)(uint16_t, void *, status_$t *);
                driver_close = close_fn;
                (*driver_close)(chan->line_number, &chan->callback_data, &dummy_status);
            }
        }

        /* Remove packet type entries for this channel from port's table */
        port = chan->port_index;
        port_table = &MAC_OS_$PORT_PKT_TABLES[port];

        entry_idx = 0;
        while (entry_idx < port_table->entry_count) {
            mac_os_$pkt_type_entry_t *entry = &port_table->entries[entry_idx];

            if (entry->channel_index == channel) {
                /* Remove by copying last entry over this one */
                mac_os_$pkt_type_entry_t *last_entry;
                last_entry = &port_table->entries[port_table->entry_count - 1];

                entry->range_low = last_entry->range_low;
                entry->range_high = last_entry->range_high;
                entry->reserved = last_entry->reserved;
                entry->channel_index = last_entry->channel_index;

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

    ML_$EXCLUSION_STOP((ml_$exclusion_t *)MAC_OS_$EXCLUSION);
#else
    /* Non-M68K implementation stub */
    (void)as_id;
#endif
}
