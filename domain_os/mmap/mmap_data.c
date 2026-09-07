/*
 * mmap/mmap_data.c - MMAP module data
 *
 * The MMAP module data block is `D E23284 MMAP_ size = AA8` in the SAU2 map,
 * running 0xE23284..0xE23D2C.  It is one object, mmap_globals_t; its layout,
 * the map symbol for every cell in it and the accessor macro for each of
 * those cells all live in mmap/mmap.h.
 *
 * On the target the block is the fixed A5 base at 0xE23284 that every MMAP_
 * entry point loads, so it has no C storage there.  On the host it needs
 * storage, and this is its one definition; the image seeds below are the
 * bytes at 0xE23284 and 0xE23C88 (`gsk read`).
 */

#include "mmap/mmap_internal.h"

#if !defined(ARCH_M68K)
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
mmap_globals_t MMAP_GLOBALS_STORAGE = {
    .hi_indx = 0x00000FFF,                    /* 0xE23288 */
    .lo_indx = 0x00000200,                    /* 0xE2328C */
    .min_rmt_pool = 0x00000042,               /* 0xE23C88 */
    .hppn = 0x00000001,                       /* 0xE23C8C */
    .lppn = 0x00000FFF,                       /* 0xE23C90 */
    .pageable_pages_lower_limit = 0x00000042, /* 0xE23C94 */
    .format = 0x0001,                         /* 0xE23CA4 */
    .wsl_hi_mark = 0x0008,                    /* 0xE23CA6 */
};
#endif

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
