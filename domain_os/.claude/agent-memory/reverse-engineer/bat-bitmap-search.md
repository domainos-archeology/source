---
name: bat-bitmap-search
description: BAT_$ALLOCATE's 6-parameter ABI, its three search extents and the rescan flag, plus the BAT_$FREE / BAT_$ALLOC_VTOCE / BAT_$MOUNT / BAT_$DISMOUNT details the 2026-09-07 pass pinned
metadata:
  type: project
---

# BAT_$ALLOCATE (0xE3B0D6) has SIX parameters

`(0x08)` vol_idx word, `(0x0a)` hint long, **`(0x0e)` alloc_count word,
`(0x10)` use_reserved word**, `(0x12)` blocks_out long, `(0x16)` status long.
The two words are separate: `tst.w (0x10,A6)` at 0xE3B120 and
`cmp.w (0xe,A6),D1w` at 0xE3B38E.  Callers push them together as one
`move.l` immediate, so on big-endian m68k the HIGH half is alloc_count:
`#0x10000` = alloc_count 1, use_reserved 0.  The tree had the halves
backwards (`count & 0xFFFF` / `count >> 16`).  Call sites and their meaning:
`bat/alloc_fm.c`, `bat/alloc_vtoce.c`, `vtoc/allocate.c` x2 all pass (1, 0);
`ast/setup_page_read.c` passes (count, 0); `pmap/fill_write_qblks.c` passes
(extra_count, **1**) - pmap draws on the RESERVED pool.

## Three nested extents plus a stride

chunk `[chunk_start, chunk_end)` (one track) inside partition
`[partition_start, partition_end)` inside `[0, total_blocks)`.
`step_remaining` is loaded from the **longword at volume +0x12**, i.e. the
`(step_blocks, bat_step)` pair as one value (`move.l (-0x222,A2)` at
0xE3B18C and 0xE3B396; BAT_$MOUNT defaults the same longword to 3 at
0xE3B7AA).  `BAT_STEP_LONG()` / `BAT_LABEL_STEP_LONG()` in
`bat/bat_internal.h` spell it without an unaligned cast.

`rescan_chunk` (-0x2e,A6) starts -1 and is re-armed at 0xE3B39E whenever the
stride makes the scan pass over a free bit.  When the chunk runs out with it
set, 0xE3B3B8 `move.l D6,D3` restarts at the **chunk's own start** (D6) -
not the partition start, not the next chunk - and clears the flag.

Chunk selection on exhaustion, all four arms:
- 0xE3B3E0 partition exhausted but `free_count != 0` -> `rel = partition_start`,
  then pick the chunk by division.
- 0xE3B3E6 `free_count == 0` -> next partition, or wrap to partition 0 with
  `rel = 0`; clamp partition_end to total_blocks.
- 0xE3B462/0xE3B472 still inside the partition and the volume ->
  `chunk_start = chunk_end` with **no division**, then
  `chunk_end = chunk_start + alloc_chunk_size`.
- 0xE3B468 `rel < alloc_chunk_offset` or `rel >= total_blocks` ->
  `chunk_start = 0`, `chunk_end = alloc_chunk_offset`.

The no-division arm and a division are **behaviourally equivalent on
reachable inputs** (the only overshoot comes from the all-zero-word skip,
whose overshoot region lies inside that same zero word), so no unit test can
separate them - emit the image's form and argue from the disassembly.

The free-pool capacity test at 0xE3B126-0xE3B150 computes both forms and
selects with the format flag: an OLD-format volume keeps a 0xB-block
cushion, a new-format one does not.

The `DBUF_$SET_BUFF` inside the loop writes its status to a frame cell of
its own (`pea (-0x4,A6)` at 0xE3B2AC), NOT the caller's status.

# The other four

- **BAT_$FREE (0xE3B516)** walks the block array **ascending**: the counter
  at (-0x2e,A6) counts down but the cursor at (-0x34,A6) is bumped by
  `addq.l #0x4` at 0xE3B6C2.  Order is observable - the load-failure exit at
  0xE3B644 leaves the tail unprocessed.  A zero entry is not a block: with
  `reserved` clear it moves one block from the reserved pool to the free
  pool, and with `reserved` set it is ignored.  The partition index comes
  from the ABSOLUTE block, not rel_block.
- **BAT_$ALLOC_VTOCE (0xE3AEC0)**: the hint partition is rejected only when
  `free_count < threshold` (0xE3AF46 `cmp.l` computes threshold-free_count,
  0xE3AF4A `bls` keeps it), while the type-2 arm inside the search IS strict
  (0xE3AF86 `bcc`).  After the search, 0xE3AFB4 `bne` computes the hint for
  ANY nonzero index **including -1** (reachable when num_partitions is 0,
  which skips the loop); only index 0 zeroes it.  Both chain-head rewrites
  (0xE3B098, 0xE3B0B4) `or.l` the block in **unmasked** over the preserved
  status byte - unlike BAT_$ADD_PART_VTOCE, which masks at 0xE3AE94.
  `BAT_PART_CHAIN_LONG` / `BAT_SET_PART_CHAIN_LONG` model that.
- **BAT_$DISMOUNT (0xE3B8BE)** stamps the clock into label +0xB0 AND
  **+0xC0** (0xE3B990), never +0xBC.  Its second argument is a BYTE
  (`move.b (0xa,A6)`, caller pushes `st -(SP)`), still typed `int16_t`
  because both callers are in vtoc/ - bead source-xlyv.
- **BAT_$MOUNT (0xE3B6F8)** ORs the **whole** NODE_$ME longword into label
  +0xB4 (0xE3B7C4) after `andi.l #-0x100000`; no 0xFFFFF mask.

`DBUF_$GET_BLOCK` really takes two WORDS at (0x16,A6)/(0x18,A6)
(0xE3A5CE/0xE3A5D2), not the one `uint32_t` dbuf.h declares; every current
caller emits the same bytes, so it is an ABI mis-statement only - bead
source-ve50.

Tests: `bat/test/test_bitmap_search.c` (30) drives all three bitmap
routines through a mock DBUF that tells BAT and VTOCE blocks apart by the
UID pointer; `bat/test/test_partition_table.c` (18) covers mount/dismount.
Every defect above has a verified negative control except the chunk-advance
one.  **Beware**: BAT_$ALLOCATE loops forever when free_blocks claims space
the bitmap does not have - that is faithful, so test fixtures must always
provide a reachable free bit.

See [[bat-volume-record]].
