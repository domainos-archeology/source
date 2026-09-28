---
name: pointer-warning-pass-notes
description: What the -Wint-conversion / -Wincompatible-pointer-types warnings actually turned out to be in area/xpd/smd/dtty/os/mst, plus the byte-cast endianness trap
metadata:
  type: project
---

A compiler pointer-type warning in this tree is almost never "add a cast".  In
the 2026-09-07 pass over area/xpd/smd/dtty/base/os/mst, every one of the 13
sites was a real fidelity bug, in four recurring shapes:

1. **A `pea (d,PC)` constant cell written as `NULL` or `0`.**  smd/blt_u.c
   passed `NULL, NULL` where the code passes &0x00E6F978 (longword 0) and
   &0x00E6D92C (SMD_ACQ_LOCK_DATA); os/shutdown.c passed `&wait_duration`
   where the code passes &0x00E6D62A (the word 2 = NETWORK_OP_SET_VALUE).
   Always resolve `pea (d,PC)`: EA = address-of-extension-word + d, then
   `gsk read` it.

2. **A wholesale wrong prototype copied from a same-named neighbour.**
   `AST_$TOUCH_AREA` (0x00E03548) had been given `AST_$TOUCH`'s signature.
   Its real one is (area_id:w, seg_index:w, page:w, area_page:l,
   ppn_array:l, status:l) - no ASTE anywhere.

3. **The wrong callee entirely.**  dtty_$load_font (0x00E1D668) calls the
   two-argument SMD_$COPY_FONT_TO_MD_HDM at 0x00E1D750, not the
   three-argument SMD_$COPY_FONT_TO_HDM.

4. **A "string" that is really a status longword.**  `extern const char
   SMD_Error_Borrowing_Display_Err[]` was the constant 0x0013000E at
   0x00E6F6FC (bead source-fnzt).  CRASH_SYSTEM only ever takes a
   `const status_$t *`; any `char[]` handed to it is a mis-typed cell.

**The endianness trap.**  Writing `*(uint8_t *)segmap |= 0x80` for
`bset.b #7,(A2)` and `*(uint16_t *)((char *)segmap + 2)` for `(0x2,A2)`
compiles and looks faithful, but on a little-endian host it hits the wrong
half of the longword - the AST_$TOUCH_AREA unit test caught it immediately.
Spell these as longword masks (`*segmap |= 0x80000000u`) and shifts
(`(uint16_t)*segmap`, `(uint16_t)(*segmap >> 16)`).  Same for 4-byte PFT
entries: `(char *)PFT_BASE + (ppn<<2) + 2` is just `PFT_BASE[ppn]`'s low half.

**The A5-displacement variant (2026-09-07, dir/).**  The same trap hides with
no warning at all when the two halves are named by *different displacements*
off the module base.  `dir_$do_op_drop_mount` reads the mount count both as
`move.w (0x155a,A5),D0w` (0x00E53404) and as `cmpi.l #0x1,(0x1558,A5)`
(0x00E5342E) / `subq.l #1` (0x00E5348C) - one longword, two spellings.  Five
dir files had spelled the word form as `*(int16_t *)(a5 + 0x155A)`, which
reads the HIGH half on a little-endian host; every mount-table walk silently
saw zero entries.  Give the block an accessor macro
(`DIR_MOUNT_COUNT16(a5)` = `(int16_t)*(int32_t *)(a5 + 0x1558)`) instead.
Two lessons: (a) before writing an A5 word displacement, check whether
base-2 is read as a longword anywhere in the same routine; (b) the *test*
can carry the identical bug on the planting side and cancel it out - three
sites in dir/test/test_do_op_delete.c planted the count through the same
alias and passed for the wrong reason.

**How to apply:** for each warning, `gsk analyze` the callee prologue for the
real widths AND the caller's push sequence; `pea` = by reference, `move.w/.l
-(SP)` = by value.  Fix whichever side is wrong, and add a host unit test for
any function you re-emit - it is the only thing that catches shape 5 above.
Related: [[feedback-fidelity-gates]], [[domain-pascal-codegen-conventions]].
