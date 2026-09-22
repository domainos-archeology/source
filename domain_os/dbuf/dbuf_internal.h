/*
 * DBUF Internal Header
 *
 * Internal data structures and declarations for the Disk Buffer subsystem.
 * This header should only be included by DBUF implementation files.
 *
 * Memory Layout.  The SAU2 map has `D E78B58 DBUF size = 920`, with interior
 * symbols DBUF at 0xE78B68 and DBUF_$TROUBLE at 0xE79474; every DBUF_ routine
 * loads 0xE78B58 into A5 (e.g. DBUF_$INIT 0x00E3ABE2) and the displacements
 * below are the ones DBUF_$INIT writes (0x00E3ABDA-0x00E3AD12):
 *   +0x000: dbuf_$eventcount  ec_$eventcount_t   0xE78B58  (EC_$INIT at
 *                             0x00E3AD08 takes `pea (A5)`)
 *   +0x00C: 4 pad bytes (ec_$eventcount_t is 12 bytes, the array is aligned)
 *   +0x010: DBUF              dbuf_$entry_t[64]  0xE78B68  (0x24 each;
 *                             64 * 0x24 = 0x900, ending exactly at +0x910)
 *   +0x910: DBUF_SPIN_LOCK                       0xE79468  (`pea (0x910,A5)`
 *                             at 0x00E3A9C4 feeds ML_$SPIN_UNLOCK)
 *   +0x914: dbuf_$head                           0xE7946C  (0x00E3AD02)
 *   +0x918: dbuf_$waiters                        0xE79470  (0x00E3AD0E clr.w)
 *   +0x91A: dbuf_$count                          0xE79472  (0x00E3ABF4 move.w)
 *   +0x91C: DBUF_$TROUBLE                        0xE79474  (0x00E3AD12 clr.w)
 * +0x91E..+0x920 is the segment's trailing pad.
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
#include "netlog/netlog.h"
#include "disk/disk.h"      /* DISK_$READ, DISK_$WRITE */
#include "arch/arch.h"

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
 * Buffer virtual addresses.  DBUF_$INIT walks D6/D7 from 0xD50400 and maps
 * and records `-0x400` off them (0x00E3AC48 `lea (-0x400,A0),A1`,
 * 0x00E3AC9E `pea (-0x400,A1)`), so buffer i is at 0xD50000 + i * 0x400 -
 * DBUF_BLKS / DISK_BUFFERS in the SAU2 map (`D50000  DBUF_BLKS`).
 */
#define DBUF_BLKS_VA            0xD50000u
/* 0x00E3AC98 `pea (0x16).w`: the MMU_$INSTALL flags for a buffer page */
#define DBUF_INSTALL_FLAGS      0x16u

/*
 * Buffer entry flags byte (+0x0C) and the word it starts.
 *
 * The routines test the WORD at +0x0C with `tst.w` (bit 15 = busy) and
 * `btst.l #0xe` (bit 14 = dirty), and the byte with `bset.b/bclr.b #7/#6`,
 * `andi.b #-0x10` and `moveq #0xf / and.b` (the low nibble is the volume
 * index).  All of those are bits of the +0x0C byte; the +0x0D byte is the
 * caller's block type.
 */
#define DBUF_ENTRY_BUSY         0x80    /* bit 7: I/O in progress */
#define DBUF_ENTRY_DIRTY        0x40    /* bit 6: needs writeback */
#define DBUF_ENTRY_VOL_MASK     0x0F    /* bits 0..3: volume index */

/*
 * Buffer entry (0x24 = 36 bytes), a doubly linked LRU list threaded by
 * dbuf_$head.  next / prev / data are 32-bit target virtual addresses
 * (`move.l` cells the list code compares with `cmpa.w #0`), not host
 * pointers, so the record is the same size on every host and the layout
 * can be asserted unconditionally.
 */
typedef struct dbuf_$entry_t {
    uint32_t    next;               /* 0x00: VA of the next (older) entry, 0 at the tail */
    uint32_t    prev;               /* 0x04: VA of the previous (newer) entry, 0 at the head */
    uint32_t    data;               /* 0x08: VA of the 1K buffer */
    uint8_t     flags;              /* 0x0C: busy / dirty / volume index */
    uint8_t     type;               /* 0x0D: block type byte from DBUF_$GET_BLOCK */
    uint16_t    ref_count;          /* 0x0E: outstanding DBUF_$GET_BLOCKs */
    uint32_t    ppn;                /* 0x10: physical page of the buffer */
    int32_t     block;              /* 0x14: disk address, -1 when empty */
    uid_t       uid;                /* 0x18: expected object UID */
    uint32_t    hint;               /* 0x20: caller's block hint */
} dbuf_$entry_t;

_Static_assert(__builtin_offsetof(dbuf_$entry_t, prev) == 0x04, "dbuf_$entry_t.prev");
_Static_assert(__builtin_offsetof(dbuf_$entry_t, data) == 0x08, "dbuf_$entry_t.data");
_Static_assert(__builtin_offsetof(dbuf_$entry_t, flags) == 0x0C, "dbuf_$entry_t.flags");
_Static_assert(__builtin_offsetof(dbuf_$entry_t, type) == 0x0D, "dbuf_$entry_t.type");
_Static_assert(__builtin_offsetof(dbuf_$entry_t, ref_count) == 0x0E, "dbuf_$entry_t.ref_count");
_Static_assert(__builtin_offsetof(dbuf_$entry_t, ppn) == 0x10, "dbuf_$entry_t.ppn");
_Static_assert(__builtin_offsetof(dbuf_$entry_t, block) == 0x14, "dbuf_$entry_t.block");
_Static_assert(__builtin_offsetof(dbuf_$entry_t, uid) == 0x18, "dbuf_$entry_t.uid");
_Static_assert(__builtin_offsetof(dbuf_$entry_t, hint) == 0x20, "dbuf_$entry_t.hint");
_Static_assert(sizeof(dbuf_$entry_t) == DBUF_ENTRY_SIZE, "dbuf_$entry_t is 0x24 bytes");

/*
 * External references to global data (dbuf/dbuf_data.c)
 */

/* DBUF spin lock for buffer pool protection */
extern uint32_t DBUF_SPIN_LOCK;     /* 0xE79468 (base + 0x910) */

/* VA of the head (most recently used entry) of the LRU list */
extern uint32_t dbuf_$head;         /* 0xE7946C (base + 0x914) */

/* Number of processes waiting in DBUF_$GET_BLOCK */
extern uint16_t dbuf_$waiters;      /* 0xE79470 (base + 0x918) */

/* Number of buffers in pool */
extern uint16_t dbuf_$count;        /* 0xE79472 (base + 0x91A) */

/* Per-volume trouble flags (bit N = volume N has trouble) */
extern uint16_t DBUF_$TROUBLE;      /* 0xE79474 (base + 0x91C) */

/* Event count for buffer availability */
extern ec_$eventcount_t dbuf_$eventcount; /* 0xE78B58 */

/*
 * The buffer entry array.  DBUF_$INIT clamps dbuf_$count to
 * DBUF_MAX_BUFFERS (0x00E3AC04-0x00E3AC0A `moveq #0x40`), and 0x40 entries of
 * DBUF_ENTRY_SIZE fill the block exactly from +0x10 to DBUF_SPIN_LOCK at
 * +0x910, so the array is 64 entries long whatever dbuf_$count ends up being.
 */
extern dbuf_$entry_t DBUF[DBUF_MAX_BUFFERS];  /* 0xE78B68 (base + 0x10) */

/* MMAP_$REAL_PAGES (0xE23CA0) comes from mmap/mmap.h */

/* status_$storage_module_stopped (0x0008001b) comes from disk/disk.h */

/*
 * Helpers
 */

static inline dbuf_$entry_t *dbuf_$entry_ptr(uint32_t va)
{
    return (dbuf_$entry_t *)ARCH_VA_TO_PTR(va);
}

/* Get volume index from buffer entry (`moveq #0xf / and.b (0xc,An)`) */
#define DBUF_GET_VOL(entry)     ((entry)->flags & DBUF_ENTRY_VOL_MASK)

/*
 * The eight-longword block header the writeback paths hand to DISK_$WRITE.
 * All three (0x00E3A6DE, 0x00E3A91A, 0x00E3AB38) build it the same way in a
 * 0x20-byte frame area: uid and hint into the first three longwords
 * (12 bytes copied by `moveq #0xb / dbf`), the type byte into +0x10 with a
 * zero byte after it (`move.b Dn,(-0x10,A6)` / `clr.b (-0xf,A6)`), and the
 * other 18 bytes left as whatever the frame held.  Callers zero the array
 * where the image leaves stack contents.
 */
static inline void dbuf_$fill_write_header(const dbuf_$entry_t *e, uint32_t hdr[8])
{
    hdr[0] = e->uid.high;
    hdr[1] = e->uid.low;
    hdr[2] = e->hint;
    hdr[4] = (hdr[4] & 0x0000FFFFu) | ((uint32_t)e->type << 24);
}

/*
 * Error status codes for CRASH_SYSTEM (in-code cells, dbuf/dbuf_data.c)
 */
extern const status_$t OS_DBUF_bad_ptr_err;     /* 0xE3A9E8: bad buffer pointer in SET_BUFF */
extern const status_$t OS_DBUF_bad_free_err;    /* 0xE3A9E4: ref count already 0 */

#endif /* DBUF_INTERNAL_H */
