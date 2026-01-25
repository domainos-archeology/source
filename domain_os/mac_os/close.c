/*
 * MAC_OS_$CLOSE - Close a MAC channel at OS level
 *
 * Closes a low-level MAC channel by:
 * 1. Calling the driver's close callback
 * 2. Removing packet type entries from the port's table
 * 3. Clearing the channel entry
 *
 * Original address: 0x00E0B45C
 * Original size: 196 bytes
 */

#include "mac_os/mac_os_internal.h"

/*
 * MAC_OS_$CLOSE
 *
 * Parameters:
 *   channel    - Pointer to channel number (0-9)
 *   status_ret - Pointer to receive status code
 *
 * Assembly notes:
 *   - Uses A5 = 0xE22990 (MAC_OS_$DATA base)
 *   - Channel lookup: channel * 4 * 4 + channel * 4 = channel * 20
 *   - Removes packet type entries by copying last entry over removed entry
 */
void MAC_OS_$CLOSE(int16_t *channel, status_$t *status_ret)
{
    int16_t chan_num;
    mac_os_$channel_t *chan;
    mac_os_$port_pkt_table_t *port_table;
    int16_t entry_idx;

    *status_ret = status_$ok;

    chan_num = *channel;

    /* Enter exclusion region */
    ML_$EXCLUSION_START((ml_$exclusion_t *)MAC_OS_$EXCLUSION);

    chan = &MAC_OS_$CHANNEL_TABLE[chan_num];

    /* Check if driver has close callback (offset 0x40) */
    if (chan->driver_info != NULL) {
        void *close_fn = *(void **)((uint8_t *)chan->driver_info + MAC_OS_DRIVER_CLOSE_OFFSET);
        if (close_fn == NULL) {
            *status_ret = status_$mac_port_op_not_implemented;
        } else {
            /* Call driver close callback */
            void (*driver_close)(uint16_t, void *, status_$t *);
            driver_close = close_fn;
            (*driver_close)(chan->line_number, &chan->callback_data, status_ret);
        }
    }

    /* Get port's packet type table */
    port_table = &MAC_OS_$PORT_PKT_TABLES[chan->port_index];

    /* Remove all packet type entries that belong to this channel */
    entry_idx = 0;
    while (entry_idx < port_table->entry_count) {
        mac_os_$pkt_type_entry_t *entry = &port_table->entries[entry_idx];

        if (entry->channel_index == chan_num) {
            /* Remove this entry by copying last entry over it */
            mac_os_$pkt_type_entry_t *last_entry;
            last_entry = &port_table->entries[port_table->entry_count - 1];

            entry->range_low = last_entry->range_low;
            entry->range_high = last_entry->range_high;
            entry->reserved = last_entry->reserved;
            entry->channel_index = last_entry->channel_index;

            port_table->entry_count--;
            /* Don't increment entry_idx - check swapped entry */
        } else {
            entry_idx++;
        }
    }

    /* Clear channel entry flags and pointers */
    chan->flags &= ~MAC_OS_FLAG_OPEN;
    chan->driver_info = NULL;
    chan->callback = NULL;

    ML_$EXCLUSION_STOP((ml_$exclusion_t *)MAC_OS_$EXCLUSION);
}
