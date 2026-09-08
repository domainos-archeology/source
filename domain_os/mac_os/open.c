/*
 * MAC_OS_$OPEN - Open a MAC channel at OS level
 *
 * Opens a low-level MAC channel by:
 * 1. Validating the port has driver support
 * 2. Finding an available channel
 * 3. Adding packet type entries to the port's table
 * 4. Calling the driver's open callback
 *
 * Original address: 0x00E0B246
 * Original size: 522 bytes
 */

#include "mac_os/mac_os_internal.h"

/*
 * MAC_OS_$OPEN
 *
 * Parameters:
 *   port_num   - Pointer to port number (0-7)
 *   params     - Open parameters containing packet types and callback
 *   status_ret - Pointer to receive status code
 *
 * On success, updates params with:
 *   - MTU value from driver at offset 0
 *   - Channel number at offset 4 (replaces socket_count field)
 *
 * The params structure layout:
 *   0x00-0x4F: Packet type min/max pairs (up to 10 pairs, 8 bytes each)
 *   0x50: Callback function pointer
 *   0x54: Number of packet types (1-based count)
 *
 * Assembly notes:
 *   - Uses A5 = 0xE22990 (MAC_OS_$DATA base)
 *   - Port packet table at A5 + port * 0xF4
 *   - Channel table at A5 + 0x7A0 + channel * 0x14
 *   - Exclusion lock at A5 + 0x868
 */
void MAC_OS_$OPEN(int16_t *port_num, mac_os_$open_params_t *params, status_$t *status_ret)
{
    int16_t port;
    int16_t channel;
    int16_t new_count;
    int16_t i;
    route_$port_t *route_port;
    void *driver_info;
    mac_os_$port_pkt_table_t *port_table;
    mac_os_$channel_t *chan;
    uint32_t *pkt_type_ptr;
    uint16_t num_pkt_types;
    uint16_t net_type;

    *status_ret = status_$ok;

    port = *port_num;

    /* 0x00E0B262-0x00E0B274: ROUTE_$PORTP[port] */
    route_port = ROUTE_$PORTP[port];
    if (route_port == NULL) {
        *status_ret = status_$mac_port_op_not_implemented;
        return;
    }

    /* 0x00E0B276-0x00E0B280: route_port->driver_info */
    driver_info = (void *)ARCH_VA_TO_PTR(route_port->driver_info);
    if (driver_info == NULL) {
        *status_ret = status_$mac_port_op_not_implemented;
        return;
    }

    /* Check if driver has open callback (offset 0x3C) */
    if (*(void **)((uint8_t *)driver_info + MAC_OS_DRIVER_OPEN_OFFSET) == NULL) {
        *status_ret = status_$mac_port_op_not_implemented;
        return;
    }

    /* Enter exclusion region */
    ML_$EXCLUSION_START(&MAC_OS_$EXCLUSION);

    /*
     * 0x00E0B2A2-0x00E0B2CA: find a channel whose IN_USE bit is clear.  The
     * bound test comes AFTER the slot test, so slot 10 - one past the ten real
     * channels - is examined before the table is declared full:
     *   clr.w D2w / movea.l A5,A0 / bra 0x00E0B2C2
     *   0x00E0B2A8  cmpi.w #0xa,D2w / bcs 0x00E0B2BC
     *   0x00E0B2AE  status 0x003A0002
     *   0x00E0B2BC  addq.w #0x1,D2w / lea (0x14,A0),A0
     *   0x00E0B2C2  move.w (0x7b2,A0),D0w / btst.l #0x9,D0 / bne 0x00E0B2A8
     * In the image slot 10 is the head of MAC_OS_$EXCLUSION; the C table has a
     * sentinel slot there (MAC_OS_CHANNEL_TABLE_SLOTS).
     */
    channel = 0;
    chan = MAC_OS_$CHANNEL_TABLE;
    while ((chan->flags & MAC_OS_CHANNEL_IN_USE) != 0) {
        if (channel >= MAC_OS_MAX_CHANNELS) {
            *status_ret = status_$mac_no_channels_available;
            goto cleanup;
        }
        channel++;
        chan++;
    }

    /* Get port's packet type table */
    port_table = &MAC_OS_$PORT_PKT_TABLES[port];

    /* Check if adding packet types would exceed table capacity */
    num_pkt_types = *(uint16_t *)((uint8_t *)params + 0x54);
    new_count = port_table->entry_count + num_pkt_types;
    if (new_count > MAC_OS_MAX_PKT_TYPES) {
        *status_ret = status_$mac_packet_type_table_full;
        goto cleanup;
    }

    /* Add each packet type entry to the port's table */
    pkt_type_ptr = (uint32_t *)params;
    for (i = 0; i < num_pkt_types; i++) {
        int8_t overlap;

        /* Check for overlap with existing entries */
        overlap = (int8_t)MAC_OS_$CHECK_RANGE_OVERLAP(
            pkt_type_ptr,
            &port_table->entries[0],
            port_table->entry_count
        );
        if (overlap < 0) {
            *status_ret = status_$mac_packet_type_in_use;
            goto cleanup;
        }

        /* Add entry at current count position */
        {
            int16_t entry_idx = port_table->entry_count + i;
            mac_os_$pkt_type_entry_t *entry = &port_table->entries[entry_idx];

            entry->range_low = pkt_type_ptr[0];
            entry->range_high = pkt_type_ptr[1];
            entry->channel_index = channel;
        }

        pkt_type_ptr += 2;  /* Move to next min/max pair */
    }

    /* Set up channel entry */
    {
        mac_os_$channel_t *chan_entry = &MAC_OS_$CHANNEL_TABLE[channel];

        /* Store callback function pointer */
        /* params offset 0x50 contains callback */
        chan_entry->callback =
            ARCH_PTR_TO_VA(*(void **)((uint8_t *)params + 0x50));

        /*
         * 0x00E0B36C-0x00E0B382.  All three operations work on the BYTE at
         * channel offset 0x12, which is the flags word's HIGH half:
         *   bset.b #0x1,(0x7b2,A0)     -> set word bit 9 (IN_USE)
         *   andi.b #0x3,(0x7b2,A0)     -> keep word bits 8 and 9 only; the
         *                                 low byte of the word is untouched
         *   move.b (PROC1_$AS_ID+1),D1b / lsl.b #0x2,D1b / or.b D1b,(0x7b2,A0)
         *                              -> word bits 10..15 = AS_ID & 0x3F
         */
        chan_entry->flags |= MAC_OS_CHANNEL_IN_USE;
        chan_entry->flags &= (uint16_t)(MAC_OS_CHANNEL_PROMISCUOUS |
                                        MAC_OS_CHANNEL_IN_USE | 0x00FF);
        chan_entry->flags |= (uint16_t)((PROC1_$AS_ID & 0x3F)
                                        << MAC_OS_CHANNEL_OWNER_SHIFT);

        /* Store port number */
        chan_entry->port_index = port;

        /* 0x00E0B38E: move.w (0x30,A3),(0x7ae,A0) */
        chan_entry->line_number = route_port->socket;

        /* Store driver info pointer */
        chan_entry->driver_info = ARCH_PTR_TO_VA(driver_info);

        /* Determine header size based on network type (offset 0x2E of route_port) */
        /*
         * 0x00E0B39A-0x00E0B3DA.  The jump table at 0x00E0B3AE holds, in the
         * image's own bytes,
         *   00e0b3ae  00 0c 00 22 00 22 00 0c  00 1a 00 14
         * i.e. targets 0x00E0B3AE plus the word:
         *   0 -> 0x00E0B3BA  move.w #0x1c,(0x7b0,A0)   header size 0x1C
         *   1 -> 0x00E0B3D0  status 0x003A0001
         *   2 -> 0x00E0B3D0  status 0x003A0001
         *   3 -> 0x00E0B3BA  header size 0x1C
         *   4 -> 0x00E0B3C8  move.w #0xe,(0x7b0,A0)    header size 0x0E
         *   5 -> 0x00E0B3C2  clr.w (0x7b0,A0)          header size 0
         * and "cmpi.w #0x6,D0w / bcc 0x00E0B3D0" sends 6 and above to the
         * same error.
         */
        net_type = route_port->port_type;
        switch (net_type) {
        case MAC_OS_NET_TYPE_ETHERNET:
        case MAC_OS_NET_TYPE_3:
            chan_entry->header_size = MAC_OS_HDR_SIZE_ETHERNET;
            break;

        case MAC_OS_NET_TYPE_TOKEN_RING:
            chan_entry->header_size = MAC_OS_HDR_SIZE_TOKEN_RING;
            break;

        case MAC_OS_NET_TYPE_FDDI:
            chan_entry->header_size = MAC_OS_HDR_SIZE_FDDI;
            break;

        default:
            *status_ret = status_$mac_port_op_not_implemented;
            goto cleanup;
        }
    }

    /* Call driver open callback */
    /* Parameters passed: line_number, params, status_ret */
    /*
     * 0x00E0B3DC-0x00E0B3F2: three arguments plus a discarded word result.
     *   subq.l #0x2,SP
     *   move.l (0x10,A6),-(SP)     arg 3, status_ret
     *   move.l D7,-(SP)            arg 2, params
     *   move.w (0x30,A3),-(SP)     arg 1, route_port->socket (the line number)
     *   movea.l (0x3c,A4),A1 / jsr (A1) / lea (0xc,SP),SP
     */
    {
        int16_t (*driver_open)(uint16_t, mac_os_$open_params_t *, status_$t *);
        uint16_t line_num = route_port->socket;

        driver_open = *(void **)((uint8_t *)driver_info + MAC_OS_DRIVER_OPEN_OFFSET);
        (void)(*driver_open)(line_num, params, status_ret);
    }

cleanup:
    if (*status_ret == status_$ok) {
        /* Success - finalize */
        port_table->entry_count = new_count;

        /* Store callback data in channel entry */
        {
            mac_os_$channel_t *chan_entry = &MAC_OS_$CHANNEL_TABLE[channel];
            /* params+4 contains the callback data to save */
            chan_entry->callback_data = *(uint16_t *)((uint8_t *)params + 4);
        }

        /* Return channel number in params+4 (replaces callback_data field) */
        *(uint16_t *)((uint8_t *)params + 4) = channel;

        /* Return MTU from driver info+4 in params */
        *(uint32_t *)params = (uint32_t)*(uint16_t *)((uint8_t *)driver_info + 4);
    } else {
        /* 0x00E0B43E: bclr.b #0x1,(0x7b2,A0) - word bit 9, IN_USE */
        mac_os_$channel_t *chan_entry = &MAC_OS_$CHANNEL_TABLE[channel];
        chan_entry->flags &= (uint16_t)~MAC_OS_CHANNEL_IN_USE;
        chan_entry->callback = 0;
    }

    ML_$EXCLUSION_STOP(&MAC_OS_$EXCLUSION);
}
