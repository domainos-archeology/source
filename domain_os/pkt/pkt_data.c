/*
 * PKT Data - Global data for PKT subsystem
 *
 * Contains the global data structure for the packet module.
 * On m68k, this data resides at address 0xE24C9C.
 *
 * For non-m68k platforms, we define the data structure here
 * and it is referenced via the PKT_$DATA macro.
 */

#include "pkt/pkt_internal.h"

#if !defined(ARCH_M68K)

/*
 * PKT module global data
 *
 * This structure contains all the global state for the PKT subsystem:
 * - Missing node tracking for node visibility
 * - Spin lock for thread-safe ID generation
 * - Packet ID counters (short and long)
 * - Ping request configuration
 */
/*
 * The initialisers below are the loaded image at 0x00E24C9C, read with
 * "gsk read 0x00E24C9C 0x100":
 *
 *   0x00E24C9C  +0x00  50 zero bytes            missing_nodes[10]
 *   0x00E24CEC  +0x50  00 00 00 00              spin_lock
 *   0x00E24CF0  +0x54  00 00 00 01              visibility_seq = 1
 *   0x00E24CF4  +0x58  00 00                    n_missing = 0
 *   0x00E24CF6  +0x5A  00 01                    ping_req_hdr = 1
 *   0x00E24CF8  +0x5C  00 00                    short_id = 0
 *   0x00E24CFC  +0x60  00 00 00 00              long_id = 0
 *   0x00E24D00  +0x64  00 00                    default_flags = 0
 *   0x00E24D04  +0x68  00 10 00 02 00 02 80 31  ping_template
 *              +0x70  ff ff 00 00 ff ff 00 00
 *              +0x78  16 zero bytes             ping_template.addr / pad
 *   0x00E24D24  +0x88  00 20 00 02 00 02 80 31  ping_reply_info
 *              +0x90  ff ff 00 00 ff ff 00 00
 *              +0x98  16 zero bytes
 */
pkt_$data_t PKT_$DATA_STRUCT = {
    /* Missing node tracking - all zeroed initially */
    .missing_nodes = { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
                       { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },

    .spin_lock = 0,

    /* 0x00E24CF0: the sequence counter starts at 1, not 0 */
    .visibility_seq = 1,

    .n_missing = 0,

    /* 2-byte ping request header; image value at 0x00E24CF6 is 0x0001 */
    .ping_req_hdr = 1,

    /* 0x00E24CF8 / 0x00E24CFC: both id counters start at 0 */
    .short_id = 0,
    .pad_5e = 0,
    .long_id = 0,

    .default_flags = 0,
    .pad_66 = 0,

    /* 0x00E24D04 */
    .ping_template = {
        .flags        = 0x0010,
        .routing_type = 2,          /* internet */
        .addr_type    = 2,
        .protocol     = 0x8031,
        .retry_limit  = 0xFFFF,
        .field_0a     = 0,
        .field_0c     = 0xFFFF,
        .addr         = { 0 },
        .pad_1e       = 0
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
        .addr         = { 0 },
        .pad_1e       = 0
    }
};

#endif /* !M68K */

/*
 * External global variable references
 *
 * These are defined elsewhere in the kernel but needed by PKT functions.
 */

#if !defined(ARCH_M68K)

/* NODE_$ME (0xE245A4) is defined in uid/uid_data.c */

/* Network loopback flag - normally defined in network/network_data.c */
int8_t NETWORK_$LOOPBACK_FLAG = 0;

#endif /* !M68K */
