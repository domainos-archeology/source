/*
 * MSG global data
 *
 * Module data blocks MSG_$WIRED_DATA and MSG_$UNWIRED_DATA: Claude Opus 5.5
 * (source-3llq).
 *
 * The MSG module's two data segments (docs/design-per-process-data.md), as
 * MODULE_DATA blocks that build/sau2/layout.ld links in the SAU2 map's
 * order; the address is the ordering key, not the link address.  Layouts,
 * biases and asserts in msg/msg_internal.h.
 *
 *   0x00E242E4  MSG_$WIRED_DATA    map "D E242E4 MSG_WIRED size = 20"
 *   0x00E80D84  MSG_$UNWIRED_DATA  map "D E80D84 MSG_UNWIRED size = 8E4"
 */

#include "msg/msg_internal.h"

/*
 * MSG_$WIRED_DATA, 0x00E242E4..0x00E24303 (`gsk read 0xE242E4 0x20`):
 *
 *   00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00
 *   00 00 00 00 00 00 00 00  00 00 00 00 ff ff 00 00
 *
 * The exclusion lock is zero (ML_$EXCLUSION_INIT sets it up, 0x00E31BC2) and
 * the page cells are filled by MSG_$INIT; DPAGE_LOCK (+0x1C) starts at -1, so
 * the first claim's pre-increment lands on 0 and owns the page.
 */
MODULE_DATA_DEFINE_INIT(msg_$wired_data_t, MSG_$WIRED_DATA, 0x00E242E4, {
    .dpage_lock = -1,
});

/*
 * MSG_$UNWIRED_DATA, 0x00E80D84..0x00E81667.  The only non-zero bytes in
 * the image (`gsk read 0xE80D84 0x8E4`) are the template's first 16:
 *
 *   +0x00  00 00 00 02 00 02 80 31  ff ff 00 00 ff ff 00 00
 *
 * i.e. flags 0 (MSG_$SEND / MSG_$SARI overwrite it), routing_type 2
 * (internet), addr_type 2, protocol 0x8031, retry_limit 0xFFFF, field_0a 0,
 * field_0c 0xFFFF - the same record as PKT_$DATA's ping templates apart from
 * the flags word.  The depth table, the ownership bitmaps and the open count
 * are filled in at run time by MSG_$INIT / MSG_$OPENI / MSG_$ALLOCATEI.
 */
MODULE_DATA_DEFINE_INIT(msg_$unwired_data_t, MSG_$UNWIRED_DATA, 0x00E80D84, {
    .send_template = {
        .flags        = 0x0000,
        .routing_type = 2,
        .addr_type    = 2,
        .protocol     = 0x8031,
        .retry_limit  = 0xFFFF,
        .field_0a     = 0,
        .field_0c     = 0xFFFF,
    },
});
