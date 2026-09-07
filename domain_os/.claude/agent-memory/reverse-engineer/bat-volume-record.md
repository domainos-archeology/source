---
name: bat-volume-record
description: The recovered 0x234-byte bat_$volume_t, its biased 1-based addressing, and the 0x83-longword label copy
metadata:
  type: project
---

`bat_$volume_t` is 0x234 bytes (`mulu.w #0x234` at 0x00E3B756 and nine other
sites).  Every BAT routine loads `A5 = 0xE79478` (BAT_DATA, `D E79478 BAT_
size = D54` in the SAU2 map) and forms `An = A5 + vol_idx*0x234`, reaching
fields at NEGATIVE displacements — so the record for volume v starts at
`0xE79478 + (v-1)*0x234` and **volume indices are 1..6**, not 0..6
(BAT_$N_FREE rejects 0 and >6 at 0x00E3BA1A/0x00E3BA1E).  Six records fill
0xE79478..0xE7A1AF; the module scalars follow at 0xE7A1B0, which is what pins
the count.  The tree models this by giving `bat_$volumes` the biased base
0xE79244 and indexing by vol_idx, leaving element 0 as a never-touched phantom.

Layout (displacement from An in parentheses):
0x00 total_blocks (-0x234), 0x04 free_blocks (-0x230), 0x08 bat_block_start
(-0x22c), 0x0C first_data_block (-0x228), 0x10 volume_trouble, 0x12
step_blocks, 0x14 bat_step (-0x220), 0x16 reserved, 0x18 reserved_blocks
(-0x21c), 0x1C unknown; 0x20 num_partitions (-0x214), 0x22
partition_start_offset (-0x212), 0x24 partition_size (-0x210), 0x28 unknown;
0x2C partitions[0x40] of 8 bytes; 0x22C alloc_chunk_size (-0x8), 0x230
alloc_chunk_offset (-0x4).

The trap: BAT_$MOUNT copies **0x83 LONGWORDS** (0x20C bytes) from label +0xFC
into vol +0x20 (0x00E3B7F2 `move.w #0x82` + dbf).  0x83 is the longword count,
NOT the partition count — the region is a 0xC-byte header plus 0x200 bytes of
8-byte entries, i.e. 0x40 partitions.  `partitions[0]` starts at +0x2C, proved
by 0x00E3B810-0x00E3B81A writing `free_blocks - 0xB` there for an old-format
volume.  BAT_$DISMOUNT copies the same 0x83 longwords back at 0x00E3B97A, but
only when the new-format flag byte at 0xE7A1B7+vol is negative.

`bat_$partition_t` = {uint32 free_count; uint8 status; uint8 vtoce_block[3]}.
Entry+4 is one longword: high byte = status (1 = VTOCE chain, 2 = free VTOCE
slots), low 24 bits = VTOCE block (`and.l #0xffffff` / `andi.l #-0x1000000` in
BAT_$ADD_PART_VTOCE at 0x00E3AE94/0x00E3AE98).

Still wrong in the tree (not yet filed): `bat_$volume_flags[7]` in
bat/bat_data.c models the new-format flag as a uint32 array whose byte 3 is
the flag, but the real cell is a byte array at 0xE7A1B7 — the uint32 model
aliases bat_$cached_block at 0xE7A1B4.  Likewise `bat_$disk_info` is addressed
with the same 0x48 bias (its `offset` field is at +0x08, not +0x40).

See [[recovered-layout-fixes]].
