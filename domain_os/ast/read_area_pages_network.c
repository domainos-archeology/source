/*
 * ast_$read_area_pages_network - Read pages from network for remote objects
 *
 * Allocates pages and reads them from the network using read-ahead.
 * Used for remote objects accessed via the network file system.
 *
 * Parameters:
 *   aste - ASTE pointer
 *   segmap - Segment map entries to update
 *   ppn_array - Output array for allocated PPNs
 *   start_page - Starting page number in segment
 *   count - Number of pages to read
 *   zero_pages - Flag to zero pages before use
 *   status - Output status
 *
 * Returns: Number of pages successfully read
 *
 * Original address: 0x00e02ca6
 */

#include "ast/ast_internal.h"

/*
 * ast_$read_area_pages_net_log - the nested NETLOG helper at 0x00E02C52
 *                                (84 bytes)
 *
 * Two stack arguments of its own -- a word count at (0x8,A6) and a boolean
 * byte at (0xa,A6) -- plus two of this function's arguments reached through
 * the static link (`movea.l (A6),A2` at 0x00E02C5E):
 *
 *   00e02c5a  move.b (0xa,A6),D0b / bpl.b 0x00e02c6a
 *   00e02c62  move.w #0x8,(-0xa,A6)      ; zero flag TRUE  -> kind 8
 *   00e02c6a  move.w #0x9,(-0xa,A6)      ; otherwise       -> kind 9
 *   00e02c72  clr.w -(SP)                ; p8 = 0
 *   00e02c74  move.w (0x8,A6),-(SP)      ; p7 = count
 *   00e02c78  clr.l -(SP)                ; p5 = p6 = 0
 *   00e02c7a  move.w (0x14,A2),-(SP)     ; p4 = the parent's start_page
 *   00e02c82  move.w (0xc,A0),-(SP)      ; p3 = aste->timestamp
 *   00e02c8e  pea (0x10,A4)              ; p2 = &aote->uid
 *   00e02c92  move.w (-0xa,A6),-(SP)     ; p1 = the kind chosen above
 *   00e02c96  jsr 0x00e71b38.l           ; NETLOG_$LOG_IT
 */
static void ast_$read_area_pages_net_log(int16_t count, int8_t zero_flag,
                                         const aste_t *aste,
                                         uint16_t start_page);

/* Process page read statistics - PROC1_$CURRENT from proc1.h via ast_internal.h */
#if defined(ARCH_M68K)
#define PROC_NET_STATS     ((int32_t *)0xE25D1C)
#else
#define PROC_NET_STATS     proc_net_stats
#endif


int16_t ast_$read_area_pages_network(aste_t *aste, uint32_t *segmap,
                                      uint32_t *ppn_array, uint16_t start_page,
                                      uint16_t count, uint8_t flags,
                                      status_$t *status)
{
    aote_t *aote;
    int16_t allocated;
    int16_t pages_read;
    uint16_t aote_flags;
    uint8_t page_size_shift;
    uint16_t page_size;
    int8_t no_read_ahead;
    int8_t zero_flag;
    /*
     * The frame slot at A6-0x28 is a 32-byte request record, not a bare UID:
     * the caller fills only the UID (0xE02D20) and the page number
     * (0xE02D3C) and NETWORK_$READ_AHEAD copies all eight longwords into
     * its own frame at 0xE0FC8A and copies eight back at 0xE0FF72.  The
     * remaining 20 bytes are left uninitialised by the original; leaving
     * this local uninitialised reproduces that.
     */
    network_$page_request_t request;
    /*
     * dtm (-0x38), clock (-0x40) and acl_info (-0x30) are each 6-byte
     * {high, low} timestamps filled in by NETWORK_$READ_AHEAD, which writes
     * a longword at +0 and a word at +4 to all three (0xE0FF48-0xE0FF6C).
     */
    clock_t dtm;
    clock_t clock;
    clock_t acl_info;
    uint32_t buffer[2];
    int16_t i;

    aote = *((aote_t **)((char *)aste + 0x04));

    /* Allocate pages - count_flags = (count << 16) | flags */
    allocated = ast_$allocate_pages(count, 1, ppn_array);

    /* Check and clear read-ahead disable flag */
    aote_flags = *((uint16_t *)((char *)aote + 0xBE));
    no_read_ahead = (aote_flags & 0x10) ? -1 : 0;
    *((uint8_t *)((char *)aote + 0xBF)) &= ~0x10;  /* Clear flag */

    ML_$UNLOCK(PMAP_LOCK_ID);

    /* Return network buffers for allocated pages */
    for (i = allocated - 1; i >= 0; i--) {
        NETBUF_$RTN_DAT(ppn_array[i] << 10);
    }

    /* Set up the request record: UID (0xE02D20) and page number (0xE02D3C) */
    request.uid.high = *((uint32_t *)((char *)aote + 0x10));
    request.uid.low = *((uint32_t *)((char *)aote + 0x14));
    request.page_num =
        start_page + (uint32_t)*((uint16_t *)((char *)aste + 0x0C)) * 32;

    /* Calculate page size from AOTE (2^(9 + (byte at 0x9D & 0xF))) */
    page_size_shift = *((uint8_t *)((char *)aote + 0x9D)) & 0x0F;
    page_size = 1 << (page_size_shift + 9);

    /* Perform network read-ahead */
    pages_read = NETWORK_$READ_AHEAD((char *)aote + 0xAC, &request, ppn_array,
                                      page_size, allocated, no_read_ahead,
                                      flags, &dtm, &clock, &acl_info, status);

    /* Free excess pages that weren't used */
    for (i = allocated - pages_read - 1; i >= 0; i--) {
        NETBUF_$GET_DAT(buffer);
        MMAP_$FREE(buffer[0] >> 10);
    }

    if (pages_read < 1) {
        ML_$LOCK(PMAP_LOCK_ID);
        goto done;
    }

    *status = status_$ok;

    /* Check if first page is NULL (needs zeroing) */
    zero_flag = (*ppn_array == 0) ? -1 : 0;

    /* Get pages from network buffers if zeroing needed */
    if (zero_flag < 0) {
        for (i = pages_read - 1; i >= 0; i--) {
            NETBUF_$GET_DAT(buffer);
            ppn_array[pages_read - 1 - i] = buffer[0] >> 10;
        }
    }

    ML_$LOCK(PMAP_LOCK_ID);

    /* Update segment map entries */
    for (i = pages_read - 1; i >= 0; i--) {
        if (zero_flag < 0) {
            ZERO_PAGE(*ppn_array);
            *((uint8_t *)((char *)segmap + 1)) |= 0x40;  /* Set COW */
        }
        *segmap &= 0xFFC00000;  /* Clear address/flags */
        *segmap |= 1;           /* Set installed flag */
        segmap++;
        ppn_array++;
    }

    /* Update timestamps */
    if (dtm.high == 0) {   /* tst.l (-0x38,A6) */
        TIME_$CLOCK(&clock);
    } else {
        *((uint32_t *)((char *)aote + 0x30)) = dtm.high;       /* move.l (-0x38,A6),(0x30,A3) */
        *((uint16_t *)((char *)aote + 0x34)) = dtm.low;        /* move.w (-0x34,A6),(0x34,A3) */
        *((uint32_t *)((char *)aote + 0x28)) = acl_info.high;  /* move.l (-0x30,A6),(0x28,A3) */
        *((uint16_t *)((char *)aote + 0x2C)) = acl_info.low;   /* move.w (-0x2c,A6),(0x2c,A3) */
        *((uint32_t *)((char *)aote + 0x40)) = clock.high;
        *((uint16_t *)((char *)aote + 0x44)) = clock.low;
    }

    /* Update file size if needed */
    {
        int32_t end_offset = ((uint32_t)*((uint16_t *)((char *)aste + 0x0C)) * 32 +
                              pages_read + start_page - 1) * 0x400;

        if (end_offset >= *((int32_t *)((char *)aote + 0x20))) {
            /* Extending file */
            *((int32_t *)((char *)aote + 0x20)) = end_offset + 0x400;
            if (dtm.high == 0) {
                *((uint32_t *)((char *)aote + 0x40)) = clock.high;
                *((uint16_t *)((char *)aote + 0x44)) = clock.low;
                *((uint32_t *)((char *)aote + 0x28)) = clock.high;
                *((uint16_t *)((char *)aote + 0x2C)) = clock.low;
            }
        } else {
            /* Not extending */
            if (dtm.high == 0) {
                *((uint32_t *)((char *)aote + 0x30)) = clock.high;
                *((uint16_t *)((char *)aote + 0x34)) = clock.low;
            }
        }
    }

    /* Log if enabled */
    if (NETLOG_$OK_TO_LOG < 0) {
        ast_$read_area_pages_net_log(pages_read, zero_flag, aste, start_page);
    }

done:
    /* Update process network statistics */
    PROC_NET_STATS[PROC1_$CURRENT] += pages_read;

    return pages_read;
}

static void ast_$read_area_pages_net_log(int16_t count, int8_t zero_flag,
                                         const aste_t *aste,
                                         uint16_t start_page)
{
    /* 0x00E02C5A: a Domain boolean -- TRUE is negative */
    uint16_t kind = (zero_flag < 0) ? 8 : 9;

    /* 0x00E02C96 */
    NETLOG_$LOG_IT(kind, &aste->aote->uid.high,
                   aste->timestamp,
                   start_page,
                   0, 0,
                   (uint16_t)count,
                   0);
}
