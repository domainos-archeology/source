/*
 * Ring log module global data
 *
 * Module data blocks RINGLOG_$CTL and RINGLOG_$DATA: Claude Opus 5.5
 * (source-vulx).
 *
 * RINGLOG_ has two data segments in the SAU2 map, both MODULE_DATA blocks
 * linked in the map's order (the address is the ordering key, not the link
 * address); layouts and asserts in ring/ringlog.h and ring/ringlog_internal.h:
 *
 *   D    E2C32C  RINGLOG_          size = 3C    RINGLOG_$CTL
 *   D53  EA3E38  RINGLOG_$DATA     size = 11FC  RINGLOG_$DATA
 */

#include "ring/ring_internal.h"
#include "ring/ringlog_internal.h"

/*
 * RINGLOG_$CTL, 0x00E2C32C..0x00E2C367 (`gsk read 0xE2C32C 60`): zero except
 *
 *   0x00E2C35E  +0x32  ff 00   mbx_sock_filter = -1 (don't filter)
 *   0x00E2C360  +0x34  ff 00   who_sock_filter = -1
 *   0x00E2C362  +0x36  ff 00   nil_sock_filter = -1
 *   0x00E2C364  +0x38  00 00   logging_active  = 0
 *   0x00E2C366  +0x3A  ff 00   first_entry_flag = -1 (reset the index
 *                              before the first entry)
 */
MODULE_DATA_DEFINE_INIT(ringlog_ctl_t, RINGLOG_$CTL, 0x00E2C32C, {
    .mbx_sock_filter  = -1,
    .who_sock_filter  = -1,
    .nil_sock_filter  = -1,
    .logging_active   = 0,
    .first_entry_flag = -1,
});

/*
 * RINGLOG_$DATA, 0x00EA3E38, 0x11FC bytes: the next-entry index word followed
 * by 100 entries of 0x2E bytes that START AT OFFSET ZERO, so entry 0's first
 * word IS the index.  The segment lies past the end of the loaded image
 * (the SR10.2 SAU2 file maps 0xE00000..0xE9534E; Ghidra has no bytes at
 * 0xEA3E38), so it starts zero-filled.
 */
MODULE_DATA_DEFINE(ringlog_$data_t, RINGLOG_$DATA, 0x00EA3E38);
