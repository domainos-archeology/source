/*
 * mmap/mmap_data.c - MMAP module data
 *
 * The MMAP module data block is `D E23284 MMAP_ size = AA8` in the SAU2 map,
 * running 0xE23284..0xE23D2C.  It is one object, mmap_globals_t; its layout,
 * the map symbol for every cell in it and the accessor macro for each of
 * those cells all live in mmap/mmap.h.
 *
 * The block is the A5 base (0xE23284 in the image) that every MMAP_ entry
 * point loads: a MODULE_DATA block, linked in the SAU2 map's order, with the
 * image's bytes at 0xE23284 and 0xE23C88 (`gsk read`) as its initial
 * contents (source-702z).
 */

#include "mmap/mmap_internal.h"

/*
 * The MMAP_ block, seeded with the image's initial values:
 *
 *   00e23284  00 00 00 00 00 00 0f ff  00 00 02 00 00 00 00 00
 *   00e23c88  00 00 00 42 00 00 00 01  00 00 0f ff 00 00 00 42
 *   00e23c98  00 00 00 00 00 00 00 00  00 00 00 00 00 01 00 08
 *
 * Everything else in the block is zero.  MMAP_$HPPN and MMAP_$LPPN start
 * inverted (1 and 0xFFF) because MMAP_$INIT narrows them as it walks the
 * PMAP entries (mmap/init.c).
 */
MODULE_DATA_DEFINE_INIT(mmap_globals_t, MMAP_$DATA, 0x00E23284, {
    .hi_indx = 0x00000FFF,                    /* 0xE23288 */
    .lo_indx = 0x00000200,                    /* 0xE2328C */
    .min_rmt_pool = 0x00000042,               /* 0xE23C88 */
    .hppn = 0x00000001,                       /* 0xE23C8C */
    .lppn = 0x00000FFF,                       /* 0xE23C90 */
    .pageable_pages_lower_limit = 0x00000042, /* 0xE23C94 */
    .format = 0x0001,                         /* 0xE23CA4 */
    .wsl_hi_mark = 0x0008,                    /* 0xE23CA6 */
});

/*
 * MMAP_$MMAPE - the MMAP page table, map MMAP 0x00EB4800..0x00EC27FF in
 * "D EB4800 OS_PMAPS size = 10000" (the rest of the segment is MMU_$PTTX,
 * mmu/'s).  0xE00 mmape_t entries for ppn 0x200..0xFFF; layout, bias and
 * asserts in mmap/mmap.h.  The image carries no bytes for OS_PMAPS, so the
 * block is zero-filled; MMAP_$INIT fills it.
 * Module data block MMAP_$MMAPE: Claude Opus 5.5 (source-fyjc).
 */
MODULE_DATA_DEFINE(mmap_$mmape_table_t, MMAP_$MMAPE, 0x00EB4800);

/*
 * DUMP_$ADDRS (0xE007EC, in the DUMP segment, not the MMAP_ block) is
 * defined by dump/sau2/dump.s with the DUMP page it belongs to (bead
 * source-gfn1); MMAP_$INIT fills it.
 */

/*
 * Constant status cell in the MMAP_ code segment, shared by MMAP_$AVAIL
 * (0x00E0CC8C) and MMAP_$UNWIRE (0x00E0CD42):
 *
 *   00e0ccb8  00 06 00 04
 */
const status_$t mmap_$bad_avail_00e0ccb8 = status_$mmap_bad_avail;

/*
 * Constant status cell shared by MMAP_$FREE_WSL (0x00E0D170) and
 * MMAP_$SET_WS_INDEX (0x00E0D1E4):
 *
 *   00e0d1c4  00 06 00 0a
 */
const status_$t mmap_$illegal_pid_00e0d1c4 = status_$mmap_illegal_pid;

/*
 * Constant status cell shared by MMAP_$SET_WS_PRI, MMAP_$PURGE (0x00E0D134),
 * MMAP_$SET_WS_INDEX (0x00E0D22E) and MMAP_$WS_SCAN:
 *
 *   00e0c9e0  00 06 00 09
 */
const status_$t mmap_$illegal_wsl_index_00e0c9e0 = status_$mmap_illegal_wsl_index;
