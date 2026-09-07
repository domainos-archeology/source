---
name: ast-common-attrs-and-rem-file-trailer
description: The ast_$common_attr_t layout and its call shape, the REM_FILE request {version,super} trailer, and ACL_$RIGHTS' real second parameter - all recovered 2026-09-06
metadata:
  type: project
---

## AST_$GET_COMMON_ATTRIBUTES (0x00E04A00) and `ast_$common_attr_t`

The function's own frame **is** the full attribute record (`link.w A6,-0x90`),
so a listing displacement `d` is record offset `d + 0x90`. Getting that wrong
put two wrong source offsets in `ast/get_common_attributes.c` for months.

AST_$GET_ATTRIBUTES fills that record verbatim from **aote+0x0C**
(`lea (0xc,A2),A0` + `moveq #0x23` + 36 longword moves at 0x00E049A2), so
`record offset R == aote offset 0x0C+R`.

`ast_$common_attr_t` (0x18 bytes, now in `ast/ast.h`, naturally aligned so no
packing needed):

| off | field | source |
|-----|-------|--------|
| 0x00 | obj_type | aote+0x0C |
| 0x01 | sub_type | aote+0x0D — **the byte nearly every caller tests** |
| 0x02 | attr_flags_hi | aote+0x0E, bits 1..0 replaced from aote+0x71 bits 5..4 |
| 0x03 | attr_flags_lo | aote+0x0F (bit 1 = read-only volume) |
| 0x04 | length | aote+0x20, object byte length |
| 0x08 | mod_time | aote+0x48, attribute 5 |
| 0x10 | blocks | aote+0x50, attribute 0x0B |
| 0x14 | refcount | aote+0x80, the count attributes 6/7/8 move |
| 0x16 | access_flags | bits 7..6 from aote+0x71; bit 7 = OS-only access |

**The call shape is always the same** and four things go wrong when it is not
followed (all four were present at nine call sites):

1. Argument 1 is a **0x20-byte `file_$obj_loc_t`**, not a `uid_t *`. The UID
   goes at descriptor **+0x08** (`lea (0x8,A4),A0` at 0x00E047D2) and the
   caller clears descriptor+0x1D bit 6 (`bclr.b #0x6`) first. On success
   AST_$GET_ATTRIBUTES overwrites all 32 bytes from aote+0x9C, which is why
   callers read descriptor+0x02 afterwards (it becomes aote+0x9E) and
   descriptor+0x1D (aote+0xB9, bit 7 = remote).
2. The output buffer is **0x18 bytes**. Buffers of 1, 4 and 8 bytes were being
   passed.
3. The descriptor and the record are adjacent in the frame — the record sits
   *below* the descriptor — so a single byte array can model both
   (dir_$do_op_add_link does: record at +0x08, descriptor at +0x20).
4. `tst.w` on the word at record+0x16 is "access_flags bit 7", because the
   byte at +0x17 is never written.

`file_$obj_loc_t` now lives in **file/file.h** (it was internal); AST is
arguably its real owner, but the `file_$` name and the existing users made
moving it to ast/ more churn than it was worth.

## Every REM_FILE request carries a {version, super-mode} trailer

Right after each opcode's payload, at the next even offset:

```
+N   uint16_t version;      /* always 3 */
+N+2 boolean  super_user;   /* ACL_$SUPER_COUNT[PROC1_$CURRENT] > 0, via sgt */
```

Seen at +0x10/+0x12 in REM_FILE_$PURIFY (0x00E62290), +0x18/+0x1A in
REM_FILE_$UNLOCK (0x00E61D6A) and +0x1C/+0x1E in REM_FILE_$SET_DEF_ACL
(0x00E6232A); REM_FILE_$SERVER reads the pair for opcode 0x18 at
request+0x1C/+0x1E. `ACL_$SUPER_COUNT` is 1-based in the image
(`movea.l #0xe7dacc,A1` + `tst.w (-0x2,A1,Dn*1)`), and the C array is declared
at 0xE7DACA so `ACL_$SUPER_COUNT[PROC1_$CURRENT]` is correct.

Note REM_FILE_$UNLOCK appends two more fields (+0x1C lock key, +0x20 release
boolean) *after* that trailer — a later protocol extension, which is why the
server guards the release byte with `request_len >= 0x22`.

`rem_file_internal.h`'s `REM_FILE_OP_*` table is guessed and wrong for at least
four opcodes (bead source-8joj); every builder writes its own literal instead.

## ACL_$RIGHTS' (0x00E46A00) second parameter is a `boolean *`

`acl/acl.h` calls it `void *unused` and callers pass NULL. The image
dereferences it: `movea.l (0xc,A6),A3` then `move.b (A3),-(SP)` at
0x00E46A50-0x00E46A54. NAME_$SET_WDIRUS passes the address of a constant byte
0xFF. Widths of the other arguments: `move.l (A2)` for the rights mask at
A6+0x10, `move.w (A1)` for the option flags at A6+0x14. Bead source-6vl5,
19 call sites.

## Naming status codes are now the database's own wording

`name/name.h` was renamed to match `stcodes/stcode.db.10.4` exactly
(source-tiil): the old `naming_name_is_not_a_file` (0xE000E) is
`naming_branch_is_not_a_directory`, `naming_acl_not_found` (0xE0033) is
`naming_directory_object_not_found`, and two duplicate spellings of 0xE0016
collapsed onto `naming_directory_locked`. The header now also lists the
module-0x0E codes the port does **not** define, so nobody re-guesses
0xE0010 / 0xE0034 again.
