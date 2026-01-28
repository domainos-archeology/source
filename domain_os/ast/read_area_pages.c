/*
 * ast_$read_area_pages - Read pages from disk for area objects
 *
 * Allocates pages and reads them from disk using multi-block I/O.
 * Used for area objects which have contiguous disk allocation.
 *
 * Parameters:
 *   aste - ASTE pointer
 *   segmap - Segment map entries with disk addresses
 *   ppn_array - Output array for allocated PPNs
 *   start_page - Starting page number in segment
 *   count - Number of pages to read
 *   status - Output status
 *
 * Returns: Number of pages successfully read
 *
 * Original address: 0x00e02af6
 */

#include "ast/ast_internal.h"

/* PROC1_$CURRENT from proc1.h via ast_internal.h */

/* Process page read statistics at A5+0x4A0 relative to process table */
#if defined(ARCH_M68K)
#define PROC_PAGE_STATS    ((int32_t *)0xE25D18)
#else
#define PROC_PAGE_STATS    proc_page_stats
#endif

int16_t ast_$read_area_pages(aste_t *aste, uint32_t *segmap, uint32_t *ppn_array,
                              uint16_t start_page, uint16_t count,
                              status_$t *status)
{
    aote_t *aote;
    int32_t qblk_head;
    uint32_t qblk_tail;
    int16_t pages_read;
    int16_t allocated;
    uint16_t vol_idx;
    uint32_t page_num;
    int32_t qblk;
    int16_t i;

    aote = *((aote_t **)((char *)aste + 0x04));

    /* Allocate pages - count_flags = (count << 16) | flags */
    allocated = ast_$allocate_pages(((uint32_t)count << 16) | 1, ppn_array);

    ML_$UNLOCK(PMAP_LOCK_ID);

    /* Get volume index */
    vol_idx = *((uint8_t *)((char *)aote + 0xB8));

    /* Get queue blocks for disk I/O */
    DISK_$GET_QBLKS(allocated, &qblk_head, &qblk_tail);

    /* Set up page number (segment * 32 + start_page) */
    page_num = (uint32_t)*((uint16_t *)((char *)aste + 0x0C)) * 32 + start_page;

    qblk = qblk_head;

    /* Fill in QBLK header */
    *((uint32_t *)(qblk + 0x28)) = page_num;
    *((uint32_t *)(qblk + 0x20)) = *((uint32_t *)((char *)aote + 0x10));  /* UID high */
    *((uint32_t *)(qblk + 0x24)) = *((uint32_t *)((char *)aote + 0x14));  /* UID low */
    *((uint8_t *)(qblk + 0x30)) = 0;

    /* Fill in each QBLK with PPN and disk address */
    for (i = allocated - 1; i >= 0; i--) {
        *((uint32_t *)(qblk + 0x14)) = ppn_array[allocated - 1 - i];
        *((uint32_t *)(qblk + 0x04)) = *segmap++ & 0x3FFFFF;  /* Disk address */
        qblk = *((int32_t *)(qblk + 0x08));  /* Next QBLK */
    }

    /* Perform disk read */
    DISK_$READ_MULTI(vol_idx, -1, -1, qblk_head, qblk_tail, &pages_read, status);

    if (*status != status_$ok) {
        *((uint8_t *)status) |= 0x80;  /* Set error flag */
    }

    ML_$LOCK(PMAP_LOCK_ID);

    /* Return queue blocks */
    DISK_$RTN_QBLKS(allocated, qblk_head, qblk_tail);

    /* Free any pages that weren't successfully read */
    if (allocated != pages_read) {
        for (i = allocated - pages_read - 1; i >= 0; i--) {
            MMAP_$FREE(ppn_array[pages_read + 1 + i]);
        }
    }

    /* Update process page statistics */
    PROC_PAGE_STATS[PROC1_$CURRENT] += pages_read;

    return pages_read;
}
