---
name: area-app-mac-audit-sweep
description: AREA seg-table pool, the shared APP reply-header prefix, the MAC/MAC_OS channel and broadcast records, the AUDIT bump allocator, and how the tree models a FIM_$CLEANUP frame carry-over
metadata:
  type: project
---

# AREA seg-table pool and its allocator (0xE1E118 block)

- `AREA_$GLOBALS + 0x150` is **not** an opaque region: it is
  `area_$seg_table_t seg_table_pool[64]` (0x0C stride, +0x150..+0x44F).
  AREA_$INIT (0x00E2F4D4-0x00E2F4E2) clears only byte +0x03 of each, which is
  the pool's `allocated` flag.
- `0x00E09D2E` was named `area_$lookup_seg_table` in Ghidra; it is really
  **`area_$alloc_seg_table`** (renamed).  It takes ML lock 0x12, refuses with
  NIL when the count has reached 64, hands out
  `seg_table_pool[AREA_$FORMAT.seg_table_next]`, sets `allocated` with `st`
  (0x00E09DBE), wires a 0x400-byte overflow-slot page at
  `0xEE6400 + index * 0x400` (WP_$CALLOC 0x00E09D88, MMU_$INSTALL 0x00E09D9A,
  zeroed at 0x00E09E36), advances the cursor past the next unallocated record
  (`tst.b (0x153,A1)` at 0x00E09DE8) and links onto
  `seg_table_list[asid]` (0x00E09E1E).
- `AREA_$FORMAT` (map symbol at 0xE1E6EC = globals+0x5D4) is four words, and
  the last two are the pool's bookkeeping, **not** spare:
  +0x5D8 = `seg_table_next` (the free cursor, also scaled to form the bitmap
  page VA at 0x00E09D6A), +0x5DA = `seg_table_count` (capped at 64).
- AREA_ lock ids: `ML_LOCK_AST` is **0x12** (AREA_$COPY, area_$alloc_seg_table);
  the 0x14 the AREA_$TOUCH / AREA_$ASSOC / AREA_$TRANSFER paths take is
  `ML_LOCK_PMAP`.
- Technique that found the accessors: `gsk read` the whole AREA_ code segment,
  `xxd -r -p` it, `m68k-elf-objdump -D -b binary -m m68k:68020
  --adjust-vma=<base>`, then grep for `%aN@(<decimal displacement>)` in the
  range of interest.  `gsk xrefs to` misses every indexed access.

# The shared APP reply-header prefix

`app_$reply_hdr_t` (app/app.h, 8 bytes packed) is the prefix APP_$RECEIVE
builds at 0x00E00980-0x00E009AC through A0 = `app_$receive_rec_t.reply`:
magic 0x0118 (+0x00), template_len (+0x02), data_len (+0x04), request_id
(+0x06).  `msg_$reply_hdr_t` and `asknode_$reply_hdr_t` embed it as
`prefix` and keep their tails; `rem_file/` and `rip/` use it directly.
MSG spells the +0x06 word "message type", ASKNODE "reply id" - same cell.

# MAC / MAC_OS

- The channel table is ONE table with two models in the tree:
  `mac_os_$channel_t` based at MAC_OS_$DATA+0x7A0 (callback +0x00,
  driver_info +0x04, socket +0x08, port_index +0x0A, callback_data +0x0C,
  line_number +0x0E, header_size +0x10, flags +0x12), and the legacy
  `mac_$channel_entry_t` in mac/mac.h based at +0x7A8 - the same records seen
  eight bytes in.  Nothing uses the legacy view any more.
- `callback` and `driver_info` are 32-bit target VAs (ARCH_VA_TO_PTR /
  ARCH_PTR_TO_VA), so the 20-byte entry holds on the host.
- **MAC_OS_$DATA + 0x8E0 (0x00E23270) is not an "ARP table"**: it is one
  constant `rip_$nexthop_t` holding the broadcast address
  (`00 00 00 00 ff ff ff ff ff ff`), the last object in the 0x8EC block, and
  MAC_$SEND's only use of it is `pea (0x8e0,A5)` as MAC_OS_$ARP's first
  argument.  Modelled as `MAC_OS_$BROADCAST_NEXTHOP` (tree name; the map gives
  the cell no symbol).
- MAC_$SEND's channel-owner test works on the flags word's HIGH byte
  (`and.b` + `lsr.w #2`), so the owner is word bits 10..15.

# AUDIT bump allocator (0x00E7120C)

`audit_$alloc(size, status_ret)`:
- `size == 0` rewinds `AUDIT_$DATA.pool_next` to 0x00EC4800 and seeds
  `pool_limit` **only while it is still zero**, then returns NIL.
- `size != 0` returns the old cursor, advances it by the **sign-extended**
  size word (`ext.l D1` at 0x00E7123C), then while `pool_next >= pool_limit`
  (unsigned `bcc`) wires one 0x400 page: WP_$CALLOC, bail out on a bad status
  still returning the block, MMU_$INSTALL(page, pool_limit, 0x16),
  `pool_limit += 0x400`.
- **Neither arm ever stores status_$ok.**  Callers must pre-clear the cell.
- There is no `audit_$free` in the image.

# Modelling a FIM_$CLEANUP frame carry-over

MAC_OS_$SEND (0x00E0B5A8) reads A6-0x44 and A6-0x3e on the FIM_$CLEANUP
unwind return (0x00E0B7E2 / 0x00E0B7EA) where the m68k frame still holds
whatever the aborted pass stored - the prologue never initialises them.
The tree's answer (source-gs4e): declare such locals **`volatile` and
uninitialised**, with a comment citing the `link.w` and both read sites.
`volatile` is what makes it faithful - the compiler must re-read the storage
rather than carry a value across the pseudo-longjmp - and it also keeps
`-Wmaybe-uninitialized` quiet.
