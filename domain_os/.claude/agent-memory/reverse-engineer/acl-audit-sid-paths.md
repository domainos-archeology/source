---
name: acl-audit-sid-paths
description: "The ACL SID-setting audit snapshot/log pattern, the AUDIT_$LOG_EVENT_S argument shape, and the ACL_$DATA / ACL_$INIT / ACL_$FREE_ASID constants recovered 2026-09-07"
metadata:
  type: project
---

## The ACL "snapshot / act / log" pattern

`ACL_$SET_RE_ALL_SIDS` (0xE481AE), `ACL_$SET_RES_ALL_SIDS` (0xE4855A) and
`ACL_$ENTER_SUBS` (0xE46DA0) all have the same three-part shape, and it is
easy to drop the two audit parts when reading only the decompiler:

1. **Head** - `tst.b (0x00e2e09e).l` / `bpl` over a block that sets a flag
   *word* to 1 and copies the pre-state SID blocks into frame locals.
2. **Body** - every permission refusal is a `bne.w`/`bpl.w` to the *audit
   tail*, never to the epilogue.  A refused call still logs.
3. **Tail** - `tst.b AUDIT_$ENABLED` again; return early only when nothing
   changed **and** `*status == 0`; otherwise copy the post-state blocks and
   call `AUDIT_$LOG_EVENT_S`.

**`AUDIT_$LOG_EVENT_S` (0xE70E40) argument order** (right-to-left pushes, so
the LAST push is argument 1):
`(uid_t *event_uid, uint16_t *flag, void *sid, status_$t *status, char *data,
const uint16_t *data_len)`.  `sid` is a pointer *into* the `data` block - the
36-byte "current SIDs on entry" element.  `data_len` is always a `pea (d,PC)`
word cell.

| caller | event UID | data | len cell |
|---|---|---|---|
| SET_RE_ALL_SIDS | AUDIT_$SET_SID_EU 0xE85668 = {0x00040007,0} | 4 SID blocks, A6-0x90 | 0x0090 at 0xE48558 |
| SET_RES_ALL_SIDS | same | 6 SID blocks, A6-0xD8 | 0x00D8 at 0xE48790 |
| ENTER_SUBS | AUDIT_$ENTER_SUBS_EU 0xE85660 = {0x00040008,0} | 1 SID block, A6-0x50 | 0x0024 at 0xE46F8E |

The frame locals are contiguous 36-byte blocks, so they model cleanly as one
record (`acl_$set_re_sids_audit_t`, `acl_$set_res_sids_audit_t` in
acl/acl_internal.h).  The flag word is 1 = attempt, 0 = success.

`ACL_$ENTER_SUBS` is still **not emitted** (bead source-m5y2) even though
acl/acl.h declares it and svc/svc_tables.c puts it in SVC slot 0x3C.

## ACL_$DATA and ACL_$INIT (0xE3109C)

- The SAU2 map has `D69 E88834 ACL_$DATA size = AD98`, and `ACL_$INIT` zeroes
  exactly `0xE935CC - 0xE88834`.  The C objects inside it **overlap** -
  ACL_$ACL_CACHE's 31st 0x400 slot covers `ACL_$ORIGINAL_SIDS[0]`, never used
  because PIDs start at 1 - so the length can never be a sum of `sizeof`s.
  `ACL_DATA_BASE/END/SIZE` in acl/acl_internal.h carry it.
- 0xE9044C / 0xE90D4C are `ACL_$ORIGINAL_SIDS[1].login_sid` and
  `ACL_$CURRENT_SIDS[1].login_sid` (offset 0x3C = 1*0x24 + 0x18), NOT
  `[0].user_sid` / `[1].user_sid`.  The UID copied is
  RGYC_$G_LOCKSMITH_UID (0xE17434 = {0x542,0}, also RGYC_$P_ROOT_UID).
- The `divs.w #0x1f` loop at 0xE31140 builds the **free-list ring** in
  `ACL_$CACHE_HASH_LINKS` (A5+0xA70), entries 0..30:
  `next = (i+1) % 31`, `prev = (i+30) % 31`.  Slot 31 is left alone.

## Constants recovered

- `ACL_$FREE_ASID`'s `lea (0xb8,PC),A3` (0xE74CE2) -> **0xE74D9C** =
  `0000000D 0000000D 0000000D`, the default `acl_proj_list_t`, not zeros.
- `AUDIT_$INIT`'s three `pea (d,PC)` VFMT format cells:
  0xE70C40 `"%/%/%/%/Warning, could not start audit trail: 0x%8zulh%."`,
  0xE70C24 `"All events will be logged.%."`,
  0xE70BEA `"Only audit administrators will be allowed to login.%."`,
  plus the longword-zero argument terminator at **0xE70C20** (pushed once by
  call 1 and twice by calls 2 and 3, the second copy via `move.l (SP),-(SP)`).
  Watch the boundary: the warning string starts 8 bytes *before* "Warning",
  at the four `%/`s.
- `AUDIT_$INIT` 0xE70BDC calls **ACL_$ENTER_SUPER a second time**, not
  ACL_$EXIT_SUPER.  Reproduce it; do not "fix" it.

## AUDIT_$SERVER (0xE710C6)

`EC_$WAITN` (0xE2063E) gets `ecs[] = {AUDIT_$DATA.event_count, &TIME_$CLOCKH
(0xE2B0D4)}` at A6-0x20 and `values[] = {*ec + 1, timeout}` at A6-0x10; the
two arrays are 0x10 bytes apart, so the Pascal source probably declared four
slots each.  `AUDIT_$DATA.dirty` (A5+0x9C) is read with `tst.b` + `bpl`, so it
is a **signed** Domain boolean - it was `uint8_t` in the tree, which made the
`< 0` flush test dead code.

See [[acl-rights-abi]], [[acl-image-cache]], [[feedback_fidelity_gates]].
