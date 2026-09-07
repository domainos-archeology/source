---
name: module-base-inherited-a5
description: A function with no `lea (addr).l,A5` of its own inherits A5 from its callers - check them before calling an A5-relative cell "per-process data"; several TODOs were wrong about this
metadata:
  type: project
---

**A function that never loads A5 is not touching per-process data.**  It
inherits A5 from whoever called it, and in this kernel every caller of a
given routine loads the *same* module base.  Verified three times on
2026-09-07 while clearing TODOs that all claimed "A5-relative, needs the
per-process abstraction (source-0i3)":

| routine | A5 | inherited from |
|---|---|---|
| `REM_FILE_$SEND_REQUEST` 0x00E60FD8 | 0x00E823FC | REM_FILE_$RN_DO_OP 0x00E61540 and every other caller |
| `disk_$rtn_qblks_internal` 0x00E3C01A | 0x00E7A1CC | DISK_$RTN_QBLKS 0x00E3C0CA |
| `TTY_$I_WORD_ERASE` 0x00E1B716 | 0x00E2DDB4 | TTY_$I_RCV 0x00E1B932 |

**How to apply:** before writing a TODO about A5, run `gsk xrefs to <addr>`
and look for `lea (0x...).l,A5` in each caller.  If they agree, the cells are
plain module globals and can be emitted now.  Only PROC1_$AS_ID- or
PROC1_$CURRENT-indexed tables are actually per-process.

Two related traps found in the same pass:

- **Two halves of one longword written separately.**  `NETWORK_$ALLOWED_SERVICE`
  (0xE24C3E) is a longword whose high word is the service flags
  (`move.w ...,(0x342,A5)`) and whose low word is `NETWORK_$REMOTE_POOL`
  (`move.w D0w,(0x344,A5)`); `NETWORK_$READ_SERVICE` hands the whole longword
  out.  Modelling them as two variables broke the read, and modelling the
  flags as the *low* half inverted every mask.  Same shape as
  NETWORK_$CAPABLE_FLAGS = bits 16..23 of that longword.
- **32-bit VA cells four bytes apart.**  `void *` in a record or module block
  is eight bytes on the host and overruns the next field.  DMOD_RESERVE_BLOCK
  (0x0BC) / DMOD_FREE_HEAD (0x0C0) and DISK_QBLK_FREE_NEXT (0x08) /
  DISK_QBLK_STATUS (0x0C) are the disk instances; use `uint32_t` +
  ARCH_VA_TO_PTR and drive the test through an ARCH_HOST_VA_BASE arena.
  Bead source-xuue tracks the rest of disk/.

Related: [[domain-pascal-codegen-conventions]], [[feedback-fidelity-gates]].
