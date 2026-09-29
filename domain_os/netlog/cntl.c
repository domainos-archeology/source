/*
 * NETLOG_$CNTL - Control network logging
 *
 * This function controls the network logging subsystem:
 *   - cmd=0: Initialize logging (wire pages, allocate buffers, set target)
 *   - cmd=1: Shutdown logging (send pending, free resources)
 *   - cmd=2: Update kinds filter
 *
 * Original address: 0x00E71914
 *
 * Module data through NETLOG_$DATA: Claude Opus 5.5 (source-iq58).
 */

#include "netlog/netlog_internal.h"
#include "netbuf/netbuf.h"
#include "mmap/mmap.h"
#include "mst/mst.h"
#include "wp/wp.h"

/*
 * Pascal by-reference constant cells (see netlog/netlog_internal.h for the
 * hex dump and the pea sites).  MST_$WIRE_AREA takes all five of its
 * arguments by reference, so the two ranges and the page limit have to be
 * addressable objects, not immediates.
 *
 * The four range cells hold VAs of linked objects: the start of NETLOG's own
 * code (NETLOG_$CNTL = NETLOG_$PROC_START) and the NETLOG_$DATA block from
 * its first byte to one past its last (NETLOG_$DATA_START / _END), so they
 * are link-time values (ARCH_PTR_TO_VA_STATIC; the image value is the second
 * argument).  The code-range end, NETLOG_$PROC_END just past
 * NETLOG_$SEND_PAGE, has no linked object to name yet.
 * TODO(source-2e9k): make netlog_$c_wire_code_end a link-time value.
 */
static const uint32_t netlog_$c_wire_code_start =
    ARCH_PTR_TO_VA_STATIC(NETLOG_$CNTL, NETLOG_WIRE_CODE_START_VA);
static const uint32_t netlog_$c_wire_code_end   = NETLOG_WIRE_CODE_END_VA;
static const uint32_t netlog_$c_wire_data_start =
    ARCH_PTR_TO_VA_STATIC(&NETLOG_$DATA, NETLOG_WIRE_DATA_START_VA);
static const uint32_t netlog_$c_wire_data_end   =
    ARCH_PTR_TO_VA_STATIC(&NETLOG_$DATA + 1, NETLOG_WIRE_DATA_END_VA);
static const int16_t  netlog_$c_max_wired_pages = NETLOG_MAX_WIRED_PAGES;

void NETLOG_$CNTL(int16_t *cmd, uint32_t *node, uint16_t *sock,
                  uint32_t *kinds, status_$t *status_ret)
{
    netlog_$data_t *nl = &NETLOG_$DATA;
    int16_t i;
    int16_t wire_count;
    int16_t pages_wired;
    uint32_t ppn_shifted;

    *status_ret = status_$ok;

    /*
     * Command 1: Shutdown logging
     */
    if (*cmd == 1 && nl->initialized < 0) {
        /* Clear initialization flag */
        nl->initialized = 0;
        NETLOG_$OK_TO_LOG = 0;
        NETLOG_$OK_TO_LOG_SERVER = 0;

        /*
         * If there are pending entries in the current buffer, send them
         */
        /* 0xE7195C: tst.w (0x6e,A0) with A0 = A5 + index*2 (Pascal [1..2]) */
        if (nl->page_counts[nl->current_buf_index] > 0) {
            nl->send_page_index = nl->current_buf_index;
            nl->done_cnt++;
            NETLOG_$SEND_PAGE();
        }

        /*
         * Return buffer virtual addresses
         */
        NETBUF_$RTNVA(&nl->buffer_va[1]);
        NETBUF_$RTNVA(&nl->buffer_va[2]);

        /*
         * Free buffer physical pages
         */
        /* 0xE71988/0xE71994: move.l (0x60,A5) / (0x64,A5) */
        MMAP_$FREE(nl->buffer_ppn[1]);
        MMAP_$FREE(nl->buffer_ppn[2]);

        /*
         * Clear ok_to_send flag
         */
        nl->ok_to_send = 0;

        /*
         * Clear page counts
         */
        /* 0xE719A4/0xE719A8: clr.w (0x70,A5) and (0x72,A5) */
        nl->page_counts[1] = 0;
        nl->page_counts[2] = 0;

        /*
         * 0xE719AC..0xE719CA: the loop runs FORWARD from wired_pages[0]
         * (A2 = A5 + 4, read at (0x1c,A2), A2 advanced by 4 each pass), and
         * `subq.w #1 / bmi` skips it entirely when the count is zero.
         */
        for (i = 0; i < nl->wired_page_count; i++) {
            WP_$UNWIRE(nl->wired_pages[i]);
        }
    }

    /*
     * Command 0: Initialize logging
     */
    if (*cmd == 0 && nl->initialized >= 0) {
        /*
         * Wire code and data pages
         * First, wire the NETLOG code section
         */
        /* 0xE719DC..0xE719FA: the NETLOG code range, limit 10 */
        nl->wired_page_count = 0;
        MST_$WIRE_AREA(&netlog_$c_wire_code_start,
                       &netlog_$c_wire_code_end,
                       &nl->wired_pages[0],
                       &netlog_$c_max_wired_pages,
                       &nl->wired_page_count);

        /*
         * 0xE719FE..0xE71A30: the NETLOG data range, appended to the same
         * array with whatever budget is left.  The count comes back in its
         * own word and is added to wired_page_count afterwards.
         */
        wire_count = (int16_t)(NETLOG_MAX_WIRED_PAGES - nl->wired_page_count);
        MST_$WIRE_AREA(&netlog_$c_wire_data_start,
                       &netlog_$c_wire_data_end,
                       &nl->wired_pages[nl->wired_page_count],
                       &wire_count,
                       &pages_wired);
        nl->wired_page_count += pages_wired;

        /*
         * Copy target node, socket, and kinds to globals
         */
        NETLOG_$NODE = *node;
        NETLOG_$SOCK = *sock;
        NETLOG_$KINDS = *kinds;

        /*
         * Allocate two buffer pages for double-buffering
         */
        /* 0xE71A52/0xE71A60: pea (0x60,A5) / (0x64,A5) */
        WP_$CALLOC(&nl->buffer_ppn[1], status_ret);
        WP_$CALLOC(&nl->buffer_ppn[2], status_ret);

        /*
         * Get virtual addresses for the buffers
         * ppn_shifted = ppn << 10 (1KB pages)
         */
        ppn_shifted = nl->buffer_ppn[1] << 10;
        NETBUF_$GETVA(ppn_shifted, &nl->buffer_va[1], status_ret);

        ppn_shifted = nl->buffer_ppn[2] << 10;
        NETBUF_$GETVA(ppn_shifted, &nl->buffer_va[2], status_ret);

        /*
         * Initialize buffer state
         * Start with buffer 1 as current
         */
        nl->current_buf_index = 1;
        nl->done_cnt = 0;
        /* 0xE71AB2/0xE71AB6: clr.w (0x70,A5) / (0x72,A5) */
        nl->page_counts[1] = 0;
        nl->page_counts[2] = 0;
        nl->current_buf_ptr = nl->buffer_va[1];

        /*
         * Initialize packet template
         */
        nl->pkt_type1 = NETLOG_PKT_TYPE1;   /* 99 */
        nl->pkt_type2 = NETLOG_PKT_TYPE2;   /* 1 */
        nl->pkt_done_cnt = 0;

        /*
         * Set flags to indicate ready
         */
        nl->ok_to_send = (int8_t)0xFF;      /* -1 = true */
        nl->initialized = (int8_t)0xFF;     /* -1 = true */
    }

    /*
     * Commands 0 and 2: Update logging flags based on kinds
     */
    if (*cmd == 0 || *cmd == 2) {
        NETLOG_$KINDS = *kinds;

        /*
         * Calculate OK_TO_LOG_SERVER
         * Enabled if bits 20 or 21 are set and initialized
         */
        int8_t server_enabled = 0;
        if ((NETLOG_$KINDS & 0x100000) != 0 || (NETLOG_$KINDS & 0x200000) != 0) {
            server_enabled = (int8_t)0xFF;
        }
        NETLOG_$OK_TO_LOG_SERVER = nl->initialized & server_enabled;

        /*
         * Calculate OK_TO_LOG
         * Enabled if any non-server bits are set and initialized
         * Mask out bits 20 and 21 (0xFFCFFFFF)
         */
        int8_t general_enabled = 0;
        if ((NETLOG_$KINDS & 0xFFCFFFFF) != 0) {
            general_enabled = (int8_t)0xFF;
        }
        NETLOG_$OK_TO_LOG = nl->initialized & general_enabled;
    }
}
