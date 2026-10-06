/*
 * DISK_$GET_STATS - Ask a registered driver for its statistics
 *
 * 0x00E3DB9C - 0x00E3DC26 (140 bytes, A5 = DISK_$DEVICES at 0xE7AD5C).
 * Verified against the disassembly on 2026-09-19; the earlier emission
 * was faithful but named the template and the table by private addresses.
 *
 * Arguments:
 *   (0x8,A6)  ctype      word, matched against entry +0x04 (D0)
 *   (0xa,A6)  cnum       word, matched against entry +0x06 (D2)
 *   (0xc,A6)  unit       word, handed to the driver
 *   (0xe,A6)  has_stats  -> byte, cleared first (0x00E3DBBC)
 *   (0x12,A6) stats      -> 22-byte buffer
 *
 * The buffer is preloaded with the template at DISK_$DEVICE_DATA + 0x180
 * (five longwords and a word, 0x00E3DBC4 - 0x00E3DBCE), then the table
 * is searched (`moveq #0x1f` / dbf, stride 0x0c) for the first registered
 * entry (jump_table != 0) with matching type and controller.  If that
 * driver's slot +0x18 is non-null it is called as
 * get_stats(cnum, unit, stats) and has_stats becomes 0xFF when either of
 * the first two longwords of the buffer is non-zero (`sne`/`sne`/`or.b`,
 * 0x00E3DC00 - 0x00E3DC0E).  A match whose slot is null ends the search
 * with has_stats still 0.
 */

#include "disk/disk_internal.h"

void DISK_$GET_STATS(int16_t ctype, int16_t cnum, int16_t unit,
                     uint8_t *has_stats, void *stats)
{
    uint32_t *dst = (uint32_t *)stats;
    const uint32_t *src =
        (const uint32_t *)(const void *)DISK_$DEVICE_DATA.stats_template;
    int16_t i;

    /* 0x00E3DBBC */
    *has_stats = 0;

    /* 0x00E3DBBE - 0x00E3DBCE: moveq #4 / dbf = five longwords, then a word */
    for (i = 0; i < 5; i++) {
        dst[i] = src[i];
    }
    *(uint16_t *)&dst[5] = *(const uint16_t *)&src[5];

    /* 0x00E3DBD0 - 0x00E3DC1A */
    for (i = 0; i < DISK_MAX_DEVICES; i++) {
        disk_device_entry_t *e = &DISK_$DEVICES[i];
        disk_jump_table_t *jt;

        if (e->jump_table == NULL ||
            e->device_type != (uint16_t)ctype ||
            e->controller != (uint16_t)cnum) {
            continue;
        }

        jt = (disk_jump_table_t *)e->jump_table;
        if (jt->get_stats == NULL) {                        /* 0x00E3DBF2 */
            return;
        }

        /* 0x00E3DBF4 - 0x00E3DC0E */
        jt->get_stats((uint16_t)cnum, (uint16_t)unit, stats);
        *has_stats = (uint8_t)((dst[0] != 0 ? 0xFF : 0) |
                               (dst[1] != 0 ? 0xFF : 0));
        return;
    }
}
