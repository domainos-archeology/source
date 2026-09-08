/*
 * MAC_OS_$CLOSE - Close a MAC channel at OS level
 *
 * Calls the port driver's close entry, drops every packet-type registration
 * that named this channel, and releases the channel slot.
 *
 * Original address: 0x00E0B45C, size 196 bytes (0x00E0B45C-0x00E0B51F).
 * A5 = 0x00E22990 (MAC_OS_$DATA).
 */

#include "mac_os/mac_os_internal.h"

/*
 * mac_os_$driver_close_fn_t - the port driver entry at driver_info + 0x40.
 *
 * 0x00E0B4A2-0x00E0B4B8 pushes, right to left:
 *   subq.l #0x2,SP            word result slot; the result is discarded
 *   pea    (A3)               arg 3, status_ret
 *   pea    (0x7ac,A2)         arg 2, &chan->callback_data
 *   move.w (0x7ae,A2),-(SP)   arg 1, chan->line_number by value
 *   ... jsr (A4) / lea (0xc,SP),SP
 * MAC_OS_$PROC2_CLEANUP calls the same entry the same way (0x00E0C04C).
 */
typedef int16_t (*mac_os_$driver_close_fn_t)(uint16_t line_number,
                                             uint16_t *callback_data,
                                             status_$t *status_ret);

/*
 * MAC_OS_$CLOSE
 *
 * Parameters:
 *   channel    - pointer to the channel number (0..9)
 *   status_ret - status return
 */
void MAC_OS_$CLOSE(int16_t *channel, status_$t *status_ret)
{
    int16_t                     chan_num;
    mac_os_$channel_t          *chan;
    mac_os_$port_pkt_table_t   *port_table;
    int16_t                     entry_idx;
    mac_os_$driver_close_fn_t   driver_close;

    /* 0x00E0B472: clr.l (A3) */
    *status_ret = status_$ok;

    /* 0x00E0B474: ML_$EXCLUSION_START(A5 + 0x868) */
    ML_$EXCLUSION_START(&MAC_OS_$EXCLUSION);

    /* 0x00E0B480-0x00E0B48C: chan = A5 + 0x7A0 + 20 * channel */
    chan_num = *channel;
    chan = &MAC_OS_$CHANNEL_TABLE[chan_num];

    /*
     * 0x00E0B490-0x00E0B4BA.  The driver record pointer is dereferenced
     * unconditionally - "movea.l (0x7a4,A2),A1 / tst.l (0x40,A1)" - so the
     * only test here is on the close entry itself.  (The tree used to guard
     * the whole block with a driver_info != NULL test the image does not
     * make; bead source-10zs.)
     */
    driver_close = (mac_os_$driver_close_fn_t)
        *(void **)((uint8_t *)chan->driver_info + MAC_OS_DRIVER_CLOSE_OFFSET);
    if (driver_close == NULL) {
        *status_ret = status_$mac_port_op_not_implemented;
    } else {
        (void)(*driver_close)(chan->line_number, &chan->callback_data, status_ret);
    }

    /*
     * 0x00E0B4BC-0x00E0B4FE: walk this port's packet-type table and remove
     * every entry that names this channel, moving the LAST entry down over
     * the hole and re-testing the same slot.
     *
     *   lea (-0x8,A0,D1w*0x1),A4    ; A0 = &table, D1 = 12 * entry_count
     *                               ; = &entries[entry_count - 1]
     *   lea (0x4,A1),A3             ; A1 = &table + 12 * entry_idx
     *   move.l (A4)+,(A3)+  x3      ; a whole 12-byte entry
     */
    port_table = &MAC_OS_$PORT_PKT_TABLES[chan->port_index];
    entry_idx = 0;
    while (entry_idx < (int16_t)port_table->entry_count) {
        mac_os_$pkt_type_entry_t *entry = &port_table->entries[entry_idx];

        if (entry->channel_index == (uint16_t)chan_num) {
            mac_os_$pkt_type_entry_t *last =
                &port_table->entries[port_table->entry_count - 1];

            entry->range_low     = last->range_low;
            entry->range_high    = last->range_high;
            entry->channel_index = last->channel_index;
            entry->reserved      = last->reserved;

            port_table->entry_count--;
            /* entry_idx is NOT advanced: the moved-down entry is retested */
        } else {
            entry_idx++;
        }
    }

    /*
     * 0x00E0B500: bclr.b #0x1,(0x7b2,A2) - the byte at channel offset 0x12 is
     * the HIGH half of the flags word, so this clears word bit 9,
     * MAC_OS_CHANNEL_IN_USE.
     */
    chan->flags &= (uint16_t)~MAC_OS_CHANNEL_IN_USE;

    /* 0x00E0B506 / 0x00E0B50A */
    chan->driver_info = NULL;
    chan->callback    = NULL;

    /* 0x00E0B50E */
    ML_$EXCLUSION_STOP(&MAC_OS_$EXCLUSION);
}
