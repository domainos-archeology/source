/*
 * PKT Data - Global data for PKT subsystem
 *
 * Module data block PKT_$DATA: Claude Opus 5.5 (source-r3tc).
 *
 * PKT_$DATA, map "D E24C9C PKT size = A8": the A5 block of every PKT
 * routine (layout, biases and asserts in pkt/pkt_internal.h), a MODULE_DATA
 * block linked in the map's order after PEB_PARITY and before PMAP_.  The
 * address is the ordering key, not the link address.
 */

#include "pkt/pkt_internal.h"

/*
 * Image contents, `gsk read 0xE24C9C 0xA8`:
 *
 *   0x00E24C9C  +0x00  0x50 zero bytes          missing_nodes[1..10]
 *   0x00E24CEC  +0x50  00 00 00 00              spin_lock
 *   0x00E24CF0  +0x54  00 00 00 01              visibility_seq = 1
 *   0x00E24CF4  +0x58  00 00                    n_missing = 0
 *   0x00E24CF6  +0x5A  00 01                    ping_req_hdr = 1
 *   0x00E24CF8  +0x5C  00 00 00 00              short_id = 0, pad
 *   0x00E24CFC  +0x60  00 00 00 00              long_id = 0
 *   0x00E24D00  +0x64  00 00 00 00              default_flags = 0, pad
 *   0x00E24D04  +0x68  00 10 00 02 00 02 80 31  ping_template
 *               +0x70  ff ff 00 00 ff ff 00 00
 *               +0x78  16 zero bytes            ping_template.addr / pad
 *   0x00E24D24  +0x88  00 20 00 02 00 02 80 31  ping_reply_info
 *               +0x90  ff ff 00 00 ff ff 00 00
 *               +0x98  16 zero bytes
 *
 * Every byte not named below is zero.
 */
MODULE_DATA_DEFINE_INIT(pkt_$data_t, PKT_$DATA, 0x00E24C9C, {
    /* 0x00E24CF0: the sequence counter starts at 1, not 0 */
    .visibility_seq = 1,

    /* 2-byte ping request header; image value at 0x00E24CF6 is 0x0001 */
    .ping_req_hdr = 1,

    /* 0x00E24D04 */
    .ping_template = {
        .flags        = 0x0010,
        .routing_type = 2,          /* internet */
        .addr_type    = 2,
        .protocol     = 0x8031,
        .retry_limit  = 0xFFFF,
        .field_0a     = 0,
        .field_0c     = 0xFFFF,
    },

    /* 0x00E24D24 - identical apart from the flags word */
    .ping_reply_info = {
        .flags        = 0x0020,
        .routing_type = 2,
        .addr_type    = 2,
        .protocol     = 0x8031,
        .retry_limit  = 0xFFFF,
        .field_0a     = 0,
        .field_0c     = 0xFFFF,
    },
});
