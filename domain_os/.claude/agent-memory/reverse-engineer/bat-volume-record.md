---
name: bat-volume-record
description: The 0x234-byte bat_$volume_t, the 0xE7A1B0 scalar cells, the biased 1-based addressing, and the 0x83-longword label copy
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

The scalars after the six records fill 0xE7A1B0..0xE7A1CB exactly (recovered
from the (disp,A5) accessors, A5 = 0xE79478):

  0xE7A1B0 (0xd38) long   bat_$cached_buffer   (move.l A0 / clr.l / movea.l)
  0xE7A1B4 (0xd3c) long   bat_$cached_block    (cmp.l, move.l D5)
  0xE7A1B7 (0xd3f) BYTE[] bat_$volume_flags, biased; live [1..6] = B8..BD
  0xE7A1BE         2 bytes never referenced (alignment)
  0xE7A1BF (0xd47) BYTE[] bat_$mounted, biased; live [1..6] = C0..C5
  0xE7A1C6 (0xd4e) word   bat_$cached_dirty    (8 = clean, 9 = writeback)
  0xE7A1C8 (0xd50) word   bat_$cached_vol
  0xE7A1CA         2 bytes never referenced (segment tail)

Both byte arrays are addressed `lea (0x0,A5,Dnw*0x1),An` + a byte-wide
instruction, so their stride is 1 and their phantom element 0 overlaps the
last byte of the preceding cell -- bat_$volume_flags[0] IS the low byte of
bat_$cached_block.  Modelling either as a uint32 array is the bug closed by
source-uu78.  BAT_$MOUNT stores `sne` of the label version word
(0x00E3B762/0x00E3B764), so a flag entry is 0 or -1 and every reader tests
only its sign.

BAT_$MOUNT's chunk geometry (0x00E3B820-0x00E3B874) reads DISK_$DVTBL, not a
BAT-owned array: A2 = 0xE7A290 + vol*0x48 and the fields sit at NEGATIVE
displacements, i.e. record v starts at 0xE7A248 + v*0x48 = DISK_VOL(v) from
disk/disk_internal.h.  Fields used: +0x08 lv_start (-0x40, NOT +0x40),
+0x24 blocks_per_cyl (-0x24), +0x2C num_parts (-0x1c) and +0x36 part_volx[0]
interleave mode (-0x12).  alloc_chunk_size = blocks_per_cyl, scaled by
M$MIU$LLW(blocks_per_cyl, num_parts) only when interleave mode == 1; then
alloc_chunk_offset = size - ((first_data_block + lv_start) mod size).
bat/bat_internal.h keeps a duplicate view named bat_$disk_info_t; folding it
into disk_$volume_t is bead source-9ddf.

See [[recovered-layout-fixes]].
