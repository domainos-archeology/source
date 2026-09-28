---
name: disk-que-and-as-io-notes
description: DISK_$ADD_QUE's elevator queue (two cylinder-run lists, sentinels with the cylinder in the HIGH half of a longword, the 0xB08 run array, two nested merge procedures) and the AS_IO_SETUP/AS_READ/AS_WRITE/AS_XFER_MULTI/DIAG_IO/DISMOUNT ABIs re-emitted 2026-09-19
metadata:
  type: project
---

Recovered 2026-09-19 (batch sub_disk1_00, disk/ re-emission):

- **DISK_$ADD_QUE (0xE3C716)**: sorts the chain by header[7] (exchange sort,
  0xE3C752-0xE3C7AC), cuts it into *cylinder runs* (one 32-bit VA per run in
  DISK_$DATA+0xB08, entry 0 and count+1 zeroed as terminators), then splices
  runs into `list_a`/`list_b` of disk_$que_t around the current cylinder;
  the direction bit is `tst.w (0x4,A0)` = bit 31 of `position`.  Argument 2
  is the *driver record* (word +0x08 bit 9 = "own queue" -> CRASH 0x8002E,
  word +0x0a = ML_$LOCK id), not the volume descriptor.  `chunk_len` (D6)
  is the first request's +0x1c word, read before the loop reuses +0x1c as
  the run count.  Group end pointers land on the SECOND request of a group
  (`movea.l (A2),A4; move.l A3,(0x18,A4)`).
- The merge helpers 0xE3C5DA (ascending, unsigned `bhi`, empty-list fast
  path via the static link's group_end) and 0xE3C690 (descending, SIGNED
  `blt`, so the 0xFFFF sentinel reads as -1) are nested procedures; now
  `disk_$add_que_merge_ascending/descending` in Ghidra and add_que.c.
- **Sentinel endianness trap**: DISK_$INIT_QUE writes `move.w #0xffff,(0x14,A0)`
  = the HIGH half of the longword at sentinel+4, which ADD_QUE compares with
  the same `cmp.w (0x4,An)` it uses on disk_io_req_t.daddr.  Model the
  sentinel as `{next, daddr}` with cyl in bits 31..16 (DISK_QUE_SENTINEL_CYL
  = 0xFFFF0000); a `uint16_t cyl` at +4 reads 0 on a little-endian host and
  the host test then walks off the sentinel (req_next(NULL)).
- **AS_IO_SETUP/AS_READ/AS_WRITE**: argument 3 is the buffer VA *by value*
  (`move.l (0x10,A6),-(SP)`); the post-MST_$WIRE test is `tst.w (0x2,A2)`
  = LOW status word, then `bset.b #7,(A2)` = bit 31.  AS_READ touches the
  page (`move.w (A1),(A1)`) and forgives 0x80011; AS_WRITE copies the
  header in and does neither.
- **AS_XFER_MULTI** frame: header[16][8] at -0x300, page_status[16] -0x100,
  daddr[16] -0xc0, buffer[16] -0x80, wired[16] -0x40; results walk the
  +0x08 (free_next) chain after READ/WRITE_MULTI; abandoned pages get
  0x80029 "transfer not executed"; a write stores the volume index's low
  byte into op_flags (+0x1f).
- **DISK_$DISMOUNT**: the drive-sharing scan keeps the LAST matching PV
  (`move.w D3w,D2w` overwrites); DISK_$INVALIDATE and DISK_$SHUTDOWN are
  called with a `subq.l #2,SP` result slot that is discarded.
- Host trap: disk_$volume_t.dev_info is `void*`, so host sizeof is 0x50 vs
  the 0x48 DISK_VOL() stride (bead filed); scan loops must use DISK_VOL(idx),
  never `entry++`.

Batch 2 (do_io/error_que/format/format_whole/get_block/get_drte/
get_error_info/get_mnt_info, 2026-09-19):
- **DISK_$ERROR_QUE** is a Pascal FUNCTION wrapper: `subq.l #2,SP` before
  the driver's slot +0x14, D0 propagated; arg 3 is a byte cell (bit 7 =
  error present).  DISK_$DO_IO = slot +0x10 with its four args passed through.
- **DISK_$GET_DRTE(ctype_ptr, cnum_ptr)** searches DISK_$DEVICES (32 x 0x0c)
  for jump_table != 0 && device_type == *ctype && controller == *cnum; it
  is NOT an index lookup.  Only caller: nested FUN_00e6c116.
- **DISK_$FORMAT** leaks the queue block on the invalid-partition exit
  (0x00E3D48A -> exit, no rtn_qblks) and reads part_volx[idx] BEFORE the
  `cmpi.w #8`; partition = head / num_heads + 1, head byte = remainder.
- **DISK_$GET_MNT_INFO**: vol_start comes from the ORIGINAL descriptor,
  everything else from the resolved PV (LV -> part_volx[1]); partition
  words pack dtype_lo<<3 | ctrl_lo (high byte) and unit_lo<<4 (low byte);
  the closing `andi.w #0xfe00,(0x28)` is bit 0 of byte +0x28 plus byte
  +0x29 - never emit it as a uint16_t store.  Its public prototype keeps
  `void *info` because asknode/test/test_internet_info.c mocks it.
- The driver record word +0x08 is used as a FLAGS word by four routines
  although disk_device_entry_t names it unit_count (bead filed).

Batch 3 (get_stats/init/init_que/interrupt/invalidate/io/lv_assign/lv_uid,
lvuid_to_volx, 2026-09-19):
- DISK_IO (0xE3D50E) and DISK_INTERRUPT (0xE0AABC, ATBUS_ segment, A5 =
  IO_$INT_CTRL) were already faithful; op>=5 leaves D4 (internal_op)
  unassigned in the image.
- DISK_$LV_ASSIGN's D2 result is only meaningful on success: pre-lock
  errors return the caller's D2, mid errors ((vol_idx<<6)&0xff00)|bool,
  late errors the low word of the LV start.  D5 (blocks_avail) is
  mount_proc with its low byte overwritten by the seq booleans on the
  early error paths.  DISK_$SET_BUFF's 3rd arg is always a 4-byte frame
  cell (`pea (-0x8,A6)`), never NULL.  Free-slot scan runs 6..1 and keeps
  the LAST free one (lowest index).
- DISK_$LVUID_TO_VOLX scans only descriptors 1..6; D2 = 1 whenever a
  candidate was compared, else the caller's D2.
- DISK_$GET_STATS: jump-table slot +0x18, procedure get_stats(cnum, unit,
  stats); a matched entry with a NULL slot ends the search; template lives
  at DISK_$DEVICES + 0x180 (0xE7AEDC).  DISK_$DEVICES has no definition in
  the tree (extern only); host tests supply their own.

disk2 batches (dbuf/*, disk mnt_dinit/pv_assign*/pv_mount/read*/register/
set_buff/sort/spin_down/unassign*/wait_que/write*, fm/read+write, 2026-09-19):
- **DBUF LRU** (A5 = 0xE78B58): entry links next/prev/data are VA cells;
  victim search walks BACKWARD from the tail (`prev`) for ref_count == 0
  && !busy; after any transfer the code re-enters the search at 0xE3A618
  (holding the lock if nobody waits) and lets the cache-hit path claim
  the entry (ref_count++, NOT = 1).  Dirty = bit 6 of the flags byte
  (`btst.l #0xe` on the word); busy = bit 7.  The DISK_$WRITE header is
  the 8-longword frame area with uid/hint in [0..2] and the type byte at
  +0x10 (hdr[4] high byte).  Buffers are at 0xD50000 + i*0x400 (DBUF_BLKS),
  not 0xD50400.
- **DISK_$MNT_DINIT** forwards 7 args to slot +0x08: (unit, entry->controller,
  five pointers).  **DISK_$PV_ASSIGN** is 8 args; its info cell is passed as
  PV_ASSIGN_N's num_blocks_ptr.  **DISK_$PV_MOUNT** opens no result slot but
  VOLX reads D0 (= PV_MOUNT_INTERNAL's) - keep the int16_t pass-through.
- **DISK_$SET_BUFF** arg 3 is the status cell.  **DISK_$WAIT_QUE** is the
  public gate onto disk_$wait_io (sets A5).  **DISK_$SORT**'s nested
  swap (0xE3C370) reaches 5 parent slots via the static link; pass 2 keys
  on disk_$volume_t.bat_step (+0x26), not on the ADD_QUE chunk word.
- DISK_$REGISTER stores `units` at entry +0x08 and `flags` at +0x0a; the
  readers (ADD_QUE/FORMAT/GET_MNT_INFO/SORT) treat +0x08 as flags.
- FM_$READ/WRITE were faithful; FM_$WRITE's 5th arg is a boolean byte in
  the high half of its word slot.
