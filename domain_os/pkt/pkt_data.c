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

#if !defined(M68K)

/*
 * PKT module global data
 *
 * This structure contains all the global state for the PKT subsystem:
 * - Missing node tracking for node visibility
 * - Spin lock for thread-safe ID generation
 * - Packet ID counters (short and long)
 * - Ping request configuration
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
    .pad_5a = 0,

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

#if !defined(M68K)

/* Local node ID - normally defined in network/network_data.c */
uint32_t NODE_$ME = 0;

/* Network loopback flag - normally defined in network/network_data.c */
int8_t NETWORK_$LOOPBACK_FLAG = 0;

/* Route port pointers - normally defined in route/route_data.c */
uint32_t *ROUTE_$PORTP = NULL;

#endif /* !M68K */
