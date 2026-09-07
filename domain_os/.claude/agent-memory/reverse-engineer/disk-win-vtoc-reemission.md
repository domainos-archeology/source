---
name: disk-win-vtoc-reemission
description: Layouts and traps recovered while emitting disk_$map_request / disk_$io_error / disk_$chksum_page, WIN's DISK_INIT, the disk_$volume_t striping fields, vtoce_$result_t's true size and CAL_$VERIFY's constant cells (Sept 2026)
metadata:
  type: project
---

Verified against the disassembly while closing beads source-cm2w, source-9tbm,
source-1nob, source-3ena, source-eb9k and source-rcd6.

**Why:** these cost the most work to recover and nothing else in the tree
records them. **How to apply:** trust these over any decompiler output.

## disk_$volume_t geometry and striping (0xE7A1CC + N*0x48 + 0x7C)

- `+0xa0 blocks_per_cyl` = `(num_heads * sec_per_track) >> sector_size_code`,
  built by DISK_$PV_MOUNT_INTERNAL at 0xE6C74E-0xE6C75C.
- `+0xa6 sector_size_code` is a **shift between blocks and hardware sectors**,
  not a byte size: PV_MOUNT divides by it, map_request shifts a block
  remainder left by it before splitting into head/sector, GET_MNT_INFO reports
  `1 << code` at info+0x12. The old "0/1/2 -> 256/512/1024" comment was
  backwards.
- Striping: `+0xaa blk_mask`, `+0xac blk_shift`, `+0xae vol_mask`,
  `+0xb0 vol_shift`. A striped address splits as
  `chunk = daddr & blk_mask`, `group = daddr >> blk_shift`,
  `member = (group/blocks_per_cyl) & vol_mask`,
  `cyl = (group/blocks_per_cyl) >> vol_shift`, and the volume is
  `part_volx[chunk + (member << blk_shift) + 1]`.
  PV_MOUNT looks each shift up in the log2 table at **DISK module base +
  0x4a** indexed by its mask (0xE6C73C, 0xE6C748).
- **`part_volx[0]` (+0xb2) is not a volume index**: it is the interleave mode
  from the PV label's +0xbc word, and PV_MOUNT switches on it (1..5) at
  0xE6C6F2 to derive the two masks from `num_parts`. Non-zero is what makes
  disk_$map_request take the striped path.
- `+0xa2` is **`bat_step`**, not a shift (bead source-zot4, settled). It is
  the LV label's +0x40 word, mirrored into the backing PV descriptor and only
  read back out by GET_MNT_INFO at info+0x0c. The DISK subsystem does no
  arithmetic with it because the real consumer is the BAT manager -- and
  **this tree already had the answer**: `bat/bat_internal.h` names label
  +0x40 `bat_step`, `bat/mount.c` defaults it to 3 and `BAT_$GET_BAT_STEP`
  returns it. Grep the other subsystems for a field at the same source
  offset before declaring a copied word unrecoverable.
  Corroboration: AEGIS Internals and Data Structures (Jan 1986) 4.3.3 lists
  the BAT header's fields in the label's own order and ends with "the BAT
  step to use on this volume"; its glossary explains a step of 2 means
  "allocate the next block at block n+2 ... set via INVOL to optimize disk
  seeks". invol's user-facing name is the **sector interleave factor**
  (sys/help/invol.hlp option 10, "not supported at SR10.4", which is why
  nothing in SR10.4 writes it; the SR10.2 invol binary still has the
  dialogue and reads it through disk_$get_mnt_info).

## disk_io_req_t sub-fields the struct hides

- `req+0x1c` is **not** flags in the usual sense: disk_$map_request stores
  `stripe_blk_mask + 1` there, the transfer length the driver may use
  (0xE3CB6E).
- `req+0x3c` is `header[7]`: map_request overwrites it with the absolute disk
  address (0xE3CB42), so the on-disk block header is self identifying.
- The per-request volume map is `{head, tail}` pairs indexed `volx-1`
  (addressed `(-0x8,A3,volx*8)` / `(-0x4,A3,volx*8)`, 0xE3CC38-0xE3CC4E), with
  no bound check on volx.

## DISK_$ERROR_INFO (0xE7AC60 = DISK_$DATA + 0xa94)

86 bytes, exactly what DISK_$GET_ERROR_INFO copies: timestamp, daddr,
`{sec_per_track, num_heads}` longword, the caller's 8-longword info, the
request's 8-longword header, ppn, status, vol_idx word. The linear-address
path of disk_$io_error writes only four of those, leaving the geometry/info/
header parts holding the previous CHS failure.

The packed device id both io_error and its log record use is
`((dev_info[5] << 3) & 0xF8) | dev_info[7]` in the high byte and
`(dev_unit & 0xFF) << 4` in the low byte, built with Pascal packed-field
mask-then-OR stores.

## vtoce_$result_t is 0x90 bytes, not 0x150

0x150 is the on-disk **entry stride** inside a VTOCE block. Every routine that
moves an entry across the caller boundary moves 36 longwords: VTOCE_$READ
0xE395B0, VTOCE_$WRITE 0xE3977A, VTOC_$ALLOCATE 0xE38C26. VTOCE_$OLD_TO_NEW's
highest destination store is +0x8C, VTOCE_$NEW_TO_OLD's highest source load is
+0x88. OS_$INIT's 0x90 buffer was right all along.

## WIN internals

- Module base 0xE2B89C; unit records are 12 bytes (`unit*12`), the drive's
  register block is the pointer at unit+0x04, the ML lock id the word at
  unit+0x08, and the 12-byte eventcounts share the stride from +0x30.
- Register block: +0x00 command, +0x02 parameter, +0x06 status
  (bit 15 busy, bit 7 not-ready, bit 11 done), +0x0c mode, +0x0e "go".
- `WAIT_FOR_CONTROLLER` (0xE190BC, was FUN_00e190bc) spins on bit 15 with a
  3-tick TIME_$CLOCKH deadline, returns 0x00080002 (controller busy) on
  timeout, then maps bit 7 to 0x00080001 (not ready). It does **not** set up
  A5 - it uses the caller's.
- `DISK_INIT` (0xE19986) is WIN-internal despite the name: bsr-called, status
  in D0, no Pascal result slot. WIN_$DINIT swaps its own first two arguments
  when calling it. Its unrecognised-drive path (0xE19B80) sets the error and
  then falls through into the geometry multiply with D1 still holding the
  drive id, overwriting `*total_blocks` anyway.

## STOP_$WATCH's operation table really has ops 8, 9 and 10

The branch table at 0xE81854 is two-byte `bra.b`s for ops 2..6 but a
**four-byte `bsr.w`** for op 7, so 8 (`ori.b #0x81,D6`, a no-op), 9
(`move.l D1,(A1)` - a long poke that misses the DISK_$DIAG gate) and 10
(the exit branch) all land on real instruction boundaries. Op 11 lands on the
gate itself, entered by `jmp` not `bsr`, so its `rts`/`addq.l #4,SP` corrupt
the caller's stack. The parent slot index is scaled with `lsl.w #6` and added
with a sign-extending `adda.w`, and 0x40*0x400 == 0x10000, so slot numbers
0x200..0x3FF come out **negative**.

## Small ones

- `M$OIU$WLW(long,short)` is an unsigned 32/16 remainder (0xE0ACC0);
  `M$MIU$LLW` an unsigned 32x16 product (0xE0AC02); `M$MIS$LLW`/`M$MIS$LLL`
  the signed ones (0xE0AC44 / 0xE0ABD4). All truncate to 32 bits and preserve
  A1.
- VFMT_$WRITE10 (0xE825F4; called ERROR_$PRINT before the SAU2 map sweep) is a gate into VFMT_$WRITEN: `VFMT_$WRITEN(fmt,
  &varargs)`, so its extra arguments are **addresses**, and vfmt's `%a` takes
  two - a character pointer and a pointer to its length.
- A longword constant in the code region can produce a false Ghidra string:
  0x00192549 at 0xE34AD8 makes "%I5  " appear at 0xE34ADA because 0x2549 is
  "%I". Check the reader's operand size before believing a string.
- Host testability: the fixed module bases are overridable per subsystem
  (`WIN_DATA_BASE` gains an `#else` branch onto `WIN_$DATA` under ARCH_HOST;
  disk tests `#undef`/`#define DISK_VOLUME_BASE` onto a mock array before
  including the .c). A spin loop that polls a plain global clock also needs a
  seam - `WIN_CLOCKH()` in win_internal.h - or the test hangs.

## The queue-block allocator's out-parameters are 32-bit VA cells

`disk_$get_qblks_internal` (0x00E3BE8A) writes both out-parameters with a
single `move.l` -- 0x00E3BF7E `move.l (0xc0,A5),(A0)` for the head and
0x00E3BFB8 `move.l (0xc0,A5),(A2)` for the tail -- and reloads the tail cell
at that width with `movea.l (A2),A3` at 0x00E3BFD8.  So both are four-byte
target VA cells, NOT host pointers, and the callee must not store a
`void *` through them: DISK_IO's frame keeps them adjacent at (-0x90,A6) and
(-0x8c,A6) (0x00E3D5DC), so an 8-byte store destroys the second.  Every
out-of-subsystem caller already declares a four-byte cell
(ast/read_area_pages.c, ast/touch_area.c, pmap/flush_write_batch.c,
pmap/purifier_l.c, disk/as_xfer_multi.c), which is why `DISK_$GET_QBLKS`
keeps its `int32_t *` / `uint32_t *` pair -- the signedness split is caller
spelling, not two different cells.  Callers inside disk/ (io.c, format.c,
format_whole.c) hold `uint32_t` cells and convert with `ARCH_VA_TO_PTR`.

Related trap: `disk_io_req_t` declares `next` / `free_next` as host pointers,
so on a 64-bit host its layout diverges from the target past +0x04.  A test
must not mix `req->daddr` with a raw `+0x04` offset -- use one or the other.
