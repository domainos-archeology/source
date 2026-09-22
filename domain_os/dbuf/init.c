/*
 * DBUF_$INIT - Build the buffer pool
 *
 * 0x00E3ABDA - 0x00E3AD1E (326 bytes, A5 = 0xE78B58).  Re-emitted from the
 * disassembly on 2026-09-19.  Wrong before: the buffers were placed at
 * 0xD50400 + i * 0x400 and mapped there (the image walks D6/D7 from
 * 0xD50400 but installs, cache-inhibits and records `-0x400` off them,
 * i.e. 0xD50000 + i * 0x400, the map's DBUF_BLKS), and the terminating
 * `clr.l` after the loop was aimed at entry[count].prev instead of
 * entry[count - 1].next (0x00E3ACEC-0x00E3ACFA: A5 - 0x14 + count * 0x24).
 *
 * dbuf_$count = ((MMAP_$REAL_PAGES >> 10) << 4) as a word, clamped to
 * [6, 64] with SIGNED compares (0x00E3ABF8 `bge`, 0x00E3AC06 `ble`).
 * Entry i gets next = &entry[i + 1], prev = &entry[i - 1] (fixed up after
 * the loop), data = the buffer VA, flags/type/ref_count 0, a page from
 * WP_$CALLOC (a failure crashes with the status cell itself), installed
 * with MMU_$INSTALL(ppn, va, 0x16) and MMU_$CACHE_INHIBIT_VA(va), block
 * -1, uid = UID_$NIL, hint 0.
 */

#include "dbuf/dbuf_internal.h"
#include "uid/uid.h"

void DBUF_$INIT(void)
{
    uint16_t count;             /* D0w / (0x91a,A5) */
    int16_t n;                  /* D2 */
    dbuf_$entry_t *e;           /* A2 */
    uint32_t va;                /* D6 - 0x400 */
    uint32_t ppn;               /* (-0x8,A6) */
    status_$t status;           /* (-0xc,A6) */
    uint16_t i;

    /* 0x00E3ABE8 - 0x00E3AC0E */
    count = (uint16_t)((MMAP_$REAL_PAGES >> 10) << 4);
    dbuf_$count = count;
    if ((int16_t)count < DBUF_MIN_BUFFERS) {
        dbuf_$count = DBUF_MIN_BUFFERS;
    } else if ((int16_t)count > DBUF_MAX_BUFFERS) {
        dbuf_$count = DBUF_MAX_BUFFERS;
    }

    /* 0x00E3AC0E - 0x00E3ACE4: dbf over dbuf_$count entries */
    e = &DBUF[0];
    n = (int16_t)(dbuf_$count - 1);
    va = DBUF_BLKS_VA;
    if (n >= 0) {
        for (i = 0; i <= (uint16_t)n; i++) {
            /* 0x00E3AC36 - 0x00E3AC4E: links to the neighbours by
             * address, entry[0].prev pointing before the array until the
             * fix-up at 0x00E3ACE8 */
            e->next = ARCH_PTR_TO_VA(e) + DBUF_ENTRY_SIZE;
            e->prev = ARCH_PTR_TO_VA(e) - DBUF_ENTRY_SIZE;
            e->data = va;
            /* 0x00E3AC52 - 0x00E3AC6E: bclr #7, bclr #6, andi #0xCF,
             * andi #0xF0 - every bit of the flags byte */
            e->flags = 0;
            e->type = 0;
            e->ref_count = 0;
            /* 0x00E3AC72 - 0x00E3AC96 */
            WP_$CALLOC(&ppn, &status);
            if (status != status_$ok) {
                CRASH_SYSTEM(&status);
            }
            /* 0x00E3AC98 - 0x00E3ACB8 */
            MMU_$INSTALL(ppn, va, DBUF_INSTALL_FLAGS);
            MMU_$CACHE_INHIBIT_VA(va);
            /* 0x00E3ACBA - 0x00E3ACCE */
            e->ppn = ppn;
            e->block = -1;
            e->uid.high = UID_$NIL.high;
            e->uid.low = UID_$NIL.low;
            e->hint = 0;
            /* 0x00E3ACD2 - 0x00E3ACDE */
            e = dbuf_$entry_ptr(e->next);
            va += DBUF_BUFFER_SIZE;
        }
    }

    /* 0x00E3ACE8 - 0x00E3ACFA: terminate both ends */
    DBUF[0].prev = 0;
    DBUF[dbuf_$count - 1].next = 0;

    /* 0x00E3ACFE - 0x00E3AD12 */
    dbuf_$head = ARCH_PTR_TO_VA(&DBUF[0]);
    EC_$INIT(&dbuf_$eventcount);
    dbuf_$waiters = 0;
    DBUF_$TROUBLE = 0;
}
