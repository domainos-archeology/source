/*
 * dump/dump.h - the DUMP module's public cells
 *
 * The SAU2 map gives the crash-dump code its own segment:
 *
 *   D23  E00400  DUMP               loaded at 101C00, size = 400
 *        E00400  DUMP
 *        E00762  DUMP_$SMAP_PHADDR
 *        E007E8  DUMP_$RARS_PHADDR
 *        E007EC  DUMP_$ADDRS
 *
 * The whole page is hand-written assembly, dump/sau2/dump.s (bead
 * source-gfn1): the code the boot PROM enters after a crash, and its
 * cells.  It defines the storage of all four symbols; MMAP_$INIT fills
 * DUMP_$ADDRS (bead source-3uo moved the declaration here).
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
 * will fill.  DUMP walks the ranges (dump/sau2/dump.s 0xE004E0: `lea
 * (DUMP_$ADDRS,PC),A3', start at 0xE004E4 with 0 ending the walk, end at
 * 0xE004E8) and sends every 1 KB page from start through end.
 *
 * Image contents: range 0 = { 0x00100000, 0x0017FC00 }, range 1 = { 0, 0 }.
 * The last 4 bytes of the map's 0x14 extent (0xE007FC) have no reference.
 */
#define DUMP_ADDRS_RANGES 2
extern mem_range_t DUMP_$ADDRS[DUMP_ADDRS_RANGES];

/*
 * DUMP - 0xE00400, the page's first instruction (entry with d7 = 1; a
 * second entry at +4 has d7 = 2).  Entered from the PROM with register
 * arguments only, never called from C; declared so C can take the page's
 * address.  0x400 bytes.
 */
#define DUMP_SIZE 0x400
extern uint8_t DUMP[DUMP_SIZE];

/*
 * DUMP_$SMAP_PHADDR (0xE00762) and DUMP_$RARS_PHADDR (0xE007E8): two
 * longwords the map names inside the page; zero in the image, and no
 * code in the image references them by address (nothing in the kernel
 * holds 0xE00762 or 0xE007E8).
 */
extern uint32_t DUMP_$SMAP_PHADDR;
extern uint32_t DUMP_$RARS_PHADDR;

#endif /* DUMP_DUMP_H */
