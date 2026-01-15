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

/* Logging function stub */
static void FUN_00e02c52(int16_t count, int8_t zero_flag);

/* Process page read statistics - PROC1_$CURRENT from proc1.h via ast_internal.h */
#if defined(M68K)
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
    uid_t uid;
    int32_t page_num;
    int32_t dtm;
    clock_t clock;
    uint32_t acl_info;
    uint32_t buffer[2];
    int16_t i;

    aote = *((aote_t **)((char *)aste + 0x04));

    /* Allocate pages - count_flags = (count << 16) | flags */
    allocated = ast_$allocate_pages(((uint32_t)count << 16) | 1, ppn_array);

    /* Check and clear read-ahead disable flag */
    aote_flags = *((uint16_t *)((char *)aote + 0xBE));
    no_read_ahead = (aote_flags & 0x10) ? -1 : 0;
    *((uint8_t *)((char *)aote + 0xBF)) &= ~0x10;  /* Clear flag */

    ML_$UNLOCK(PMAP_LOCK_ID);

    /* Return network buffers for allocated pages */
    for (i = allocated - 1; i >= 0; i--) {
        NETBUF_$RTN_DAT(ppn_array[i] << 10);
    }

    /* Set up UID and page number */
    uid.high = *((uint32_t *)((char *)aote + 0x10));
    uid.low = *((uint32_t *)((char *)aote + 0x14));
    page_num = start_page + (uint32_t)*((uint16_t *)((char *)aste + 0x0C)) * 32;

    /* Calculate page size from AOTE (2^(9 + (byte at 0x9D & 0xF))) */
    page_size_shift = *((uint8_t *)((char *)aote + 0x9D)) & 0x0F;
    page_size = 1 << (page_size_shift + 9);

    /* Perform network read-ahead */
    pages_read = NETWORK_$READ_AHEAD((char *)aote + 0xAC, &uid, ppn_array,
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
    if (dtm == 0) {
        TIME_$CLOCK(&clock);
    } else {
        *((int32_t *)((char *)aote + 0x30)) = dtm;
        *((uint16_t *)((char *)aote + 0x34)) = *((uint16_t *)&acl_info);
        *((uint32_t *)((char *)aote + 0x28)) = *((uint32_t *)&acl_info + 1);
        *((uint16_t *)((char *)aote + 0x2C)) = *((uint16_t *)((char *)&acl_info + 2));
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
            if (dtm == 0) {
                *((uint32_t *)((char *)aote + 0x40)) = clock.high;
                *((uint16_t *)((char *)aote + 0x44)) = clock.low;
                *((uint32_t *)((char *)aote + 0x28)) = clock.high;
                *((uint16_t *)((char *)aote + 0x2C)) = clock.low;
            }
        } else {
            /* Not extending */
            if (dtm == 0) {
                *((uint32_t *)((char *)aote + 0x30)) = clock.high;
                *((uint16_t *)((char *)aote + 0x34)) = clock.low;
            }
        }
    }

    /* Log if enabled */
    if (NETLOG_$OK_TO_LOG < 0) {
        FUN_00e02c52(pages_read, zero_flag);
    }

done:
    /* Update process network statistics */
    PROC_NET_STATS[PROC1_$CURRENT] += pages_read;

    return pages_read;
}

/* Stub for logging function */
static void FUN_00e02c52(int16_t count, int8_t zero_flag)
{
    /* TODO: Implement network logging */
    (void)count;
    (void)zero_flag;
}
