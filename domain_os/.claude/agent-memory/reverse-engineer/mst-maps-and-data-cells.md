---
name: mst-maps-and-data-cells
description: MST_$MAPS' recovered ABI (two Pascal BOOLEAN bytes), how a byte argument is pushed on m68k, and the MST_UNWIRED / MST data-cell layout.
metadata:
  type: project
---

# MST_$MAPS (0x00E43982) and the MST data cells

**A byte argument pushed with `st -(SP)` or `move.b Dn,-(SP)` lands at the
EVEN address.** Those forms decrement A7 by two to keep the stack word
aligned and then access the byte at the *new* (even) A7, so the byte occupies
the first half of the word slot - which is the half the callee reads with
`move.b (0x0a,A6)`. A caller therefore passes a plain boolean (`true`,
0xFF), never a 0xFF00 word.

**Why:** the tree used to declare such parameters `int16_t` and spell TRUE as
0xFF00; once the parameter is a byte, that word truncates to 0 (false), so
retyping and respelling must happen in the same pass.

**How to apply:** whenever a prologue reads an argument with `move.b (d,A6)`
at an even `d`, declare it `boolean` and fix every caller in the same commit,
including the hand-written `MST_$MAPS`-style stub prototypes inside other
subsystems' `test/test_*.c` (they redeclare the function and will conflict).

MST_$MAPS' argument block (frame `link.w A6,-0x4`): +0x08 asid(w),
+0x0A direction(BYTE), +0x0C uid(l), +0x10 start_va(l), +0x14 length(l),
+0x18 area_id(w), +0x1A area_size(l), +0x1E access_rights(BYTE),
+0x20 map_info(l), +0x24 status(l).  The body is one forwarding call to
`mst_$alloc_segs` (0x00E43182) with addr_hint 0x7FFFFFFF and touch_count
MST_$TOUCH_COUNT; `mst_$alloc_segs` reads the two forwarded bytes with
`tst.b (0x22,A6)` / `tst.b (0x24,A6)`.  All 15 call sites push both as bytes.

## Data cells

- 0xE7CF0C is the MST_UNWIRED **A5 module base** (`lea (0xe7cf0c).l,A5`,
  0x00E43988).  SAU2 map: segment E7CF0C size 0x48.  Layout:
  MST_$PAGE_AVAIL_BITMAP 12 longwords (A5+0, image bytes all 0xFF),
  MST_$PAGE_ALLOC_HINT word (A5+0x30), MST_$MST_PAGES_LIMIT (A5+0x32, map
  symbol), MST_$MST_PAGES_WIRED (A5+0x34, map symbol), then an unreferenced
  nine-word table {0,0,1,4,5,2,3,6,7} at 0xE7CF42.
- `MST` = 0xEE5800, 0xC00 bytes = 0x600 words (next map symbol PIT_PAGES at
  EE6400); uninitialised region, `gsk read` fails on it.
- `MST_$ASID_LIST` (0xE24384, 8 bytes) is a Pascal SET whose bit N lives at
  byte `(63-N)>>3`, bit `N&7` - a big-endian 64-bit integer.  The link
  symbols `MST_$ASID_LIST_LONG` (E24384) and `DAT_00e24388` are just its two
  longwords, not separate objects; writing them as longwords is byte-order
  sensitive, so mst_internal.h stores them MSB-first via
  `MST_$ASID_LIST_STORE_LONG()`.

See [[pacct-log-record-stores]], [[feedback_shared_records]].
