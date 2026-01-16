/*
 * RING_$GET_STATS - Get statistics for a ring unit
 *
 * Copies the per-unit statistics to the provided buffer.
 *
 * Original address: 0x00E76950
 *
 * Assembly analysis:
 *   - Validates unit < 2 and initialized
 *   - Copies 0x3C bytes (15 longs) from RING_$STATS[unit] to buffer
 *   - Returns length 0x3C
 */

#include "ring/ring_internal.h"

/*
 * Per-unit statistics array.
 * Located at 0xE261E0, 0x3C bytes per unit.
 */
ring_$stats_t RING_$STATS[RING_MAX_UNITS];

/*
 * RING_$GET_STATS - Get unit statistics
 *
 * @param unit_ptr      Pointer to unit number
 * @param stats_buf     Output buffer (must be at least 0x3C bytes)
 * @param unused        Unused parameter
 * @param len_out       Output: bytes copied
 * @param status_ret    Output: status code
 */
void RING_$GET_STATS(uint16_t *unit_ptr, void *stats_buf, uint16_t unused,
                     uint16_t *len_out, status_$t *status_ret)
{
    uint16_t unit_num;
    ring_unit_t *unit_data;
    uint32_t *src;
    uint32_t *dst;
    int16_t count;

    (void)unused;

    unit_num = *unit_ptr;

    /*
     * Validate unit number.
     */
    if (unit_num > 1) {
        *len_out = 0;
        *status_ret = status_$internet_unknown_network_port;
        return;
    }

    /*
     * Get unit data and check initialization.
     */
    unit_data = &RING_$DATA.units[unit_num];

    if (unit_data->initialized >= 0) {
        *len_out = 0;
        *status_ret = status_$internet_unknown_network_port;
        return;
    }

    /*
     * Copy statistics to output buffer.
     * The assembly copies 15 longs (0x3C bytes) using a dbf loop.
     *
     * Assembly:
     *   moveq #0xe,D1           ; 15 iterations
     *   move.l (A3)+,(A4)+      ; Copy long
     *   dbf D1w,loop
     */
    src = (uint32_t *)&RING_$STATS[unit_num];
    dst = (uint32_t *)stats_buf;

    for (count = 0; count < 15; count++) {
        *dst++ = *src++;
    }

    /*
     * Return bytes copied.
     */
    *len_out = RING_STATS_SIZE;  /* 0x3C */
    *status_ret = status_$ok;
}
