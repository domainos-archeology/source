---
name: aliased-cells-and-nested-procs
description: Recurring fidelity bugs found across subsystems - one image cell modelled as two C objects, nested Pascal procedures reaching the parent frame, and dbf loop counts that overrun a declared array
metadata:
  type: project
---

Three defect *classes* that keep turning up in this tree, with the confirmed
examples.  **Why:** each one produces code that compiles and passes tests while
disagreeing with the image, so they are invisible without an explicit check.
**How to apply:** run these checks whenever emitting or auditing a subsystem.

## 1. One image cell, two C objects

Symptom: a header declares `extern T NAME;` *and* the same storage appears as a
field of a module block, so the m68k build has two cells.  Check: does the SAU2
map put the symbol at `<module block> + <field offset>`?

Confirmed and fixed:
- `FILE_$LOT_HASHTAB` == `FILE_$LOCK_CONTROL.lock_map` (map: `E821F0
  FILE_$LOT_HASHTAB`, block at `E82128`, so +0xC8).  Same for
  `FILE_$LOT_FREE` (+0x2CE, map-confirmed at `E823F6`), `FILE_$LOT_HIGH`
  (+0x2CC), `FILE_$LOT_SEQN` (+0x2C4), `FILE_$DEFAULT_SIZE` (+0x2C0),
  `FILE_$LOCK_ILLEGAL_MASK` (+0x2C8), `FILE_$LOT_PENDING` (+0x2CA),
  `FILE_$LOT_FULL` (+0x2D0).  All are now macros over the struct.
- `pmap` `DAT_00e23366/6c/7c/80` were aliases for
  `MMAP_WSL[MMAP_WSL_POOL_WIRED]`'s owner/scan_pos/pri_timestamp/ws_timestamp.
- `volx` had two function-local `static uint32_t unused_param` where the image
  has one cell at 0x00E6B504, passed by both VOLX_$DISMOUNT (`pea (0x5a,PC)`
  @0xE6B4A8) and VOLX_$SHUTDOWN (`pea (-0x46,PC)` @0xE6B548).

## 2. Nested Pascal procedures

`bsr.w` with nothing pushed + `movea.l (A6),A2` == a nested procedure.  Flatten
to a static in the parent's file with every uplevel reference as a parameter.
Watch for uplevel reads of locals the parent has *not yet written* - reproduce
that rather than substituting a plausible value.

- `audit_$clear_hash_table` (0xE7128A) forwards `audit_$load_list`'s status_ret
  from (0x8,A2).
- `route_$close_port` (0xE69EC2) reads ROUTE_$SERVICE's port_info (0xC,A2),
  status_ret (0x10,A2), short_port local (-0x48,A2) AND old_status (-0x62,A2),
  which the parent first writes only at 0x00E6A448 - i.e. garbage on this path.

## 3. dbf counts that overrun the declared array

`moveq #N,Dn` + `dbf` is N+1 iterations.  Compare against the modulus/mask the
*lookup* path uses; they often differ by one.

- `HINT_$clear_hintfile` (0xE31194) clears 65 buckets (`moveq #0x40`) while
  `HINT_$add_internal` masks with `andi.w #0x3f` (0..63).  hint_file_t is
  therefore 65 buckets, 0x1560 bytes, not 0x150C.
- `audit_$clear_hash_table` clears bucket slots 1..37 (first store is A5+0xB4,
  A0 = A5+4) while UID_$HASH's remainder gives 0..36 - slot 0 is never
  cleared and slot 37 never used.  An original off-by-one; preserved.

See also [[handwritten-asm-verification]], [[obj-loc-record]].
