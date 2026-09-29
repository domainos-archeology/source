/*
 * ASKNODE - Data Definitions
 *
 * Global data used by the ASKNODE subsystem.
 *
 * Module data block ASKNODE_$DATA: Claude Opus 5.5 (source-esg8).
 */

#include "asknode/asknode_internal.h"

/*
 * ASKNODE_$DATA, map "D E82408 ASKNODE size = 20": the A5 block of the
 * ASKNODE routines (layout and asserts in asknode/asknode_internal.h), a
 * MODULE_DATA block linked in the map's order after REM_FILE and before
 * OS_TERM; the address is the ordering key, not the link address.
 *
 * Image contents (`gsk read 0xE82408 0x20`):
 *
 *   +0x00  00 10 00 02 00 02 80 31  ff ff 00 00 ff ff 00 00
 *   +0x10  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 03
 *
 * i.e. the packet-info template flags 0x0010, routing_type 2 (internet),
 * addr_type 2, protocol 0x8031, retry_limit 0xFFFF, field_0a 0, field_0c
 * 0xFFFF, a zero address, and protocol_version 3 in the last word.
 */
MODULE_DATA_DEFINE_INIT(asknode_$data_t, ASKNODE_$DATA, 0x00E82408, {
    .pkt_info = {
        .flags        = 0x0010,
        .routing_type = 2,
        .addr_type    = 2,
        .protocol     = 0x8031,
        .retry_limit  = 0xFFFF,
        .field_0a     = 0,
        .field_0c     = 0xFFFF,
        .pad_1e       = 3,      /* = protocol_version (0x00E82426) */
    },
});

/*
 * The socket event count pointers (0x00E28DB0 + n * 4) are entries of the
 * socket pointer table in sock/sock_data.c (SOCK_$EVENT_COUNTERS).
 */

/*
 * ASKNODE_$EMPTY_DATA - Zero longword used as "no data" (0x00E658CC)
 */
uint32_t ASKNODE_$EMPTY_DATA = 0;

/* NETWORK_$CAPABLE_FLAGS (0xE24C3F) is defined in network/network_data.c */
