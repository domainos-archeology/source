/*
 * MAC_OS_$DEMUX - Demultiplex a received packet
 *
 * Routes an incoming packet to the appropriate channel callback
 * based on the packet type and port's packet type table.
 *
 * Original address: 0x00E0B816
 * Original size: 168 bytes
 */

#include "mac_os/mac_os_internal.h"

/*
 * MAC_OS_$DEMUX
 *
 * This function is called by the network driver when a packet is received.
 * It looks up the packet type in the port's table and calls the appropriate
 * channel's callback function.
 *
 * Parameters:
 *   pkt_info   - mac_os_$rcv_pkt_t the driver built; see mac_os/mac_os.h
 *   port_num   - Pointer to port number (0-7)
 *   param3     - Additional parameter (passed through to callback)
 *   status_ret - Pointer to receive status code
 *
 * Assembly notes:
 *   - Uses A5 = 0xE22990 (MAC_OS_$DATA base)
 *   - Gets current time via TIME_$ABS_CLOCK
 *   - Looks up packet type in port's table
 *   - Calls channel callback with: pkt_info, port_num, param3, status_ret
 */
/*
 * mac_os_$demux_callback_t - the channel's receive entry, called at
 * 0x00E0B8A4-0x00E0B8B2.  The pushes, right to left, are
 *   pea    (A3)               arg 4, status_ret
 *   move.l (0x10,A6),-(SP)    arg 3, the caller's third argument
 *   move.l D2,-(SP)           arg 2, the port number pointer
 *   pea    (A4)               arg 1, the packet descriptor
 * with no result slot, so the entry is a Pascal procedure.
 */
typedef void (*mac_os_$demux_callback_t)(mac_os_$rcv_pkt_t *pkt_info,
                                         int16_t *port_num,
                                         void *param3,
                                         status_$t *status_ret);

void MAC_OS_$DEMUX(mac_os_$rcv_pkt_t *pkt_info, int16_t *port_num,
                   void *param3, status_$t *status_ret)
{
    int16_t entry_idx;
    int16_t channel;
    mac_os_$port_pkt_table_t *port_table;
    mac_os_$channel_t *chan;
    mac_os_$demux_callback_t callback;
    clock_t timestamp;                  /* A6-0x4C */

    *status_ret = status_$ok;           /* 0x00E0B830 clr.l (A3) */

    /* 0x00E0B832 */
    TIME_$ABS_CLOCK(&timestamp);

    /*
     * 0x00E0B83E-0x00E0B846: the port index is a WORD read through the second
     * argument, scaled by the 0xF4 table stride off A5.
     */
    port_table = &MAC_OS_$PORT_PKT_TABLES[*port_num];

    /*
     * 0x00E0B84A-0x00E0B85A: MAC_OS_$FIND_PACKET_TYPE(frame_type,
     * &table->entries[0], table->entry_count).  The pushes are the count word
     * (A2), the entry array address (A2+4) and the frame type, so the frame
     * type is argument 1.
     */
    entry_idx = MAC_OS_$FIND_PACKET_TYPE(pkt_info->frame_type,
                                         &port_table->entries[0],
                                         port_table->entry_count);

    /*
     * 0x00E0B85E: a miss, and 0x00E0B882: a channel with no callback, share
     * one error exit at 0x00E0B888.  The SR10.4 status-code table has no text
     * for 0x003A000F, so the name stays the placeholder mac_os.h carries.
     */
    if (entry_idx == -1) {
        *status_ret = status_$mac_XXX_unknown;
        return;
    }

    /*
     * 0x00E0B864-0x00E0B870: entries are 12 bytes and the channel index is
     * the word at entry + 0x08, which the image reaches as (0xc,A2,D1) with
     * A2 = the table base and the entries starting at A2+4.
     */
    channel = (int16_t)port_table->entries[entry_idx].channel_index;

    /* 0x00E0B874-0x00E0B880: channel entries are 0x14 bytes at A5+0x7A0 */
    chan = &MAC_OS_$CHANNEL_TABLE[channel];

    /* 0x00E0B882: tst.l (0x7a0,A2) - the callback is the entry's first field */
    if (chan->callback == 0) {
        *status_ret = status_$mac_XXX_unknown;
        return;
    }

    /* 0x00E0B890 / 0x00E0B896: the arrival time, longword then word */
    pkt_info->time_high = timestamp.high;
    pkt_info->time_low  = timestamp.low;

    /*
     * 0x00E0B89C-0x00E0B8A0: "lea (0x7a0,A2),A0 / move.l A0,(0x34,A4)" - the
     * channel ENTRY'S ADDRESS, not its index, and it is stored as a longword
     * VA.
     */
    pkt_info->channel = ARCH_PTR_TO_VA(chan);

    /* 0x00E0B8A4-0x00E0B8B2 */
    callback = (mac_os_$demux_callback_t)ARCH_VA_TO_PTR(chan->callback);
    (*callback)(pkt_info, port_num, param3, status_ret);
}
