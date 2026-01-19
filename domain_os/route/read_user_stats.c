/*
 * ROUTE_$READ_USER_STATS - Read user-visible routing statistics
 *
 * This function retrieves routing statistics for a user-mode port.
 * It looks up the port by socket number (assuming network type 2 = routing),
 * then copies statistics data from the port's driver structure to the
 * caller's buffer.
 *
 * @param socket_ptr    Pointer to socket number (uint16_t)
 * @param stats_buf     Output buffer for statistics data
 * @param length_ret    Output: number of bytes written to stats_buf
 * @param status_ret    Output: status code (status_$ok or error)
 *
 * The statistics format appears to be:
 *   - 1 byte: flags/type
 *   - 4 bytes: field 1
 *   - 4 bytes: field 2
 *   - N * 4 bytes: additional data (count based on port+0x34)
 *
 * Original address: 0x00E6A65E
 */

#include "route/route_internal.h"

/*
 * Port structure offsets used:
 *   0x34: count of additional data longs (minus 1)
 *   0x36: copy control value
 *   0x44: pointer to driver/stats structure
 *
 * Driver/stats structure layout:
 *   +0x00: 1 byte flags
 *   +0x02: 4 bytes data 1
 *   +0x06: 4 bytes data 2
 *   +0x0A: variable length data (4 bytes each)
 */

/* Base statistics structure size (flags + 2 longs) */
#define STATS_BASE_SIZE     10

void ROUTE_$READ_USER_STATS(uint16_t *socket_ptr, uint8_t *stats_buf,
                            int16_t *length_ret, status_$t *status_ret)
{
    int16_t port_index;
    route_$port_t *port;
    uint8_t *driver_stats;
    int16_t copy_count;
    int16_t i;
    uint32_t socket_ext;

    /*
     * Look up the port by socket number
     * Network type is always 2 (ROUTE_PORT_TYPE_ROUTING) for user stats
     * Socket is zero-extended to 32 bits
     */
    socket_ext = (uint32_t)*socket_ptr;
    port_index = ROUTE_$FIND_PORT(ROUTE_PORT_TYPE_ROUTING, (int32_t)socket_ext);

    if (port_index == -1) {
        /* Port not found */
        *length_ret = 0;
        *status_ret = status_$internet_unknown_network_port;
        return;
    }

    /*
     * Get pointer to port structure
     */
    port = &ROUTE_$PORT_ARRAY[port_index];

    /*
     * Get pointer to driver stats structure at port+0x44
     * This is an indirect pointer - the port structure contains a pointer
     * to another structure that holds the actual statistics
     */
    driver_stats = *(uint8_t **)((uint8_t *)port + 0x44);

    /*
     * Copy base statistics (10 bytes):
     *   - 1 byte flags at offset 0
     *   - 4 bytes at offset 2
     *   - 4 bytes at offset 6
     */
    stats_buf[0] = driver_stats[0];
    *(uint32_t *)(stats_buf + 2) = *(uint32_t *)(driver_stats + 2);
    *(uint32_t *)(stats_buf + 6) = *(uint32_t *)(driver_stats + 6);

    /*
     * Copy additional data based on count at port+0x36
     * The value at port+0x36 is (count - 1), so loop while >= 0
     */
    copy_count = *(int16_t *)((uint8_t *)port + 0x36);
    if (copy_count >= 0) {
        uint8_t *src = driver_stats;
        uint8_t *dst = stats_buf;

        for (i = copy_count; i >= 0; i--) {
            *(uint32_t *)(dst + 10) = *(uint32_t *)(src + 10);
            dst += 4;
            src += 4;
        }
    }

    /*
     * Calculate total bytes written
     * Formula: (count_at_0x34 + 1) * 4 + 10
     * This gives base size (10) plus variable data
     */
    {
        int32_t count = *(int32_t *)((uint8_t *)port + 0x34);
        int32_t total_size = ((count + 1) * 4) + STATS_BASE_SIZE;
        *length_ret = (int16_t)total_size;
    }

    *status_ret = status_$ok;
}
