/*
 * DBUF Internal Header
 *
 * Internal data structures and declarations for the Disk Buffer subsystem.
 * This header should only be included by DBUF implementation files.
 *
 * Memory Layout (base: 0xE78B58):
 *   +0x000: ec_$eventcount_t (event count for buffer availability)
 *   +0x010: dbuf_$entry_t[] (buffer entry array, 0x24 bytes each)
 *   +0x910: DBUF_SPIN_LOCK (spin lock for buffer pool)
 *   +0x914: dbuf_$head (pointer to LRU list head)
 *   +0x918: dbuf_$waiters (count of threads waiting for buffers)
 *   +0x91A: dbuf_$count (number of buffers in pool)
 *   +0x91C: DBUF_$TROUBLE (per-volume trouble flags)
 *
 * Buffer Virtual Addresses:
 *   Start: 0xD50400
 *   Each buffer: 0x400 (1024) bytes
 */

#ifndef DBUF_INTERNAL_H
#define DBUF_INTERNAL_H

#include "dbuf/dbuf.h"  /* Include public header for DBUF_FLAG_* constants */
#include "ml/ml.h"
#include "ec/ec.h"
#include "mmu/mmu.h"
#include "wp/wp.h"
#include "mmap/mmap.h"
#include "misc/crash_system.h"

/* Forward declare DISK functions to avoid circular include */
void DISK_$READ(int16_t vol_idx, void *buffer, void *daddr, void *count,
                status_$t *status);
void DISK_$WRITE(int16_t vol_idx, void *buffer, void *daddr, void *count,
                 status_$t *status);

/*
 * Buffer pool limits
 */
#define DBUF_MIN_BUFFERS        6       /* Minimum number of buffers */
#define DBUF_MAX_BUFFERS        64      /* Maximum number of buffers (0x40) */
#define DBUF_BUFFER_SIZE        0x400   /* 1024 bytes per buffer */

/*
 * Buffer entry size
 */
#define DBUF_ENTRY_SIZE         0x24    /* 36 bytes per entry */

/*
 * Buffer virtual address base
 * Buffers are mapped starting at 0xD50400
 */
#define DBUF_VA_BASE            0xD50400

/*
 * Buffer entry flags (stored in flags byte at offset +0x0C)
 *
 * Bit layout:
 *   7: busy (buffer being read/written)
 *   6: dirty (needs writeback)
 *   5: reserved
 *   4: reserved
 *   3-0: volume index
 */
#define DBUF_ENTRY_BUSY         0x80    /* Buffer I/O in progress */
#define DBUF_ENTRY_DIRTY        0x40    /* Buffer needs writeback */
#define DBUF_ENTRY_VOL_MASK     0x0F    /* Volume index mask */

/*
 * Additional flags in high byte (0x0D position):
 *   Bit 14 (0x4000 in word at +0x0C): buffer valid/has data
 */
#define DBUF_ENTRY_VALID        0x4000  /* Buffer contains valid data */

/*
 * Buffer entry structure (0x24 = 36 bytes)
 *
 * Forms a doubly-linked LRU list. Most recently used buffers
 * are at the head; least recently used at the tail.
 */
typedef struct dbuf_$entry_t {
    struct dbuf_$entry_t *next;     /* 0x00: Next entry in LRU list */
    struct dbuf_$entry_t *prev;     /* 0x04: Previous entry in LRU list */
    void        *data;              /* 0x08: Pointer to buffer data (VA) */
    uint8_t     flags;              /* 0x0C: Flags (busy, dirty, vol_idx) */
    uint8_t     type;               /* 0x0D: Buffer type/flags */
    uint16_t    ref_count;          /* 0x0E: Reference count */
    uint32_t    ppn;                /* 0x10: Physical page number */
    int32_t     block;              /* 0x14: Disk block number (-1 = invalid) */
    uid_t       uid;                /* 0x18: UID for validation */
    uint32_t    hint;               /* 0x20: Block hint/type */
} dbuf_$entry_t;

/*
 * DBUF global data structure
 *
 * Located at 0xE78B58
 */
typedef struct dbuf_$data_t {
    ec_$eventcount_t eventcount;     /* 0x000: Event count for waiters */
    uint8_t     reserved_08[8];     /* 0x008: Reserved */
    dbuf_$entry_t entries[DBUF_MAX_BUFFERS]; /* 0x010: Buffer entries */
    /* Note: actual size depends on dbuf_$count */
    /* After entries array: */
    /* +0x910: spin_lock */
    /* +0x914: head pointer */
    /* +0x918: waiters */
    /* +0x91A: count */
    /* +0x91C: trouble */
} dbuf_$data_t;

/*
 * External references to global data
 */

/* DBUF spin lock for buffer pool protection */
extern uint32_t DBUF_SPIN_LOCK;     /* 0xE78E68 (base + 0x910) */

/* Head of LRU buffer list */
extern dbuf_$entry_t *dbuf_$head;   /* 0xE78E6C (base + 0x914) */

/* Number of threads waiting for buffers */
extern uint16_t dbuf_$waiters;      /* 0xE78E70 (base + 0x918) */

/* Number of buffers in pool */
extern uint16_t dbuf_$count;        /* 0xE78E72 (base + 0x91A) */

/* Per-volume trouble flags (bit N = volume N has trouble) */
extern uint16_t DBUF_$TROUBLE;      /* 0xE78E74 (base + 0x91C) */

/* Event count for buffer availability */
extern ec_$eventcount_t dbuf_$eventcount; /* 0xE78B58 */

/* First buffer entry */
extern dbuf_$entry_t DBUF;          /* 0xE78B68 (base + 0x10) */

/* Number of real memory pages (from MMAP) */
extern uint32_t MMAP_$REAL_PAGES;   /* 0xE23CA0 */

/* NIL UID constant (declared in base/base.h) */

/*
 * Status codes
 */
#define status_$storage_module_stopped  0x8001B

/*
 * Helper macros
 */

/* Get volume index from buffer entry */
#define DBUF_GET_VOL(entry)     ((entry)->flags & DBUF_ENTRY_VOL_MASK)

/* Set volume index in buffer entry */
#define DBUF_SET_VOL(entry, vol) \
    ((entry)->flags = ((entry)->flags & ~DBUF_ENTRY_VOL_MASK) | ((vol) & DBUF_ENTRY_VOL_MASK))

/* Check if buffer is busy */
#define DBUF_IS_BUSY(entry)     (((entry)->flags & DBUF_ENTRY_BUSY) != 0)

/* Check if buffer is dirty */
#define DBUF_IS_DIRTY(entry)    (((entry)->flags & DBUF_ENTRY_DIRTY) != 0)

/* Check if buffer has valid data (using word access for bit 14) */
#define DBUF_IS_VALID(entry)    ((*(uint16_t *)&(entry)->flags & DBUF_ENTRY_VALID) != 0)

/* Get buffer data pointer from entry */
#define DBUF_DATA(entry)        ((entry)->data)

/*
 * Error status codes for CRASH_SYSTEM
 * These are status values that cause system crash with diagnostic info.
 */
extern const status_$t OS_DBUF_bad_ptr_err;     /* Bad buffer pointer in SET_BUFF */
extern const status_$t OS_DBUF_bad_free_err;    /* Bad free (ref count already 0) */

/*
 * Internal helper functions
 */

/* Move entry to head of LRU list (most recently used) */
static inline void dbuf_$move_to_head(dbuf_$entry_t *entry)
{
    /* Remove from current position */
    if (entry->prev != NULL) {
        entry->prev->next = entry->next;
    }
    if (entry->next != NULL) {
        entry->next->prev = entry->prev;
    }

    /* Insert at head */
    entry->prev = NULL;
    entry->next = dbuf_$head;
    if (dbuf_$head != NULL) {
        dbuf_$head->prev = entry;
    }
    dbuf_$head = entry;
}

/*
 * Disk write helper structure
 *
 * Contains the parameters needed for DISK_$WRITE when flushing a buffer.
 * Matches the layout expected by DISK_$WRITE.
 */
typedef struct dbuf_$write_params_t {
    uid_t       uid;                /* 0x00: UID */
    uint32_t    hint;               /* 0x08: Hint */
    uint8_t     type;               /* 0x0C: Type */
    uint8_t     reserved;           /* 0x0D: Reserved (0) */
} dbuf_$write_params_t;

#endif /* DBUF_INTERNAL_H */
