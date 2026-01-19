/*
 * RIP_$TABLE and RIP_$TABLE_D - Routing table access functions
 *
 * These functions provide read/write access to routing table entries.
 * RIP_$TABLE_D is the detailed version with full port identification.
 * RIP_$TABLE is a simplified wrapper for common use cases.
 *
 * Original addresses:
 *   RIP_$TABLE_D: 0x00E68E2C
 *   RIP_$TABLE:   0x00E68F90
 */

#include "rip/rip_internal.h"
#include "misc/string.h"

/*
 * RIP_$TABLE_D - Direct table entry access
 *
 * Reads or writes a routing table entry with full detail, including
 * port network and socket identification.
 *
 * Parameters:
 *   op_flag    - If *op_flag < 0, read; else write
 *   route_type - If *route_type < 0, non-standard route; else standard route
 *   index      - Pointer to entry index (0-63, masked to 6 bits)
 *   buffer     - Data buffer for read/write
 *   status_ret - Status return (0 = success)
 *
 * Read operation:
 *   Copies the entry at index to buffer, then looks up port info
 *   to fill in port_network and port_socket fields.
 *
 * Write operation:
 *   Uses ROUTE_$FIND_PORT to find the port index from buffer's
 *   port_network/port_socket, then writes the entry.
 *   Returns status_$internet_unknown_network_port if port not found.
 */
void RIP_$TABLE_D(int8_t *op_flag, int8_t *route_type, uint16_t *index,
                  rip_$table_d_buf_t *buffer, status_$t *status_ret)
{
    rip_$entry_t local_entry;
    rip_$route_t *route_ptr;
    route_$port_t *port_info;
    uint16_t masked_index;
    int8_t port_idx;
    int i;

    *status_ret = 0;

    if (*op_flag < 0) {
        /*
         * READ OPERATION
         *
         * 1. Mask index to valid range (0-63)
         * 2. Copy entire entry to local buffer
         * 3. Select route based on route_type
         * 4. Copy route data to output buffer
         * 5. Look up port info if port is valid
         */
        masked_index = *index & RIP_TABLE_MASK;

        /* Copy 44 bytes (11 longwords) from table to local entry */
        memcpy(&local_entry, &RIP_$INFO[masked_index], sizeof(rip_$entry_t));

        /* Output destination network */
        buffer->dest_network = local_entry.network;

        /* Select route based on route_type flag */
        if (*route_type < 0) {
            route_ptr = &local_entry.routes[1];  /* Non-standard route */
        } else {
            route_ptr = &local_entry.routes[0];  /* Standard route */
        }

        /* Copy nexthop address (10 bytes) */
        buffer->nexthop_network = route_ptr->nexthop.network;
        memcpy(buffer->nexthop_host, route_ptr->nexthop.host, 6);

        /* Copy expiration */
        buffer->expiration = route_ptr->expiration;

        /* Copy metric (stored as uint16_t, only low byte significant) */
        buffer->metric = route_ptr->metric;

        /* Extract state from upper 2 bits of flags */
        buffer->state = (route_ptr->flags >> RIP_STATE_SHIFT) & 0x03;

        /* Look up port info if port index is valid (0-7) */
        if (route_ptr->port < ROUTE_$MAX_PORTS) {
            port_info = ROUTE_$PORTP[route_ptr->port];
            buffer->port_network = port_info->network;
            buffer->port_socket = port_info->socket;
        } else {
            /* Invalid port - return special marker value */
            buffer->port_network = 0x0001;
            buffer->port_socket = 0x0000;
        }
    } else {
        /*
         * WRITE OPERATION
         *
         * 1. Find port index from port_network/port_socket
         * 2. If not found, return error status
         * 3. Select route based on route_type
         * 4. Copy data from buffer to route
         * 5. Write entry back to table
         */

        /* Select route based on route_type flag */
        if (*route_type < 0) {
            route_ptr = &local_entry.routes[1];  /* Non-standard route */
            /* Handle index overflow for non-standard routes */
            if ((int16_t)*index > RIP_TABLE_MASK) {
                /* Original code masks a byte in the entry for some reason */
                /* This appears to be an artifact of the Pascal implementation */
            }
        } else {
            route_ptr = &local_entry.routes[0];  /* Standard route */
            /* Handle index overflow for standard routes */
            if ((int16_t)*index > RIP_TABLE_MASK) {
                /* Similar handling for standard routes */
            }
        }

        /* Find port index by network/socket */
        port_idx = ROUTE_$FIND_PORT(buffer->port_network, buffer->port_socket);

        if (port_idx == (int8_t)-1) {
            /* Port not found - return error */
            *status_ret = status_$internet_unknown_network_port;
            return;
        }

        /* Copy entry from table to local buffer first */
        masked_index = *index & RIP_TABLE_MASK;
        memcpy(&local_entry, &RIP_$INFO[masked_index], sizeof(rip_$entry_t));

        /* Re-select route_ptr after memcpy */
        if (*route_type < 0) {
            route_ptr = &local_entry.routes[1];
        } else {
            route_ptr = &local_entry.routes[0];
        }

        /* Update destination network */
        local_entry.network = buffer->dest_network;

        /* Update route data */
        route_ptr->nexthop.network = buffer->nexthop_network;
        memcpy(route_ptr->nexthop.host, buffer->nexthop_host, 6);
        route_ptr->expiration = buffer->expiration;
        route_ptr->port = (uint8_t)port_idx;
        route_ptr->metric = (uint8_t)buffer->metric;

        /* Update state in flags (preserve lower 6 bits, set upper 2) */
        route_ptr->flags = (route_ptr->flags & 0x3F) |
                           ((buffer->state & 0x03) << RIP_STATE_SHIFT);

        /* Write entry back to table */
        memcpy(&RIP_$INFO[masked_index], &local_entry, sizeof(rip_$entry_t));
    }
}

/*
 * RIP_$TABLE - Simplified table entry access
 *
 * Wrapper around RIP_$TABLE_D that provides a more compact interface.
 * Always uses standard routes (route_type = 0).
 *
 * Parameters:
 *   op_flag - If *op_flag < 0, read; else write
 *   index   - Entry index (for reads via TABLE_D)
 *   buffer  - Compact data buffer
 *
 * Read operation:
 *   Calls TABLE_D to read the entry, then reformats into compact buffer.
 *   The port_index is determined by looking up the port_network/port_socket
 *   from the TABLE_D result.
 *
 * Write operation:
 *   Only accepts port_index < 8. Uses port_index to look up the port
 *   structure and get network/socket, then calls TABLE_D.
 */
void RIP_$TABLE(int8_t *op_flag, uint16_t *index, rip_$table_buf_t *buffer)
{
    rip_$table_d_buf_t table_d_buf;
    route_$port_t *port_info;
    status_$t status;
    int8_t route_type_flag = 0;  /* Always use standard routes */

    if (*op_flag < 0) {
        /*
         * READ OPERATION
         *
         * 1. Call TABLE_D to read the full entry
         * 2. Reformat into compact buffer format
         */
        RIP_$TABLE_D(op_flag, &route_type_flag, index, &table_d_buf, &status);

        /* Output destination network */
        buffer->dest_network = table_d_buf.dest_network;

        /*
         * Extract lower 20 bits of nexthop host address.
         * The nexthop_host is 6 bytes; we take bytes [2-5] (the lower 4 bytes)
         * and mask to 20 bits.
         */
        buffer->nexthop_host_low = ((uint32_t)table_d_buf.nexthop_host[2] << 24 |
                                    (uint32_t)table_d_buf.nexthop_host[3] << 16 |
                                    (uint32_t)table_d_buf.nexthop_host[4] << 8 |
                                    (uint32_t)table_d_buf.nexthop_host[5]) & 0xFFFFF;

        /* Output expiration */
        buffer->expiration = table_d_buf.expiration;

        /* Look up port index from network/socket */
        buffer->port_index = (uint8_t)ROUTE_$FIND_PORT(
            table_d_buf.port_network, table_d_buf.port_socket);

        /* Copy metric */
        buffer->metric = (uint8_t)table_d_buf.metric;

        /* Encode state in upper 2 bits of state_flags */
        buffer->state_flags = (buffer->state_flags & 0x3F) |
                              ((table_d_buf.state & 0x03) << 6);
    } else {
        /*
         * WRITE OPERATION
         *
         * 1. Check that port_index is valid (< 8)
         * 2. Look up port info to get network/socket
         * 3. Reformat compact buffer to TABLE_D format
         * 4. Call TABLE_D to write
         */
        if (buffer->port_index >= ROUTE_$MAX_PORTS) {
            /* Invalid port index - silently ignore */
            return;
        }

        /* Get port info from port index */
        port_info = ROUTE_$PORTP[buffer->port_index];

        /* Build TABLE_D buffer */
        table_d_buf.expiration = buffer->expiration;
        table_d_buf.dest_network = buffer->dest_network;

        /*
         * Reconstruct nexthop from port info and compact buffer.
         * The nexthop network comes from port structure.
         * The nexthop host lower bytes come from the compact buffer.
         */
        table_d_buf.nexthop_network = port_info->network;
        table_d_buf.port_network = port_info->network;
        table_d_buf.port_socket = port_info->socket;

        /* Reconstruct nexthop host from stored lower 20 bits */
        table_d_buf.nexthop_host[0] = 0;
        table_d_buf.nexthop_host[1] = 0;
        table_d_buf.nexthop_host[2] = (buffer->nexthop_host_low >> 24) & 0x0F;
        table_d_buf.nexthop_host[3] = (buffer->nexthop_host_low >> 16) & 0xFF;
        table_d_buf.nexthop_host[4] = (buffer->nexthop_host_low >> 8) & 0xFF;
        table_d_buf.nexthop_host[5] = buffer->nexthop_host_low & 0xFF;

        /* Copy metric */
        table_d_buf.metric = buffer->metric;

        /* Extract state from upper 2 bits */
        table_d_buf.state = (buffer->state_flags >> 6) & 0x03;

        /* Call TABLE_D to write */
        RIP_$TABLE_D(op_flag, &route_type_flag, index, &table_d_buf, &status);
    }
}
