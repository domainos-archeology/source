---
name: dir-module-block-tables
description: The DIR module block at A5=0xE7DC00 - its two tables (lock 0x10 stride, handle 0x3C stride), the five scalars around them, and the block-base trap in DIR_$INIT.
metadata:
  type: project
---

Recovered while re-emitting DIR_$CLEANUP (source-f13d) and DIR_$INIT
(source-c3s9), 2026-09-07.

**Block:** map "D E7DBF8 DIR size = 212C" (0x00E7DBF8..0x00E7FD24).  Every DIR
routine does `lea (0xe7dc00).l,A5`, so **A5 is the segment base + 8** - the two
constant pointers live at A5-0x8 / A5-0x4.  Image bytes at 0x00E7DBF8 are
`00 e4 b3 3c 00 e4 b2 30`: A5-0x8 -> DAT_00e4b33c (AST_$PURIFY's segment list),
A5-0x4 -> Naming_bad_request_header_ver_err (0x00E4B230).  **Every DIR
CRASH_SYSTEM site pushes the longword at A5-0x4**, not the constant's address.

**Two parallel 32-entry tables**, both built by one loop in DIR_$INIT:

| A5 off | abs | stride | contents |
|---|---|---|---|
| 0x1680 | 0xE7F280 | 0x10 | `dir_$lock_entry_t[32]` - next/uid union at 0x00, waiters 0x08, lock_count 0x0C, index 0x0E |
| 0x1880 | 0xE7F480 | 0x3C | `dir_$handle_t[32]` - uid 0x00, owner 0x08, lock_mode 0x0A, split_busy 0x0E, length 0x10, wired_page 0x14, buf 0x18, max_slots 0x1C, cur_slot 0x1E, mapped 0x20, page_cache 0x22, next 0x30, lock_entry 0x34, slot_index 0x38, volume 0x3A |

The handle table ends *exactly* on DIR_$NAME_OFFSET_TABLE (0x00E7FC00) and the
lock table ends exactly where the handle table starts - both extents are
closed, no guessing.

**Scalars** (all were loose `DAT_00e7fcXX` objects in the tree, i.e. storage
that did **not** alias the A5 cells the other dir files already used):

- 0x2030 / 0xE7FC30 `DIR_$LOCK_FREE`   -> &lock_tab[0]
- 0x2034 / 0xE7FC34 `DIR_$LOCK_IN_USE` (DIR_$LOCK_OBJ's bitmap)
- 0x2038 / 0xE7FC38 `DIR_$HANDLE_FREE` -> &handle_tab[**1**]
- 0x203C / 0xE7FC3C `DIR_$HANDLE_IN_USE`
- 0x2040 / 0xE7FC40 `DIR_$LINK_BUF_OWNER` (never written by DIR_$INIT)

The three cells the tree called DAT_00e7f470 / DAT_00e7f4b0 / DAT_00e7fbf4 are
**interior `next` fields**: lock_tab[31], handle_tab[0], handle_tab[31].  Handle
slot 0 is the emergency handle DIR_$ALLOC_HANDLE hands out directly, which is
why its link is broken and the free list head starts at slot 1.

**DIR_$INIT's A5 is not the DIR block.**  0x00E3140C is in the boot init
segment ("I E3140C DIR size = E8"); it loads A5 = 0xE3503C (map "D E3503C
OLD_DIR") and never uses it, reaching the block absolutely with
`movea.l #0xe7dc00,A0`.  Any accessor macro built on `__A5_BASE()` is wrong
inside it - dir_internal.h therefore has `DIR_<X>_OF(blk)` forms plus
`DIR_$BLOCK_ABS`.

**Original quirk worth not "fixing":** DIR_$CLEANUP's page scan crashes when
the header's page number *equals* the index it was read at (0x00E53654 `cmp.w
(0xa,A2),D4w` / `bne` past the crash).  DIR_$VALIDATE_PAGES (0x00E53728) uses
the same equality to end its scan instead.

Related: [[byte-pool-vs-typed-array]], [[module-block-alias-pattern]],
[[acl-dir-name-abi-notes]].
