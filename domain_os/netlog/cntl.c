/*
 * NETLOG_$CNTL - Control network logging
 *
 * This function controls the network logging subsystem:
 *   - cmd=0: Initialize logging (wire pages, allocate buffers, set target)
 *   - cmd=1: Shutdown logging (send pending, free resources)
 *   - cmd=2: Update kinds filter
 *
 * Original address: 0x00E71914
 */

#include "netlog/netlog_internal.h"
#include "netbuf/netbuf.h"
#include "mmap/mmap.h"
#include "mst/mst.h"
#include "wp/wp.h"

/*
 * External references to code/data boundaries for wiring
 * These are defined in the linker script and point to the NETLOG/AUDIT
 * code and data sections that need to be wired in memory.
 *
 * Note: On m68k, these would be actual linker-defined symbols.
 * For portability, we define placeholder addresses here.
 */
#if defined(ARCH_M68K)
    /* Code boundary addresses for MST_$WIRE_AREA */
    #define NETLOG_CODE_START       ((void*)0xE71914)   /* Start of NETLOG code */
    #define NETLOG_DATA_START       ((void*)0xE85684)   /* Start of NETLOG data */
    #define NETLOG_DATA_END_ADDR    ((void*)0xE85800)   /* End of NETLOG data */
    #define AUDIT_DATA_END_ADDR     ((void*)0xE248FC)   /* End of AUDIT data */
#else
    extern char NETLOG_CODE_START_SYM;
    extern char NETLOG_DATA_START_SYM;
    extern char NETLOG_DATA_END_SYM;
    extern char AUDIT_DATA_END_SYM;
    #define NETLOG_CODE_START       (&NETLOG_CODE_START_SYM)
    #define NETLOG_DATA_START       (&NETLOG_DATA_START_SYM)
    #define NETLOG_DATA_END_ADDR    (&NETLOG_DATA_END_SYM)
    #define AUDIT_DATA_END_ADDR     (&AUDIT_DATA_END_SYM)
#endif

void NETLOG_$CNTL(int16_t *cmd, uint32_t *node, uint16_t *sock,
                  uint32_t *kinds, status_$t *status_ret)
{
    netlog_data_t *nl = NETLOG_DATA;
    int16_t i;
    int16_t wire_count;
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
        MMAP_$FREE(nl->buffer_ppn[0]);
        MMAP_$FREE(nl->buffer_ppn[1]);

        /*
         * Clear ok_to_send flag
         */
        nl->ok_to_send = 0;

        /*
         * Clear page counts
         */
        nl->page_counts[1] = 0;
        nl->page_counts[2] = 0;

        /*
         * Unwire the previously wired pages
         */
        for (i = nl->wired_page_count - 1; i >= 0; i--) {
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
        nl->wired_page_count = 0;
        MST_$WIRE_AREA(NETLOG_CODE_START, NETLOG_DATA_START,
                       &nl->wire_area_data[0],
                       (void *)(NETLOG_MAX_WIRED_PAGES - nl->wired_page_count),
                       &nl->wired_page_count);

        /*
         * Wire the AUDIT data section (continuation of wire area)
         */
        wire_count = NETLOG_MAX_WIRED_PAGES - nl->wired_page_count;
        MST_$WIRE_AREA(AUDIT_DATA_END_ADDR, NETLOG_DATA_END_ADDR,
                       &nl->wire_area_data[nl->wired_page_count],
                       &wire_count,
                       &wire_count);
        nl->wired_page_count += wire_count;

        /*
         * Copy target node, socket, and kinds to globals
         */
        NETLOG_$NODE = *node;
        NETLOG_$SOCK = *sock;
        NETLOG_$KINDS = *kinds;

        /*
         * Allocate two buffer pages for double-buffering
         */
        WP_$CALLOC(&nl->buffer_ppn[0], status_ret);
        WP_$CALLOC(&nl->buffer_ppn[1], status_ret);

        /*
         * Get virtual addresses for the buffers
         * ppn_shifted = ppn << 10 (1KB pages)
         */
        ppn_shifted = nl->buffer_ppn[0] << 10;
        NETBUF_$GETVA(ppn_shifted, &nl->buffer_va[1], status_ret);

        ppn_shifted = nl->buffer_ppn[1] << 10;
        NETBUF_$GETVA(ppn_shifted, &nl->buffer_va[2], status_ret);

        /*
         * Initialize buffer state
         * Start with buffer 1 as current
         */
        nl->current_buf_index = 1;
        nl->done_cnt = 0;
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
