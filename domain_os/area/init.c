/*
 * AREA_$INIT - Initialize the area subsystem
 *
 * Original address: 0x00E2F3A8, size 340 bytes (SAU2 map: `I  E2F3A8  AREA_
 * size = 154`, one routine).
 *
 * Called once during system startup.  Everything it touches is a field of the
 * AREA_ module data block at 0xE1E118 (`D  E1E118  AREA_  size = 5E8`); see
 * area/area.h for the recovered record and its offset assertions.  This file
 * used to reach the block through `(uint32_t *)AREA_GLOBALS_BASE` and raw byte
 * displacements, which put the three diskless page cells at +0x08/+0x0C/+0x10
 * instead of +0x00/+0x04/+0x08.  (source-vm49)
 */

#include "area/area_internal.h"
#include "misc/crash_system.h"

/*
 * Loop trip counts, all `moveq #N,Dn` + `dbf` = N+1 iterations:
 *   0x00E2F3CE  moveq #0x39  -> 58   seg_table_list[] and asid_list[]
 *   0x00E2F3EC  moveq #0xa   -> 11   uid_hash[] and uid_hash_pool[]
 *   0x00E2F448  moveq #0x2   -> 3    diskless RPMAP pages
 *   0x00E2F4D4  moveq #0x3f  -> 64   seg_table_pool[]
 */
#define AREA_INIT_ASID_SLOTS        AREA_MAX_ENTRIES            /* 58 */
#define AREA_INIT_HASH_SLOTS        AREA_UID_HASH_BUCKETS       /* 11 */
#define AREA_INIT_RPMAP_PAGES       AREA_DISKLESS_PAGE_COUNT    /* 3  */
#define AREA_INIT_POOL_SLOTS        AREA_SEG_TABLE_POOL_COUNT   /* 64 */

/* 0x00E2F3B6 `move.w #0x540,(0x5d6,A0)`: the maximum size of the area table. */
#define AREA_FORMAT_MAX_ENTRIES     0x540

/* 0x00E2F4A8 `move.w #-0x1,(0xa,A0)`: an RPMAP cache slot marked empty. */
#define AREA_RPMAP_SLOT_EMPTY       0xFFFF

/* 0x00E2F48A / 0x00E2F4AC `pea (0x16).w`: the MMU_$INSTALL flag word used for
 * both the RPMAP cache pages here and the PEB control page elsewhere. */
#define AREA_RPMAP_MMU_FLAGS        0x16

void AREA_$INIT(void)
{
    int i;
    status_$t status;                   /* A6-0x10 */
    uint32_t page_va;                   /* A6-0x0C */

    /* 0x00E2F3B0-0x00E2F3CC: the scalar cells at the top of the block. */
    AREA_$FORMAT.max_entries = AREA_FORMAT_MAX_ENTRIES;   /* +0x5D6 */
    AREA_$FREE_LIST          = NULL;                      /* +0x5C8 */
    AREA_$N_AREAS            = 0;                         /* +0x5E2 */
    AREA_$N_FREE             = 0;                         /* +0x5E0 */
    AREA_$NEXT_CALLER_ID     = 0;                         /* +0x5C4 */

    /*
     * 0x00E2F3CE-0x00E2F3EA: one `dbf` clears BOTH 58-longword arrays, the
     * per-ASID area list heads at +0x4D8 and the per-ASID extended
     * segment-table list heads at +0x68.
     */
    for (i = 0; i < AREA_INIT_ASID_SLOTS; i++) {
        AREA_$ASID_LIST[i]                  = NULL;     /* 0x00E2F3DC */
        AREA_$GLOBALS.seg_table_list[i]     = NULL;     /* 0x00E2F3E0 */
    }

    /*
     * 0x00E2F3EC-0x00E2F40C: clear the 11 hash buckets and thread the 11 pool
     * records into a free list, each pointing at its successor.  The `dbf`
     * target is 0x00E2F3F8, one instruction past `movea.l A2,A0`, so A0 (the
     * pool cursor) is set once and then advances by 8 while A2 (the bucket
     * cursor) advances by 4.
     */
    for (i = 0; i < AREA_INIT_HASH_SLOTS; i++) {
        AREA_$UID_HASH[i]                = NULL;                        /* 0x00E2F3F8 */
        AREA_$UID_HASH_POOL[i].next      = &AREA_$UID_HASH_POOL[i + 1]; /* 0x00E2F400 */
    }

    /*
     * 0x00E2F414: the last link written above pointed one record past the
     * pool; `clr.l (0x4d0,A3)` (globals+0x4D0 = &uid_hash_pool[10]) nils it,
     * terminating the free list.
     */
    AREA_$UID_HASH_POOL[AREA_INIT_HASH_SLOTS - 1].next = NULL;

    /* 0x00E2F418-0x00E2F422: the free-list head is the first pool record. */
    AREA_$UID_HASH_FREE = &AREA_$UID_HASH_POOL[0];

    /* 0x00E2F426: clear the high half of the 8-byte partner node address. */
    AREA_$PARTNER.high = 0;

    /*
     * 0x00E2F42A-0x00E2F43E: only a diskless node has a partner.
     * NETWORK_$DISKLESS is a Domain boolean, tested `tst.b` / `bpl`.
     */
    if (NETWORK_$DISKLESS < 0) {
        AREA_$PARTNER.low = NETWORK_$MOTHER_NODE;   /* 0x00E2F432 */
    } else {
        AREA_$PARTNER.low = 0;                      /* 0x00E2F43C */
    }

    /*
     * 0x00E2F440-0x00E2F4C4: a diskless node wires three pages and maps them
     * as the RPMAP cache window at AREA_RPMAP_CACHE_VA.
     *
     * The cursors:
     *   A2 = globals + 4, and WP_$CALLOC gets `pea (-0x4,A2)`, so the page
     *        cell filled on pass i is globals + i*4 - i.e. rpmap_page[i],
     *        at +0x00, +0x04 and +0x08.  MMU_$INSTALL then re-reads that same
     *        cell with `move.l (-0x4,A2),-(SP)`.  (source-vm49)
     *   D4 = 0xEE4C00 + 0x400 on entry, and the VA installed is D4 - 0x400,
     *        so the window starts at 0xEE4C00 and steps by 0x400.
     *   A3 = globals + 0x0C, the biased cursor for rpmap_cache[i]; every field
     *        write is at A3 + 4 .. A3 + 0x0D, i.e. record + 0x00 .. + 0x09.
     */
    if (NETWORK_$DISKLESS < 0) {
        uint32_t va = AREA_RPMAP_CACHE_VA + AREA_RPMAP_PAGE_SIZE;   /* D4 */

        for (i = 0; i < AREA_INIT_RPMAP_PAGES; i++) {
            /* 0x00E2F45E-0x00E2F46C */
            WP_$CALLOC(&AREA_$GLOBALS.rpmap_page[i], &status);

            /* 0x00E2F46E-0x00E2F47E */
            if (status != status_$ok) {
                CRASH_SYSTEM(&status);
            }

            /* 0x00E2F480-0x00E2F488: the VA is computed into A1 and also
             * stored to the (write-only) local at A6-0x0C. */
            page_va = va - AREA_RPMAP_PAGE_SIZE;

            /* 0x00E2F48A-0x00E2F49A: MMU_$INSTALL(page, va, flags), the page
             * number read back out of the cell WP_$CALLOC just filled. */
            MMU_$INSTALL(AREA_$GLOBALS.rpmap_page[i], page_va,
                         0, AREA_RPMAP_MMU_FLAGS);

            /* 0x00E2F49E-0x00E2F4B4 */
            AREA_$GLOBALS.rpmap_cache[i].seq     = 0;
            AREA_$GLOBALS.rpmap_cache[i].volx = 0;
            AREA_$GLOBALS.rpmap_cache[i].group = AREA_RPMAP_SLOT_EMPTY;
            AREA_$GLOBALS.rpmap_cache[i].dirty = 0;
            AREA_$GLOBALS.rpmap_cache[i].in_trans = 0;

            /* 0x00E2F4BA */
            va += AREA_RPMAP_PAGE_SIZE;
        }
    }

    /* 0x00E2F4C6-0x00E2F4D2: the two AREA_$FORMAT words after max_entries. */
    AREA_$FORMAT.seg_table_count = 0;   /* +0x5DA */
    AREA_$FORMAT.seg_table_next  = 0;   /* +0x5D8 */

    /*
     * 0x00E2F4D4-0x00E2F4E2: clear byte +0x03 of each of the 64 0x0C-byte
     * records at globals+0x150 - the `allocated` flag of every
     * area_$seg_table_t in the pool area_$alloc_seg_table (0x00E09D2E) hands
     * out.  No other field of a pool record is initialised here.
     */
    for (i = 0; i < AREA_INIT_POOL_SLOTS; i++) {
        AREA_$GLOBALS.seg_table_pool[i].allocated = 0;
    }

    /* 0x00E2F4E4-0x00E2F4F0: the dedup counters. */
    AREA_$CR_DUP  = 0;      /* +0x5DE */
    AREA_$DEL_DUP = 0;      /* +0x5DC */
}
