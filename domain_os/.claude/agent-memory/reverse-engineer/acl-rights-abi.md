---
name: acl-rights-abi
description: "ACL_$RIGHTS (0xE46A00) / acl_$eval_rights (0xE464B8) ABI, the ignore_super boolean pointer, and the ACL per-process table bases - recovered 2026-09-06 for bead source-6vl5"
metadata:
  type: project
---

**ACL_$RIGHTS (0x00E46A00)** is a five-argument gate in front of
**acl_$eval_rights (0x00E464B8)** (was FUN_00e464b8; renamed in Ghidra):

```
uint32_t ACL_$RIGHTS(uid_t *uid, boolean *ignore_super, uint32_t *required_mask,
                     int16_t *option_flags, status_$t *status);
```

- **Every one of the four pointer arguments is dereferenced** — NULL faults.
  `ignore_super` is a pointer to a Domain **boolean**: `movea.l (0xc,A6),A3` +
  `move.b (A3),-(SP)` at 0xE46A50-54. TRUE (0xFF) **suppresses the super-user
  bypass** that acl_$eval_rights would otherwise take at 0xE464DC.
- `required_mask` is read as a **longword** (`move.l (A2)`, 0xE46A4E);
  `option_flags` as a **word** (`move.w (A1)`, 0xE46A48). Option flags 1 =
  directory object type.
- Result is the **full longword** left in D0 by acl_$eval_rights — there is no
  stack cleanup after the `bsr` (the `unlk` drops the 0x1C bytes), so D0 falls
  straight through. Callers use `tst.l D0` (0xE73D4C) and `cmpi.l #0x2,D0`
  (0xE71504), so an `int16_t` return truncates.

**acl_$eval_rights frame** (nine args): 0x08 sids, 0x0c proj_uids, 0x10 uid,
0x14 ignore_super (byte), 0x16 required_mask (long), 0x1a option_flags (word),
0x1c in_super (byte), 0x1e in_subsys (byte), 0x20 status. ACL_$RIGHTS_CHECK
(0xE46AEC) fills the same frame but passes `clr.w` (FALSE) for ignore_super and
the caller's byte for in_subsys.

**Pushed booleans land on the EVEN byte of their 2-byte stack slot** in this
image — the callee reads `move.b (0x14,A6)`, `(0x1c,A6)`, `(0x1e,A6)`. Useful
whenever you need to decide which half of a slot a `move.b …,-(SP)` filled.

**ACL per-process tables** (index = PROC1_$CURRENT at 0xE20608, reloaded for
each table):
- `ACL_$CURRENT_SIDS` 0xE90D10, stride 0x24
- project UIDs: the row is at **0xE924FC + cur*0x40**, i.e. `&ACL_$PROJ_UIDS[cur][1]`
  given the **1-biased base 0xE924F4** recorded in acl/acl_internal.h. Pascal
  indexes these 1..8 (`ACL_$ADD_PROJ` 0xE47F00 `lea (0,A1,D2)` with D2 starting
  at 8, `lea (-0x4da0,A0)`), so the existing 0-based C in add_proj/get_proj_list/
  delete_proj/set_proj_list is shifted 8 bytes low — unverified, worth a bead.
- `ACL_$SUPER_COUNT` 0xE7DACA (A5+0xB76), `ACL_$SUBSYS_LEVEL` 0xE9353A; both
  turned into booleans with `tst.w` + `sgt` (SIGNED strictly-greater-than-zero).

**FILE_$CHECK_PROT (0xE5D172)** A6+0x12 and A6+0x14 are **two 2-byte
parameters** (ignore_super boolean, option-flags word), each passed to
ACL_$RIGHTS by reference; they were one bogus `void *unused`. Both callers fill
them with a single `clr.l -(SP)` (0xE73FFC, 0xE43DAC), which is why the split is
invisible from the call sites.

**dir_$do_op_cname (0xE518BC)** calls ACL_$RIGHTS at **0xE51A1E**, inside a
jump-table arm (0xE51A0C-0xE51A24) that Ghidra has not disassembled — so it
does NOT appear in ACL_$RIGHTS' xref list. When an emitted C file has a call the
xrefs do not show, read the raw bytes of the switch arms before concluding the C
invented it.

See [[feedback_fidelity_gates]] and [[name-dir-subsystems]].
