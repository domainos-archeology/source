---
name: pacct-log-record-stores
description: PACCT_$LOG's 0x80-byte record frame, its complete store list, and the MST_$MAPS byte-argument trap that bites every caller.
metadata:
  type: project
---

PACCT_$LOG (0x00E5AA9C) builds `pacct_record_t` in its frame at A6-0x190 and
copies 32 longwords out at 0x00E5ACEE, so **record offset = displacement +
0x190**.  The complete store list is in the header comment of
`domain_os/pacct/test/test_log.c`; offsets 0x03 and 0x74..0x7F are never
stored and leak stack (original behaviour).

**Why:** the record is the on-disk accounting format, so every offset has to
be pinned against the instruction that writes it, not against a guess.

**How to apply:**
- The 36-byte SID block comes from ACL_$GET_RE_ALL_SIDS' FIRST argument and
  the 12-byte protection block from its THIRD (0x00E5AB30 / 0x00E5AB42).
  Arguments 2 and 4 are written by the callee but never read back.
- `ac_devno` (0x34) is stored twice: cleared at 0x00E5AB50, then set at
  0x00E5AC5C from the zero-extended word at compact-record +0x32.
- Flag bits are `lsr.b #7` of the boolean byte (0x00E5AB06 / 0x00E5AB18), a
  LOGICAL shift.  `boolean` is a plain `char`, so `*p < 0` is
  host-signedness-dependent: mask with `(uint8_t)*p & 0x80`.
- **MST_$MAPS' second parameter is a BYTE**, read with `move.b (0xa,A6)` at
  0x00E43998; argument 8 likewise at 0x00E4399C.  Callers push them with
  `st -(SP)`, which lands 0xFF in the HIGH half of the word slot.  While
  `mst/mst.h` still declares argument 2 as `int16_t`, every call site must
  spell TRUE as `0xFF00` (the tree's convention - see hint/init.c,
  peb/load_wcs.c, log/init.c).  Tracked as bead source-qmdl.
- `cmpi.l #0x80,(0xc,A5)` / `bge` at 0x00E5AC66 is a SIGNED comparison of
  `buf_remaining`.

See [[feedback_fidelity_gates]] and [[pointer-warning-pass-notes]].
