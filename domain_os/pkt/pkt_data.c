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
 * TODO(source-wv5s): these initialisers do not match the loaded image at
 * 0x00E24C9C (short_id and long_id are 0 there, visibility_seq is 1, and the
 * ping template reads 0x0010 0x0002 0x0002 0x8031 0xFFFF ...).
 */
pkt_$data_t PKT_$DATA_STRUCT = {
    /* Missing node tracking - all zeroed initially */
    .missing_nodes = { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
                       { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },

    /* Spin lock */
    .spin_lock = 0,

    /* Visibility sequence counter */
    .visibility_seq = 0,

    /* Count of missing nodes */
    .n_missing = 0,

    /* 2-byte ping request header; image value at 0x00E24CF6 is 0x0001 */
    .ping_req_hdr = 1,

    /* Short packet ID counter - starts at 1 */
    .short_id = 1,
    .pad_5e = 0,

    /* Long packet ID counter - starts at 1 */
    .long_id = 1,

    /* Default send flags */
    .default_flags = 0,
    .pad_66 = 0,

    /* Ping request template */
    .ping_template = {
        .type = 2,
        .length = 0,
        .id = 0,
        .flags = 0,
        .protocol = 0,
        .retry_count = 0,
        .pad_0a = 0,
        .field_0c = 0
    },

    /* Ping server flags */
    .ping_server_flags = 0
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
