---
name: mmap-module-block-map
description: The MMAP_ module data block (0xE23284, 0xAA8 bytes) as one mmap_globals_t; every map cell, the offset-0 spin lock, and the MMAP_PID_TO_WSL -2 bias.
metadata:
  type: project
---

`D E23284 MMAP_ size = AA8` (0xE23284..0xE23D2C) is ONE object,
`mmap_globals_t` in `mmap/mmap.h`; every separately named cell is an accessor
macro over it (`#define MMAP_$HPPN (MMAP_GLOBALS.hppn)` etc.), storage is
`MMAP_GLOBALS_STORAGE` on the host / the address literal on m68k.

**Why:** as of 2026-09-07 (bead source-mu8j, split from source-75vi) the block
was modelled three times - `mmap_globals_t`, standalone `MMAP_$` scalars in
`mmap_data.c`, and an unused `MMAP_LOCK`. All three are now one.

**How to apply:** offset 0x000 is unnamed in the map but is the module spin
lock - every entry point does `lea (0xe23284).l,A5 / pea (A5) / jsr
ML_$SPIN_LOCK` (MMAP_$FREE 0x00E0CACA/0x00E0CAE2), so it is
`MMAP_GLOBALS.lock`. Offsets: 004 HI_INDX, 008 LO_INDX, 00C WS_REMOVE, 010
RECLAIM_PUR_CNT, 014 RECLAIM_SHAR_CNT, 018 WS_SCAN_CNT, 01C WS_OVERFLOW, 020
STEAL_CNT, 024 ALLOC_PAGES, 028 ALLOC_CNT, 02C WSL[70] (0x9D8), A04
MIN_RMT_POOL, A08 HPPN, A0C LPPN, A10 PAGEABLE_PAGES_LOWER_LIMIT, A14
PAGEABLE_PAGES, A18 REMOTE_PAGES, A1C REAL_PAGES, A20 FORMAT (word), A22
WSL_HI_MARK (word), A24 WS_OWNER[64], AA4 RMT_LIMIT.

`MMAP_PID_TO_WSL` is NOT a separate array: it is `&MMAP_GLOBALS.wsl_hi_mark`,
the -2 biased base of `MMAP_$WS_OWNER`, so `MMAP_PID_TO_WSL[pid] ==
MMAP_$WS_OWNER[pid - 1]` and `MMAP_PID_TO_WSL[0] == MMAP_$WSL_HI_MARK`.
`MMAP_$RMT_LIMIT` is a longword whose only readers (0x00E11598, 0x00E1192A)
`tst.b` its most-significant byte, i.e. the sign of the longword.
`MMAP_$HI_INDX`, `MMAP_$LO_INDX` and `MMAP_$FORMAT` have no xrefs at all -
they exist only to hold the layout and their image seeds.

Related: [[byte-pool-vs-typed-array]], [[module-base-inherited-a5]].
