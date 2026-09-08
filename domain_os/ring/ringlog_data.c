/*
 * Ring log module global data
 *
 * RINGLOG_ has two objects in the SAU2 map:
 *   D    E2C32C  RINGLOG_          size = 3C    the control block
 *   D53  EA3E38  RINGLOG_$DATA     size = 11FC  the circular buffer
 */

#include "ring/ring_internal.h"
#include "ring/ringlog_internal.h"

/*
 * Ring log control structure, 0x00E2C32C, 0x3C bytes.
 *
 * Initial values are the image's:
 *   wired_pages / spinlock / filter_id / wire_count  zero
 *   mbx_sock_filter, who_sock_filter, nil_sock_filter  -1 (no filtering)
 *   logging_active   0
 *   first_entry_flag -1 (reset the index before the first entry)
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
 * RINGLOG_$DATA, 0x00EA3E38, 0x11FC bytes: the next-entry index word followed
 * by 100 entries of 0x2E bytes that START AT OFFSET ZERO, so entry 0's first
 * word IS the index.  Kept as a byte pool because the 0x2E stride is an
 * explicit constant in the code (see ring/ringlog_internal.h).
 */
ringlog_$data_t RINGLOG_$DATA;
