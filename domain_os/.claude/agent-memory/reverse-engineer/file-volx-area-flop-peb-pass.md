---
name: file-volx-area-flop-peb-pass
description: Recovered constants and layouts from the 2026-09-07 FILE_/VOLX_/AREA_/FLOP_/PEB_/PACCT_ fidelity pass - the shared 251 cell, the AREA_ module block map, FLOP_$BOOT's cells, the SET_PROT selector bit.
metadata:
  type: project
---

# FILE_ / VOLX_ / AREA_ / FLOP_ / PEB_ / PACCT_ recoveries (2026-09-07)

## The one UID_$HASH modulus cell, 0x00E5EA28

Image bytes `00 FB` = 251.  FOUR FILE_ routines reach the SAME word with
`pea (d,PC)`: FILE_$DELETE_INT 0x00E5E8FE, FILE_$LOCAL_READ_LOCK 0x00E60528,
FILE_$LOCAL_LOCK_VERIFY 0x00E60832, FILE_$PRIV_LOCK 0x00E5F18C,
FILE_$PRIV_UNLOCK 0x00E5FD5A.  Defined once as `file_$lot_hash_modulus` in
file/file_data.c.  251 is also the word count FILE_$LOCK_INIT clears at
FILE_$LOCK_CONTROL+0xC8 (0x00E327BE `move.w #0xfa,D0w`), so
**FILE_$LOT_HASHTAB has 251 buckets, not 58** - it was sized
FILE_LOCK_TABLE_ENTRIES (58) and a remainder of 250 ran off the end.

## FILE_$FW_PAGES sorts DESCENDING

0x00E5E80C `move.l (-0x84,A1),D0` / `cmp.l (-0x84,A3),D0` / `bls` skips the
exchange when `batch[j] <= batch[i]`, so AST_$PURIFY gets the batch in
descending unsigned order.  Outer count is `count-1` (`D0 = count-2` + dbf),
and a batch of exactly one skips the sort at 0x00E5E7C4.

## The AREA_ module data block (0xE1E118, map size 0x5E8) tiles exactly

    +0x000 rpmap_page[3]        the WP_$CALLOC cells (NOT +0x08/+0x0C/+0x10)
    +0x00C                      the biased array cursor's landing pad
    +0x010 rpmap_cache[3]       0x0C bytes each; +0x06 = 0xFFFF means empty
    +0x038/+0x048/+0x058        the three map-named eventcounts, stride 0x10
    +0x068 seg_table_list[58]
    +0x150 64 x 0x0C records    only byte +3 is ever touched (bead source-tqkk)
    +0x450 uid_hash_free
    +0x454 uid_hash[11]
    +0x480 uid_hash_pool[11]
    +0x4D8 asid_list[58]
    +0x5C0 rpmap_seq            0x00E073D0 increments it, 0x00E073DE stamps a slot
    +0x5C4 next_caller_id  +0x5C8 free_list  +0x5CC partner(8)
    +0x5D4 AREA_$FORMAT(4 words; +2 = 0x540 = the area-table maximum)
    +0x5DC del_dup +0x5DE cr_dup +0x5E0 n_free +0x5E2 n_areas +0x5E4 pkt_size

The RPMAP cache manager at 0x00E07370 holds a cursor BIASED four bytes below
the record (`lea (0xc,A5),A1`, fields at `(0x4,A0)`/`(0xa,A0)`), and indexes
1-based with `(0x4,A5,D1*0x0C)`.  AREA_$INIT's diskless loop uses the same
bias, which is why the page cells look like they start at +0x0C and do not.

## FLOP_$BOOT's `pea (d,PC)` cells

    0x00E32538  byte 0x00   FILE_$LOCK rights AND MST_$MAP_AT concurrency
    0x00E3253A  word 0x0004 FILE_$LOCK lock mode
    0x00E32540  word 0x0001 FILE_$LOCK lock index
    0x00E32542  byte 0xFF   MST_$MAP concurrency (a DIFFERENT cell)
    0x00E326C6  word 0x0007 mapping mode
    0x00E3272C  long 0x00100000 mapping length
    0x00E32730  long 0        start AND extend - one cell pushed twice
    0x00E326DE  word 0x0013 = 19, the length of "/flp/sys/boot_shell"

All five console messages end with '%' then NUL, not a full stop.

MST_$MAP_AT (0x00E42F54) reads its arguments as va(l) uid(p) start(l)
length(l) mode(**word**) extend(l) concurrency(**byte**) map_info(p)
status(p) - its forwarding prologue at 0x00E42F62-0x00E42FA0 shows each width.

## PEB_$INIT's cells

0x00E31DCE = word 0x0001 and 0x00E31DD0 = long 0x00FF7000, both handed to
io_$probe by address.  MMU pages 0x2C -> 0xFF7000 (control) and 0x2E ->
0xFF7800 (WCS), flags 0x16.  Vectors 0x2C (FIM_$FLINE) and 0x70 (PEB_$INT);
writing them through `ARCH_VA_TO_PTR` makes the routine host-testable and is
a no-op on m68k.

## FILE_$SET_PROT's default-protection selector is BIT 24 of acl_uid->low

0x00E5DF58 `and.w (0x4,A2),D0w` is a WORD read at the START of the `low`
longword - the HIGH word on this big-endian target.  With `#0x0FF0` and
`lsr.w #4` the `btst #4` lands on bit 24 of `low`; 0x00E5DF6E's
`andi.b #-0x10` then clears bits 24..27 of the copy.  A `(low & 0xFF0) >> 4`
reading tests bit 8 and is wrong.

## FILE_$SET_PROT_INT's two locksmith tests have OPPOSITE senses

0x00E5DE5C uses `sne` (deny 0x230001 when ACL_$GET_LOCAL_LOCKSMITH returns
non-zero, and only when PROC1_$TYPE[cur] == 9); 0x00E5DE7E uses `seq` and the
`beq 0x00E5DE9C` at 0x00E5DE98 SKIPS the clear when the type IS 9 - so the
0x230010 status is forgiven when the call returned 0 **or** the type is NOT
9.  `subsys_flag` is a BOOLEAN byte: read `move.b (0x14,A6),D3b`, re-pushed
`move.b D3b,-(SP)`, and the REM_FILE_ server pushes it `move.b (-0x42c,A2)`.

Related: [[byte-pool-vs-typed-array]], [[module-block-alias-pattern]],
[[mst-maps-and-data-cells]], [[file-lot-and-lock-init]].
