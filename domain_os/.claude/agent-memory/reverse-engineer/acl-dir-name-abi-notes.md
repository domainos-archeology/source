---
name: acl-dir-name-abi-notes
description: "ACL cache/prot-record layouts, the ACL_$PROJ_UIDS base, DIR_$DO_OP's fifth argument, and the delete-right arithmetic shared by dir_$do_op_delete and NAME_$OLD_DELETE_ENTRYU - recovered 2026-09-07"
metadata:
  type: project
---

**ACL per-process tables** (index = PROC1_$CURRENT, which is 1-based; row 0 is
never used and other data lives inside it):

- `ACL_$PROJ_UIDS` base is **0xE924FC**, 0-based `[pid][0..7]`, stride 0x40.
  Proof: `ACL_$INIT` 0x00E31122-0x00E3113C stores UID_$NIL to eight slots from
  0xE9253C = 0xE924FC + 1*0x40. All four proj functions and ACL_$RIGHTS use the
  same `-0x4da0` bias with a displacement that starts at 8. `ACL_$ASID_FREE_BITMAP`
  at 0xE92534 sits harmlessly inside the unused row 0.
- **0xE935BC is ACL_$LOCKSMITH_OVERRIDE_BITMAP**, not a lock table:
  ACL_$OVERRIDE_LOCAL_LOCKSMITH writes it at 0x00E492BC. 0xE935C4 is
  ACL_$ASID_SUSER_BITMAP. FILE_$PRIV_LOCK's lock table starts at **0xE935CC**
  (stride 0x1C, `movea.l #0xe935cc,A2` at 0x00E5EBCA) - file/check_prot.c and
  file/export_lk.c still say 0xE935BC (bead source-xi4k).
- All three bitmaps: byte `(pid-1)>>3`, mask `0x80 >> ((pid-1)&7)`.
- Ghidra labels: **0xE17444 is ACL_$FILE_ACL and 0xE1744C is ACL_$DIR_ACL**
  (acl_data.c had FILE_ACL at 0xE1744C).

**ACL record layouts** (now in acl/acl.h and acl/acl_internal.h):
- `ast_$acl_attr_t.acl_data[44]` is `acl_$prot_data_t`: owner/group/org UIDs at
  0/8/0x10, then owner/group/org/world/subsys rights bytes at 0x18..0x1C. It is
  also REM_FILE_$SET_ACL's `acl_header` (11 longwords).
- `ast_$acl_attr_t.obj_flags[]`: [0] = protection present, [1] = object type,
  [3] bit 0 = "the ACL is held locally".
- The ACL image cache is **31 slots of 0x400 at 0xE88834** (ACL_$INIT's
  `divs.w #0x1f` free list, 0x00E31140); the last slot ends exactly where
  ACL_$ORIGINAL_SIDS[1] begins. Fields: type_uid +0x02, entry_count +0x0E,
  required_uid +0x12, subsys_uid +0x1A, entries +0x34. Needs `packed`.

**DIR_$DO_OP's fifth argument is `uint16_t *received_len`** (bead source-32ld),
not the request buffer. Local path writes it itself at 0x00E4C24E; remote path
hands it to REM_FILE_$RN_DO_OP arg 6 (0x00E4C104), which hands it to
REM_FILE_$SEND_REQUEST arg 8 (0x00E616E4), written at 0x00E61288. Callers pass
a 2-byte frame cell that is usually - but not always - two bytes below the
request.

**The "delete right" arithmetic** appears verbatim in both `dir_$do_op_delete`
(0x00E51482) and `NAME_$OLD_DELETE_ENTRYU` (0x00E56C9E):
`move.b flag,D7 / not.b D7 / btst.l #3,rights / seq / or.b / bpl` then
`btst.l #6,rights`. In C: `if (flag >= 0 || (rights & 8) == 0) { if (rights & 0x40) refuse; }`.

**file_$obj_loc_t +0x02 is `.volume`**: FILE_$CHECK_SAME_VOLUME (0x00E5E578),
name_$old_add_link (0x00E568CC) and dir_$do_op_delete (0x00E5138C) all compare
that word to decide "same volume"; DIR_$VALIDATE_HANDLE caches it at
handle+0x3A (0x00E4B566). +0x04 is FILE_$PRIV_CREATE's VTOC block hint.

**`file_$op_cannot_perform_here` is 0x000F000B**, not 0x000F0018 (which is not a
code the SR10.4 database defines); FILE_$FORCE_UNLOCK stores it at 0x00E60DFE.

**`lock_verify_request_t` +0x12 is the LOCK MODE**, and the 12-entry table at
FILE_$LOCK_CONTROL+0x40 is a lock-mode canonicalisation table
(`FILE_$LOCK_MODE_MAP`), not an ASID map - FILE_$LOCAL_LOCK_VERIFY indexes it
with the *entry's* mode at 0x00E608C8.

**`__A5_BASE()` in a host test**: arch/host/arch.h defines it as a `static inline`
returning NULL, so a test that needs a real A5 area must `#undef __A5_BASE` and
define a macro **after** all header includes but before `#include "../x.c"`.

See [[acl-rights-abi]] and [[feedback_fidelity_gates]].
