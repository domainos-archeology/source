---
name: rem-file-client-buffer-and-layouts
description: The 0xBE reply buffer at A6-0xC0 that every rem_file client shares, how to turn an A6 displacement into a record offset, and the per-opcode request/reply layouts recovered in the 2026-09-07 pass
metadata:
  type: project
---

Recovered while closing beads source-r2te / source-1q0c / source-bnky /
source-giip / source-emay / source-mfx0 / source-hzfi / source-vaov.

## The one rule that fixes most of rem_file

**Every REM_FILE_$* client pushes `move.w #0xbe,-(SP)` as `response_max` and
bases its reply buffer at `A6-0xC0`.**  So a reply field's record offset is

    offset = <A6 displacement> + 0xC0

`move.l (-0xb8,A6)` is response+0x08, `(-0xb4,A6)` is +0x0C, `(-0x80,A6)` is
+0x40, `(-0x4,A6)` is +0xBC.  The tree had assumed the buffer started at
A6-0xE4, which put every recovered field 0x24 bytes too far along and made
`REM_FILE_RESPONSE_BUF_SIZE` 0xE4 instead of 0xBE.

**The single exception is REM_FILE_$CREATE_TYPE (0x00E6171A)**: its frame is
`link.w A6,-0x1a0` and its buffer sits at **A6-0xE8**, so its offsets are
displacement + 0xE8.  Its length is still 0xBE.

The reply head is the same shape REM_FILE_$SERVER builds
(`rem_file_$response_t` in rem_file_internal.h): pkt_flag word, magic 0x80,
opcode = request opcode + 1, status longword at +0x04, payload from +0x08.

Recovered payload offsets (all displacement + 0xC0 unless noted):

| client | reply fields |
|---|---|
| TRUNCATE 0xE61976 | DTM long +0x08, word +0x0C; taken only when received_len == 0x10 |
| LOCK 0xE61AAE | ext: 36 longs from +0x0C, 8 longs from +0x9C, word +0xBC; plain: long +0x08, word +0x0C |
| UNLOCK 0xE61D1C | long +0x08, byte +0x0F |
| LOCAL_READ_LOCK 0xE61E9A | 8 longs + 1 word (34 bytes) from +0x08 |
| GET_SEG_MAP 0xE61F3E | long +0x08 |
| NEIGHBORS 0xE621B8 | result BYTE +0x08 |
| RESERVE 0xE62458 | reply opcode byte +0x03 |
| CREATE_AREA 0xE62622 | handle word +0x08, pkt size word +0x0A |
| ACL_IMAGE 0xE627A8 | len word +0x0A, 11 longs +0x0C |
| ACL_CREATE 0xE6283C | phase-1 UID +0x08, phase-2 UID +0x0C (they overlap - use a union) |
| ACL_SETIDS 0xE62930 | flag byte +0x0A, 9 longs +0x0C, 3 longs +0x30 |
| ACL_CHECK_RIGHTS 0xE629E8 | long +0x0C |
| FILE_SET_PROT / FILE_SET_ATTRIB | DTM long +0x40, word +0x44; taken when received_len == 0xBE |
| CREATE_TYPE (base 0xE8) | session UID +0x08, 36 longs +0x0C, 8 longs +0x9C |

## Request layouts the pass corrected

- **TRUNCATE**: flags is the **BYTE** at (0x14,A6) - AST_$TRUNCATE pushes it
  with `move.b D7b,-(SP)` at 0x00E06254.  Request: uid +0x04, flags byte
  +0x0C, new_size long +0x0E (only 2-aligned, so the record is packed),
  reserved +0x12, admin byte +0x14; length 0x16.
- **LOCK**: both requests put the lock **TYPE** at +0x14 and the **MODE** at
  +0x16.  Extended (0x84, 0xA2 bytes): key +0x0C, NODE_$ME +0x10, flags +0x18
  (bit 8 set for a subsystem caller), reserved 1 +0x1A, 8-longword location
  record +0x1C, ACL_$GET_EXSID output +0x3C, wait word +0xA0.  Plain (0x0A,
  0x22 bytes): admin byte +0x1A, reserved 1 at **+0x1E**.  Its two output
  words are the other way round from the old names: (0x1A,A6) gets
  response+0xBC and (0x1E,A6) gets the packet id.
- **CREATE_AREA**: area_offset (the caller's 4th argument) at +0x10 and
  area_size (the 3rd) at +0x18, with a dead longword at +0x14.  The `flags`
  word at (0x18,A6) is never read.
- **ACL_CREATE / CREATE_TYPE / CREATE_TYPE_PRESR10**: `pea (0x10,A2)` -
  SEND_REQUEST is handed the **address of** the 8-byte {network, node} record
  embedded at ctx+0x10, not a pointer stored there.  ACL_CREATE's phase-2
  session UID is at +0x40, with a dead longword at +0x3C after the 11-longword
  header at +0x10.
- **NAME_ADD_HARD_LINKU / DROP_HARD_LINKU**: only ONE longword of spaces is
  written (`move.l #0x20202020`), and the copy that follows moves exactly 32
  bytes (`moveq #0x1f,D1` / `dbf`) **without** clipping to name_len - name_len
  is only stored into the request.  add: name_len +0x2C, file_uid +0x2E
  (2-aligned, pack it), reserved +0x36, `st` byte +0x38, length 0x3A.
  drop: name_len +0x2C, flags +0x2E, reserved +0x30, `st` byte +0x32,
  length 0x34.
- **FILE_SET_ATTRIB** (0xAC): flags2 +0x0C, flags +0x0E, extra_flags +0x12,
  13 longs +0x14, 25 longs +0x48.
  **FILE_SET_PROT** (0xA8): flag byte +0x0C, flags +0x0E, 13 longs +0x10,
  25 longs +0x44.
- **LOCAL_READ_LOCK**'s two trailing clears are **unaligned longwords** at
  byte offsets 0x1A and 0x1E of the caller's record (`clr.l (0x1a,A3)` /
  `clr.l (0x1e,A3)`), and they run whatever the status was.

## Host-build trap: 2-byte alignment

The m68k ABI aligns longwords to **2**; a 64-bit host aligns them to 4.  Any
recovered record whose longword lands on a 2-aligned offset (rn_do_op's tail
at 0x8E, truncate's new_size at 0x0E, add_hard_linku's file_uid at 0x2E,
acl_setids' sid array at 0x10 after a word at 0x0C) compiles on m68k and then
fails its `_Static_assert` on the host.  Fix it with `__attribute__((packed))`
on the record (and on each inner struct of a union - packing the union alone
does not pack its members), never by moving the field.

Related: [[rem-file-opcodes]], [[rem-file-subsystem]], [[rem-file-name-get-entryu]].

## The request side: the A6-0x170 template (2026-09-08 pass)

Every simple `link.w A6,-0x178` client stub lays its frame out identically, so
one map covers PURIFY / ACL_IMAGE / GROW_AREA / DELETE_AREA / INVALIDATE /
SET_DEF_ACL / LOCAL_VERIFY / SET_ATTRIBUTE:

    A6-0x170  request record base   ("pea (-0x170,A6)", SEND_REQUEST arg 2)
    A6-0x176  received_len          (arg 8)
    A6-0x174  packet_id             (arg 12)
    A6-0x172  the ONE zero word     (`clr.w`), shared by args 4, 9 and 11
    A6-0xC0   reply buffer          (arg 6, 0xBE bytes)

So **a request field's record offset is its A6 displacement plus 0x170**
(`move.b #-0x80,(-0x16e,A6)` is request+0x02).

**REM_FILE_$ACL_IMAGE (0x00E627A8) is the one that breaks the pattern**: it
shifts everything down a slot and uses FOUR distinct cells - received_len at
A6-0x178, `bulk_len` at A6-0x176 (`pea (-0x176,A6)` at 0x00E627E8),
packet_id at A6-0x174 and the zero word at A6-0x172.  Aliasing bulk_len onto
the zero word (which the tree did) lets the transport's payload-length store
overwrite the extra-length argument.  It is also the only one whose arg 9 is
a real caller buffer (`move.l (0x12,A6)`, bulk max 0x400 at 0x00E627EC).

Declared request lengths (the literal word pushed as arg 3), now in
rem_file_internal.h as `REM_FILE_*_REQ_LEN`: SET_ATTRIBUTE 0x42, LOCAL_VERIFY
0x2E, PURIFY **0x14** (not 0x16), SET_DEF_ACL 0x20, INVALIDATE 0x16,
DELETE_AREA 0x1C, GROW_AREA 0x1C, ACL_IMAGE 0x14.  Several records are longer
than the bytes actually stored - GROW_AREA leaves +0x04..+0x0B, +0x10..+0x13
and +0x16..+0x17 unwritten, DELETE_AREA +0x04..+0x0F and +0x16..+0x1B,
ACL_IMAGE +0x0F..+0x13 - and the frame is never cleared, so the garbage goes
on the wire.  Model those as named `unset[]` members.

**Every one of these records needs `__attribute__((packed, aligned(2)))` plus
explicit padding and `_Static_assert`s.**  m68k-elf-gcc aligns longwords to 2,
so a naturally laid out struct still gets GROW_AREA's current_size at +0x0A
instead of +0x0C and SET_ATTRIBUTE's attr_data at +0x10 instead of +0x0E.

REM_FILE_$INVALIDATE's `flags` is a `boolean` (int8_t): `move.b (0x18,A6),D0b`
at 0x00E623EE reads the high byte of the word slot and 0x00E62412 stores one
byte at request+0x14.  Same shape as ACL_IMAGE's `acl_type` (0x00E627B6).

REM_FILE_$LOCAL_VERIFY copies the lock block TWICE: `lea (A0),A1` at
0x00E61E3E fills the UID at +0x04, then `lea (A0),A1` again at 0x00E61E48
restarts from the head of the same block for the 34-byte lock_info at +0x0C.
The trailing `move.w (A1)+,(A2)+` is a two-BYTE stream copy - write it as two
byte assignments, not a `uint16_t` index, or it flips on a little-endian host.
