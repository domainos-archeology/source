---
name: lv-label-and-disk-req-va
description: The logical-volume label record (bat_$label_t) with its AEGIS Figure 4-4/4-5 names, and why disk_io_req_t's two chain links are uint32_t VA cells
metadata:
  type: project
---

## bat_$label_t lives in bat/bat.h (bead source-f5j9)

Block 0 of a logical volume. BAT_$MOUNT (0xE3B6F8) reads it, BAT_$DISMOUNT
(0xE3B8BE) writes it back, and **DISK_$LV_MOUNT (0xE6CA3A) reads the same
block** -- which is why the record is public, not in bat_internal.h.

Names come from the AEGIS Internals PDF, Figure 4-4 "Logical Volume Label
Format" (4.3.1) and Figure 4-5 "Relationship of BAT header and BAT" (4.3.3).
The OCR renders the offset column as `00 04 24 2e .tC 10 B4 I!S8 BC CO C4` =
`00 04 24 2C 4C B0 B4 B8 BC C0 C4`, and "BAT Header (12 Bytes)" is OCR for
32 bytes: 0x2C + 0x20 = 0x4C, and 0x4C + 100 (VTOC header) = 0xB0. Field
names: 00 Version|Unused, 04 Logical Volume Name, 24 Logical Volume ID,
2C BAT Header, 4C VTOC Header, B0 Time LV Label Written, B4 Unused|Last
Mounted Node, B8 Time System Booted, BC Time Volume Dismounted.

Figure 4-5 breaks the BAT header down, in this order:
2C "No. of Blocks Represented", 30 "No. Blocks Free", 34 "DADDR of first BAT
Block", 38 "Blk No. represented by First BAT Bit", 3C "Volume Trouble |
Unused", 40 "BAT Step to use on This Volume".  BAT_$MOUNT copies exactly
those eight longwords (0x2C..0x4B) into bat_$volume_t+0x00 at 0xE3B7DA.

Two traps in that record:
- **0x3C is written through its HIGH byte.** `andi.b #-0x11,(0x3c,A0)` /
  `or.b D6b,(0x3c,A0)` (0xE3B79C-0xE3B7A6) rewrite bit **12** of the word,
  the same bit `btst.l #0xc` reads at 0xE3B76E -- not bit 4.
- **0x3E and 0x40 are tested as one longword.** `tst.l (0x3e,A0)` at
  0xE3B7AA, default `step_blocks=0, bat_step=3`.  DISK_$LV_MOUNT still reads
  0x40 as a word (`move.w (0x40,A1),(-0x22,A0)` at 0xE6CBEA).

DISK_$LV_MOUNT sets `disk_$volume_t.addr_start = first_data_block (0x38) +
total_blocks (0x2C)` (0xE6CBC8/0xE6CBCC) -- the block just past the BAT's
coverage.

The SAU2 map names **no** symbol for this record; the only label symbols are
PV_LABEL_$UID (0xE1738C) and LV_LABEL_$UID (0xE17394).  That is a real
negative, not a gap.

## The DVTBL bias trap in DISK_$LV_MOUNT

The original walks the volume table as `0xE7A290 + idx*0x48` with **negative**
displacements (-0x48 .. -0x20).  `DISK_VOL(idx)` (= 0xE7A248 + idx*0x48) is
the same cell with those displacements shifted up by 0x48.  The old
lv_mount.c mixed the two -- it used `0xE7A290 + idx*0x48` with *positive*
offsets and so addressed the wrong entry by one whole 0x48-byte record.
Rule: if the disassembly reaches a descriptor with a negative displacement,
emit `DISK_VOL(idx)->field`, never a hand-rolled base + offset.

Also: DISK_$SET_BUFF's status argument is a **scratch cell** at (-0x34,A6),
not the (-0x38,A6) cell that carries the result out.  Passing the real status
variable makes each buffer release clobber the pending result.

## disk_io_req_t's links are VA cells (bead source-wyn9)

`next` (+0x00) and `free_next` (+0x08) are four-byte target addresses moved
with `move.l`, with live fields immediately above them (daddr +0x04,
status +0x0c).  They are `uint32_t` now, converted with ARCH_VA_TO_PTR /
ARCH_PTR_TO_VA, so the whole 0x40-byte layout is asserted on the host build
too (the asserts are no longer ARCH_M68K-guarded).  Same treatment for the
raw `void **` link cells in disk/sort.c and disk/add_que.c, including
add_que's group-end link at +0x18.

`disk_$vol_map_entry_t.head/tail` stay host pointers on purpose: that map is
a caller stack array, so only the target build needs the 8-byte stride.

Host tests that build requests must put them in an arena and set
`ARCH_HOST_VA_BASE` (see disk/test/test_rtn_qblks_internal.c,
disk/test/test_map_request.c) -- start the array one record in so no live
request has VA 0.

Related: [[vtoc-disk-layouts]], [[reference_apollo_pdf_docs]],
[[host-test-va-and-gas-idioms]].
