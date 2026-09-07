---
name: attr-query-call-shape
description: The AST_$GET_ATTRIBUTES call shape (0x20-byte location record in, 0x20 bytes out), the 0x7A compact attribute record, and the big-endian "byte 1 of a word" request-cell trap - recovered 2026-09-07
metadata:
  type: project
---

## AST_$GET_ATTRIBUTES (0x00E047A0) is a location-record in/out routine

Argument 1 is a **`file_$obj_loc_t` (0x20 bytes)**, never a `uid_t *`:

- object UID at record+0x08 (`lea (0x8,A4),A0` 0x00E047D2; `pea (0x8,A4)` at
  0x00E047EC, 0x00E04810, 0x00E04836)
- caller clears record+0x1D bit 6 first (`bclr.b #0x6`)
- on **every** path that reaches an AOTE the routine writes all 0x20 bytes back
  from **aote+0x9C** (0x00E0492C remote-refresh, 0x00E049B0 cached)
- after unlocking: `flags & 0x80` + `record+0x1D & 0x80` -> `0x000F0001`
  (0x00E049CC-0x00E049E0), then `record+0x10 = ROUTE_$PORT_ARRAY[0].network`
  when zero (0x00E049E8)

It is a **procedure**: callers reserve a 2-byte result slot at A6+0x16 that the
body never writes. `AST_$GET_COMMON_ATTRIBUTES`, `AST_$GET_ACL_ATTRIBUTES` and
`AST_$GET_LOCATION` all take the same record.

The three flag bits that matter: 0xFC00 = illegal, 0x0020 = refresh a remote
object, 0x0200 = ask for the full form, 0x0080 = "touch" (tested as
`tst.b D2b`, i.e. bit 7 of the word's LOW byte).

## FILE_$GET_ATTRIBUTES / FILE_$GET_ATTR_INFO frames

Both build the descriptor and the 0x90-byte attribute record **adjacent** in
the frame (record at A6-0xB0, descriptor at A6-0x20, so record+0x90 ==
descriptor), the same shape AST_$GET_COMMON_ATTRIBUTES uses. Both take the
caller's own `file_$obj_loc_t` as argument 4, probe it with FILE_$DELETE_INT
**before** filling the local descriptor, and copy the local descriptor back
over it afterwards.

Their size checks sit in different places and this is load-bearing:
FILE_$GET_ATTRIBUTES checks at 0x00E5D9F2, i.e. **after** the probe and after
seeding the descriptor; FILE_$GET_ATTR_INFO checks at 0x00E5D89C, **after** the
AST call and after the record copy-back.

`file_$attr_info_t` (0x7A bytes, in file/get_attr_info.c) maps the compact
record; source offsets are attribute-record offsets, and the three unwritten
holes are 0x2A-0x2B, 0x34-0x35 and 0x6B-0x6D. PACCT reads its +0x32 word as the
TTY device number.

## `btst.b #n,(0x1,A0)` on a two-byte cell is a WORD test, not `byte[1]`

The attribute-query request cell is a word; the compiler addresses its **low**
byte, which is byte 1 only because the m68k is big-endian. Modelling it as
`((uint8_t *)param_2)[1] & bit` silently inverts on a little-endian host and
made every host test take the error path. Read the word and mask it instead.

The same trap applies to a masked longword that is then bit-edited byte by
byte (FILE_$GET_ATTR_INFO's record+0x00..0x03): declare it as four bytes and
place them big-endian by hand, or the byte surgery lands on the wrong bits.

## Constant cells found this pass

| cell | value | passed by | as |
|------|-------|-----------|----|
| 0xE5DAAE | 0xFF boolean | `pea (0x18,PC)` 0x00E5DA94, `pea (-0x4c,PC)` 0x00E5DAF8 | VTOCE_$NEW_TO_OLD arg 2 |
| 0xE52392 | word 6 | `pea (0x40,PC)` 0x00E52350 | FILE_$SET_PROT prot_type |
| 0xE4B33C | NIL long | `pea (-0x6fe0,PC)` 0x00E5231A | FILE_$PRIV_LOCK acl_ctx |
| 0xE5A8B8 | word 0x7A | `pea (-0x380,PC)` 0x00E5AC36, `pea (0x4c,PC)` 0x00E5A86A | GET_ATTR_INFO size |
| 0xE5A8BA | word 0x0401 | `pea (0x4a,PC)` 0x00E5A86E | PACCT_$START request |
| 0xE5AD34 | word 0x0004 | `pea (0xf8,PC)` 0x00E5AC3A | PACCT_$LOG request |

## DIR_$SET_PROTECTION's request record starts 3 bytes before its opcode

`move.b #0x52,(-0xe5,A6)` with the request base at A6-0xE8: the opcode byte is
at **request+0x03**, the UID at +0x04, the version word at +0x0E, the 44-byte
protection image at +0x8E, the ACL UID at +0xBA and the type word at +0xC2
(0xC4 bytes total). DIR_$DO_OP's fifth argument is a *separate* local at
A6-0x100 — bead source-32ld; ~15 dir/ callers guess `&request` for it.
