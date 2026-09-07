---
name: rem-file-name-get-entryu
description: REM_FILE_$NAME_GET_ENTRYU's frame map, and the in-place request-filling pattern shared by the rem_file client stubs
metadata:
  type: project
---

REM_FILE_$NAME_GET_ENTRYU (0x00E6209A, `link.w A6,-0x1a0`) fills its request
**in place**: ACL_$GET_RE_SIDS' second output is request+0x34 (0x00E62112),
ACL_$GET_PROJ_LIST writes request+0x58 and request+0x98 (0x00E62130), and
nothing is staged in locals and copied in afterwards.  Request 0xA2 bytes at
A6-0x198, reply 0xBE bytes at A6-0xE8, GET_RE_SIDS' *first* output the
36-byte local at A6-0x28 (source-fqv4).

**Why:** the earlier transcription invented staging locals and a copy loop,
which produced the same bytes but the wrong frame model, so the offsets could
not be asserted.

**How to apply:**
- In these client stubs the request record is the frame; the ACL/attribute
  helpers are always handed pointers *into* it.  Derive each field offset as
  `A6-displacement` minus the request base before writing any C.
- Two trailing longwords land at 0x9A and 0x9E - only 2-aligned - so model
  them as word pairs rather than packing the whole record; packing makes
  `&proj_list` / `&proj_count` raise `-Waddress-of-packed-member` and the
  build gate uses `-Werror`.
- The 32-byte name copy at 0x00E620DC is unconditional (`moveq #0x1f,D1`),
  **not** clipped to the caller's `name_len`; `name_len` is only stored into
  request+0x2C.
- The caller's result record is packed: uid at +0x02, extra longword at +0x0A.
- One `clr.w` word (A6-0x19A) is passed to REM_FILE_$SEND_REQUEST three times
  (extra_data, bulk_data, bulk_len).
- Host tests must assert *named fields*, never wire bytes: the host is
  little-endian.  Include the unit under test **before** the mocks so the
  recovered record types are in scope for the mock signatures.
