---
name: mmap-module-block-map
description: The SAU2 map pins mmap_globals_t field by field; MMAP_$WSL is ws_hdr_t[70] and several "separate" cells are just its fields.
metadata:
  type: project
---

`D E23284 MMAP_ size = AA8` names every object, so `mmap_globals_t`
(mmap/mmap.h) is fully pinned: +0x04 HI_INDX, +0x08 LO_INDX, +0x0C WS_REMOVE,
+0x10 RECLAIM_PUR_CNT, +0x14 RECLAIM_SHAR_CNT, +0x18 WS_SCAN_CNT,
+0x1C WS_OVERFLOW, +0x20 STEAL_CNT, +0x24 ALLOC_PAGES, +0x28 ALLOC_CNT,
+0x2C MMAP_$WSL (70 x 0x24 = 0x9D8), +0xA04 MIN_RMT_POOL, +0xA08 HPPN,
+0xA0C LPPN, +0xA10 PAGEABLE_PAGES_LOWER_LIMIT, +0xA14 PAGEABLE_PAGES,
+0xA18 REMOTE_PAGES, +0xA1C REAL_PAGES, +0xA20 FORMAT (w),
+0xA22 WSL_HI_MARK (w), +0xA24 WS_OWNER (64 words), +0xAA4 RMT_LIMIT.

**Why:** several tree names were separate `extern`s for what are really fields
of MMAP_$WSL or MMAP_$WS_OWNER, and one pair of addresses (0xE23C94 vs
0xE23C98) is two distinct cells the tree had merged.

**How to apply:** the six `MMAP_$WSL_*_CNT` Ghidra labels are
`MMAP_WSL[pool].page_count`; 0xE23366/6C/7C/80 are `MMAP_WSL[5]`'s owner /
scan_pos / pri_timestamp / ws_timestamp; `MMAP_$WS_OWNER` (0xE23CA8) is a
64-word table every reader indexes 1-based through a base biased by -2
(`movea.l #0xe23ca8,A3 / move.w (-0x2,A3,D1w*0x1)`), and is the same storage
the tree called MMAP_$WSL_INDEX_TABLE.  `MMAP_$PROC_WS_LIST` was PROC1_$TYPE
(0xE2612C).  `MEM_EXAM_TABLE` is DUMP_$ADDRS (0xE007EC), two 8-byte ranges,
image-initialised to { 0x00100000, 0x0017FC00 }.

Related: [[sau2-map-name-corrections]], [[reference_sau2_domain_os_map]].
