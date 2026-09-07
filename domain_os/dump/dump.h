/*
 * dump/dump.h - the DUMP module's public cells
 *
 * The SAU2 map gives the crash-dump code its own segment:
 *
 *   D    E00400  DUMP               size = 400
 *        E007EC  DUMP_$ADDRS
 *
 * There is no dump/*.c yet: the only cell the tree has recovered is
 * DUMP_$ADDRS, whose storage is defined by mmap/mmap_data.c because MMAP_$INIT
 * is what fills it.  The declaration lives here, with the module that owns the
 * storage (bead source-3uo).
 */

#ifndef DUMP_DUMP_H
#define DUMP_DUMP_H

#include "base/base.h"

/*
 * Memory range descriptor.  Two longwords, the shape of one DUMP_$ADDRS entry
 * (MMAP_$INIT writes start at (-0x8,A3) and end at (-0x4,A3), 0x00E319D6 -
 * 0x00E31A2E).
 */
typedef struct mem_range_t {
    uint32_t start;
    uint32_t end;
} mem_range_t;

/*
 * DUMP_$ADDRS - the physical memory ranges MMAP_$INIT hands to the crash-dump
 * code.  Named by the SAU2 map (`E007EC DUMP_$ADDRS`, inside the DUMP
 * segment, 0x14 bytes up to the APP segment at 0xE00800); the tree used to
 * call it MEM_EXAM_TABLE.
 *
 * MMAP_$INIT walks it with `movea.l #0xe007ec,A4 / lea (A4),A3` and
 * `addq.l #0x8,A3` per range, writing start at (-0x8,A3) and end at
 * (-0x4,A3) (0x00E319D6-0x00E31A2E), and crashes once the range count passes
 * 2 (`cmpi.w #0x2,D4w / ble`, 0x00E31A0C) - so two 8-byte ranges are all it
 * will fill.  DUMP reads range 0's end at 0x00E004E8 and range 1's start at
 * 0x00E004E4.
 *
 * Image contents: range 0 = { 0x00100000, 0x0017FC00 }, range 1 = { 0, 0 }.
 * The last 4 bytes of the map's 0x14 extent (0xE007FC) have no reference.
 */
#define DUMP_ADDRS_RANGES 2
extern mem_range_t DUMP_$ADDRS[DUMP_ADDRS_RANGES];

#endif /* DUMP_DUMP_H */
