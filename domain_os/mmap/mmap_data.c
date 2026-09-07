/*
 * mmap/mmap_data.c - MMAP module data
 *
 * The MMAP module data block is `D E23284 MMAP_ size = AA8` in the SAU2 map,
 * running 0xE23284..0xE23D2C.  The map names every object in it, and those
 * names pin mmap_globals_t (mmap/mmap.h) displacement for displacement:
 *
 *   +0x000  0xE23284  (unnamed)
 *   +0x004  0xE23288  MMAP_$HI_INDX
 *   +0x008  0xE2328C  MMAP_$LO_INDX
 *   +0x00C  0xE23290  MMAP_$WS_REMOVE
 *   +0x010  0xE23294  MMAP_$RECLAIM_PUR_CNT
 *   +0x014  0xE23298  MMAP_$RECLAIM_SHAR_CNT
 *   +0x018  0xE2329C  MMAP_$WS_SCAN_CNT
 *   +0x01C  0xE232A0  MMAP_$WS_OVERFLOW
 *   +0x020  0xE232A4  MMAP_$STEAL_CNT
 *   +0x024  0xE232A8  MMAP_$ALLOC_PAGES
 *   +0x028  0xE232AC  MMAP_$ALLOC_CNT
 *   +0x02C  0xE232B0  MMAP_$WSL        70 x ws_hdr_t (0x9D8)
 *   +0xA04  0xE23C88  MMAP_$MIN_RMT_POOL
 *   +0xA08  0xE23C8C  MMAP_$HPPN
 *   +0xA0C  0xE23C90  MMAP_$LPPN
 *   +0xA10  0xE23C94  MMAP_$PAGEABLE_PAGES_LOWER_LIMIT
 *   +0xA14  0xE23C98  MMAP_$PAGEABLE_PAGES
 *   +0xA18  0xE23C9C  MMAP_$REMOTE_PAGES
 *   +0xA1C  0xE23CA0  MMAP_$REAL_PAGES
 *   +0xA20  0xE23CA4  MMAP_$FORMAT              (word)
 *   +0xA22  0xE23CA6  MMAP_$WSL_HI_MARK         (word)
 *   +0xA24  0xE23CA8  MMAP_$WS_OWNER            64 words (0x80)
 *   +0xAA4  0xE23D28  MMAP_$RMT_LIMIT
 *
 * On the target every one of these is reached through the absolute MMAP_
 * base (MMAP_GLOBALS / MMAP_WSL in mmap/mmap.h); the objects below are the
 * separately named cells the rest of the tree links against.
 *
 * Image contents (gsk read 0x00E23284 / 0x00E23C88): everything is zero
 * except MMAP_$HI_INDX = 0xFFF, MMAP_$LO_INDX = 0x200,
 * MMAP_$MIN_RMT_POOL = 0x42, MMAP_$HPPN = 1, MMAP_$LPPN = 0xFFF,
 * MMAP_$PAGEABLE_PAGES_LOWER_LIMIT = 0x42, MMAP_$FORMAT = 1 and
 * MMAP_$WSL_HI_MARK = 8.
 */

#include "mmap/mmap_internal.h"

/* +0x00C - pages removed from working sets by the WS scanner */
uint32_t MMAP_$WS_REMOVE;               /* 0xE23290 */

/* +0x010 / +0x014 - reclaim counters reported by OSINFO_$GET_MMAP */
uint32_t MMAP_$RECLAIM_PUR_CNT;         /* 0xE23294 */
uint32_t MMAP_$RECLAIM_SHAR_CNT;        /* 0xE23298 */

/* +0x018 - working-set scan passes */
uint32_t MMAP_$WS_SCAN_CNT;             /* 0xE2329C */

/* +0x01C - working sets that exceeded their maximum */
uint32_t MMAP_$WS_OVERFLOW;             /* 0xE232A0 */

/* +0x020 / +0x024 / +0x028 - allocator counters */
uint32_t MMAP_$STEAL_CNT;               /* 0xE232A4 */
uint32_t MMAP_$ALLOC_PAGES;             /* 0xE232A8 */
uint32_t MMAP_$ALLOC_CNT;               /* 0xE232AC */

/*
 * +0xA08 / +0xA0C - the highest and lowest pageable page numbers.
 * MMAP_$INIT narrows them from the image seeds (HPPN = 1, LPPN = 0xFFF) as it
 * walks the PMAP entries (mmap/init.c).
 */
uint32_t MMAP_$HPPN = 0x00000001;       /* 0xE23C8C */
uint32_t MMAP_$LPPN = 0x00000FFF;       /* 0xE23C90 */

/*
 * +0xA10 - the floor PMAP's purifiers keep the pageable-page count above.
 * Written at 0x00E1157E and 0x00E11910; image value 0x42.
 */
uint32_t MMAP_$PAGEABLE_PAGES_LOWER_LIMIT = 0x00000042;   /* 0xE23C94 */

/* +0xA14 - pages currently pageable (counted up by MMAP_$INIT) */
uint32_t MMAP_$PAGEABLE_PAGES;          /* 0xE23C98 */

/* +0xA18 / +0xA1C - remote and real page totals */
uint32_t MMAP_$REMOTE_PAGES;            /* 0xE23C9C */
uint32_t MMAP_$REAL_PAGES;              /* 0xE23CA0 */

/*
 * +0xA24 - the WSL index owned by each process, 64 words.  See
 * mmap/mmap_internal.h for the biased 1-based addressing every reader uses.
 */
uint16_t MMAP_$WS_OWNER[MMAP_WS_OWNER_SLOTS];   /* 0xE23CA8 */

/*
 * DUMP_$ADDRS - the two physical memory ranges MMAP_$INIT records for the
 * crash-dump code.  This one cell is image-initialised.
 *
 *   00e007ec  00 10 00 00  00 17 fc 00  00 00 00 00  00 00 00 00
 *
 * Original address: 0xE007EC (in the DUMP segment, not the MMAP_ block)
 */
mem_range_t DUMP_$ADDRS[DUMP_ADDRS_RANGES] = {
    { 0x00100000, 0x0017FC00 },
    { 0, 0 },
};
