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
    void *route_port;
    void *driver_info;
#if defined(M68K)
    mac_os_$port_pkt_table_t *port_table;
    mac_os_$channel_t *chan;
    uint32_t *pkt_type_ptr;
    uint16_t num_pkt_types;
    uint16_t net_type;

    *status_ret = status_$ok;

    port = *port_num;

    /* Look up route port structure */
    route_port = ((void **)0xE26EE8)[port];
    if (route_port == NULL) {
        *status_ret = status_$mac_port_op_not_implemented;
        return;
    }

    /* Get driver info from route port (offset 0x48) */
    driver_info = *(void **)((uint8_t *)route_port + ROUTE_PORT_DRIVER_INFO_OFFSET);
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
    ML_$EXCLUSION_START((ml_$exclusion_t *)MAC_OS_$EXCLUSION);

    /* Find an available channel (one without IN_USE flag set) */
    channel = 0;
    chan = MAC_OS_$CHANNEL_TABLE;
    while ((chan->flags & MAC_OS_FLAG_IN_USE) != 0) {
        if (channel >= MAC_OS_MAX_CHANNELS - 1) {
            *status_ret = status_$mac_no_channels_available;
            goto cleanup;
        }
        channel++;
        chan++;
    }

    /* Get port's packet type table */
    port_table = &((mac_os_$port_pkt_table_t *)MAC_OS_$DATA_BASE)[port];

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
        uint32_t channel_offset = (uint32_t)channel * MAC_OS_CHANNEL_SIZE;
        mac_os_$channel_t *chan_entry = (mac_os_$channel_t *)(MAC_OS_$DATA_BASE + 0x7A0 + channel_offset);

        /* Store callback function pointer */
        /* params offset 0x50 contains callback */
        chan_entry->callback = *(void **)((uint8_t *)params + 0x50);

        /* Set OPEN flag (bit 1) */
        chan_entry->flags |= MAC_OS_FLAG_OPEN;

        /* Clear non-ASID bits, keep only bits 0-1 (open/promisc) */
        chan_entry->flags &= 0x03;

        /* Set owner ASID in bits 2-7 */
        chan_entry->flags |= (uint16_t)(PROC1_$AS_ID << MAC_OS_FLAG_ASID_SHIFT);

        /* Store port number */
        chan_entry->port_index = port;

        /* Store line number from route port (offset 0x30) */
        chan_entry->line_number = *(uint16_t *)((uint8_t *)route_port + ROUTE_PORT_LINE_NUM_OFFSET);

        /* Store driver info pointer */
        chan_entry->driver_info = driver_info;

        /* Determine header size based on network type (offset 0x2E of route_port) */
        net_type = *(uint16_t *)((uint8_t *)route_port + ROUTE_PORT_NET_TYPE_OFFSET);
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
    {
        void (*driver_open)(uint16_t, void *, void *, status_$t *);
        uint16_t line_num = *(uint16_t *)((uint8_t *)route_port + ROUTE_PORT_LINE_NUM_OFFSET);

        driver_open = *(void **)((uint8_t *)driver_info + MAC_OS_DRIVER_OPEN_OFFSET);
        (*driver_open)(line_num, params, params, status_ret);
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
        /* Failure - clear channel's OPEN flag */
        mac_os_$channel_t *chan_entry = &MAC_OS_$CHANNEL_TABLE[channel];
        chan_entry->flags &= ~MAC_OS_FLAG_OPEN;
        chan_entry->callback = NULL;
    }

    ML_$EXCLUSION_STOP((ml_$exclusion_t *)MAC_OS_$EXCLUSION);
#else
    /* Non-M68K implementation stub */
    (void)port_num;
    (void)params;
    *status_ret = status_$mac_port_op_not_implemented;
#endif
}
