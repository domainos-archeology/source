/*
 * DBUF_$INIT - Initialize the disk buffer subsystem
 *
 * Allocates and initializes the buffer pool based on available memory.
 * Sets up the LRU linked list and maps physical pages for buffer data.
 *
 * Original address: 0x00e3abda
 */

#include "dbuf/dbuf_internal.h"

/*
 * Global data declarations
 *
 * These would normally be defined in a data file or linker script.
 * For now, we declare them as extern and let the linker resolve them.
 */

/*
 * DBUF_$INIT
 *
 * Initialization steps:
 *   1. Calculate number of buffers based on available memory
 *   2. Allocate physical pages for each buffer
 *   3. Map buffer pages into kernel virtual address space
 *   4. Initialize LRU linked list
 *   5. Initialize event count for buffer waiters
 */
void DBUF_$INIT(void)
{
    uint16_t num_buffers;
    uint16_t i;
    dbuf_$entry_t *entry;
    dbuf_$entry_t *prev_entry;
    uint32_t va;
    uint32_t ppn;
    status_$t status;

    /*
     * Calculate number of buffers based on available memory.
     * Formula: (real_pages / 1024) * 16
     * This gives roughly 1 buffer per 64KB of memory.
     */
    num_buffers = (uint16_t)((MMAP_$REAL_PAGES >> 10) << 4);

    /* Clamp to [6, 64] range */
    if (num_buffers < DBUF_MIN_BUFFERS) {
        num_buffers = DBUF_MIN_BUFFERS;
    } else if (num_buffers > DBUF_MAX_BUFFERS) {
        num_buffers = DBUF_MAX_BUFFERS;
    }

    dbuf_$count = num_buffers;

    /*
     * Initialize buffer entries and allocate physical pages.
     * Build the doubly-linked LRU list as we go.
     */
    entry = &DBUF;
    prev_entry = NULL;
    va = DBUF_VA_BASE;

    for (i = 0; i < num_buffers; i++) {
        /* Set up linked list pointers */
        entry->next = (dbuf_$entry_t *)((char *)entry + DBUF_ENTRY_SIZE);
        entry->prev = prev_entry;

        /* Set buffer data pointer (virtual address) */
        entry->data = (void *)va;

        /* Clear flags */
        entry->flags &= ~DBUF_ENTRY_BUSY;
        entry->flags &= ~DBUF_ENTRY_DIRTY;
        entry->flags &= ~0x30;  /* Clear reserved bits */
        entry->flags &= ~DBUF_ENTRY_VOL_MASK;

        /* Clear type and ref count */
        entry->type = 0;
        entry->ref_count = 0;

        /* Allocate physical page for buffer data */
        WP_$CALLOC(&ppn, &status);
        if (status != status_$ok) {
            CRASH_SYSTEM(&status);
            return;  /* Not reached */
        }

        /* Map physical page into kernel address space */
        MMU_$INSTALL(ppn, va, 0x16);  /* 0x16 = kernel read/write, valid */

        /* Mark page as cache-inhibited for I/O consistency */
        MMU_$CACHE_INHIBIT_VA(va);

        /* Store physical page number */
        entry->ppn = ppn;

        /* Mark block as invalid */
        entry->block = -1;

        /* Clear UID */
        entry->uid.high = UID_$NIL.high;
        entry->uid.low = UID_$NIL.low;

        /* Clear hint */
        entry->hint = 0;

        /* Move to next entry */
        prev_entry = entry;
        entry = entry->next;
        va += DBUF_BUFFER_SIZE;
    }

    /* Terminate the last entry's next pointer */
    prev_entry->next = NULL;

    /* Clear the word after the last entry (from original code) */
    /* This terminates some related data structure */
    *(uint32_t *)((char *)&DBUF + dbuf_$count * DBUF_ENTRY_SIZE + 4) = 0;

    /* Set head of LRU list to first buffer */
    dbuf_$head = &DBUF;

    /* Initialize event count for buffer waiters */
    EC_$INIT(&dbuf_$eventcount);

    /* Clear waiter count */
    dbuf_$waiters = 0;

    /* Clear per-volume trouble flags */
    DBUF_$TROUBLE = 0;
}
