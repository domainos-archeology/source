/*
 * Ring log module global data
 *
 * This file defines the global data structures used by the ring log module.
 * These correspond to memory-mapped data at specific addresses on the
 * original m68k platform.
 */

#include "ring/ringlog_internal.h"

/*
 * ============================================================================
 * Global Data Structures
 * ============================================================================
 */

/*
 * Ring log control structure.
 * Located at 0xE2C32C on original platform.
 *
 * Initial values set based on the memory dump at 0xE2C32C:
 * - wired_pages: all zeros
 * - spinlock: 0
 * - filter_id: 0
 * - wire_count: 0
 * - mbx_sock_filter: -1 (0xFF) - filtering disabled
 * - who_sock_filter: -1 (0xFF) - filtering disabled
 * - nil_sock_filter: -1 (0xFF) - filtering disabled
 * - logging_active: 0 - logging not active
 * - first_entry_flag: -1 (0xFF) - need to reset index on first entry
 */
ringlog_ctl_t RINGLOG_$CTL = {
    .wired_pages = {0},
    .spinlock = 0,
    .filter_id = 0,
    .wire_count = 0,
    .mbx_sock_filter = -1,      /* 0xFF = don't filter */
    ._pad1 = 0,
    .who_sock_filter = -1,      /* 0xFF = don't filter */
    ._pad2 = 0,
    .nil_sock_filter = -1,      /* 0xFF = don't filter */
    ._pad3 = 0,
    .logging_active = 0,        /* logging not active initially */
    ._pad4 = 0,
    .first_entry_flag = -1,     /* reset index on first entry */
};

/*
 * Ring log buffer.
 * Located at 0xEA3E38 on original platform.
 *
 * Contains the current entry index followed by 100 log entries.
 * Initially empty (index 0, all entries zeroed).
 */
ringlog_buffer_t RINGLOG_$BUF = {
    .current_index = 0,
    .entries = {{0}},
};
