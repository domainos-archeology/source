---
name: mm-proc-nested-procedures
description: The nested-Pascal-procedure sites in PMAP/AST/MMAP/PROC1/PROC2 that read a parent frame through A1 or (A6), plus the ast_$allocate_pages ABI and the shared PROC1 release tail.
metadata:
  type: project
---

Recovered 2026-09-07 while clearing the TODO markers in ast/pmap/mst/mmap/
mmu/area/proc1/proc2/log/osinfo/ec/ml/dxm.  See [[feedback_fidelity_gates]].

## The static link arrives in A1, not only in (A6)

A nested procedure called from the ENCLOSING body reads its parent frame
with `movea.l (A6),Ax` (the dynamic link happens to be the parent).  A
nested procedure called from a SIBLING nested procedure gets the parent
frame explicitly in **A1** and does `movea.l A1,Ax` at entry.  Sites found:

- `pmap_$update_seg_map` 0xE1359C -- A1 from `pmap_$flush_write_batch`
  (`movea.l A4,A1` at 0xE13718) and from PMAP_$FLUSH itself
  (`movea.l A6,A1` at 0xE13928).  A2+0x08 = PMAP_$FLUSH's `aste`,
  A2+0x15 = the LOW byte of its `flags` word.  It is NOT an ASTE-taking
  routine, despite the old comment.
- `ast_$count_valid_pages` 0xE0305C -- (A6); A2+0x14 = AST_$TOUCH's
  ppn_array, +0x18 status, +0x1D the low byte of its flags word.  Its own
  first argument is a SEGMENT-MAP pointer.
- `mmap_$move_pages_to_wsl_type` 0xE0D274 -- (A6); A0+0x08 =
  MMAP_$WS_SCAN's wsl_index word, A0+0x0A its mode boolean.  Together they
  produce the byte stamped into `mmape->priority`.
- The three AST NETLOG helpers, all (A6):
  `ast_$update_aste_log` 0xE01502 (counts bit-31 longwords in the parent's
  32-entry disk image at A6-0x88, kind 12),
  `ast_$read_area_pages_net_log` 0xE02C52 (kind 8/9 by the zero flag),
  `ast_$deactivate_segment_log` 0xE01872 (kind 1; its own argument is the
  segment-map ROW address, `pea (-0x80,A3)` with A3 = 0xED5000+seg*0x80).
- `proc2_$suspend_try` 0xE4120C -- (A6); writes PROC2_$SUSPEND's status,
  index and suspend_result slots.

**How to apply:** when a Ghidra "function" starts `movea.l A1,Ax` or
`movea.l (A6),Ax` and then uses POSITIVE displacements off Ax, those are
the *parent's arguments*.  Flatten by adding them as explicit parameters
and threading them through every intermediate frame.

## ast_$allocate_pages (0xE00D46) -- three words, not a packed longword

`(0x8,A6)` requested count (D2, decremented), `(0xa,A6)` minimum count
(compared at 0xE00E56 to decide whether to wake the purifier and retry),
`(0xc,A6)` array.  Every caller pushes `(count, 1, array)` except
WP_$CALLOC_LIST which pushes `(count, count, array)` and WP_$CALLOC
`(1, 1, buf)`.  The old `count_flags` model read the halves backwards and
asked for one page instead of N (bead source-4z9a).

## 0xE20EB6 is PROC1's tail, shared by three entry points

`proc1_$release_tail()` now lives in `proc1/proc1.h` (it was
`ml_$release_tail` in ml/ml_internal.h).  ML_$UNLOCK, ML_$EXCLUSION_STOP
**and PROC1_$INHIBIT_END** all branch into it; 0xE20EA2 is simultaneously
PROC1_$INHIBIT_END's entry and ML_$EXCLUSION_STOP's no-waiter block, which
is why Ghidra reports INHIBIT_END as only 14 bytes.

## PROC1_$RESUME / PROC1_$SUSPEND IPL discipline

RESUME raises at 0xE147AC and forces IPL 0 at 0xE147EE, but the
*suspended* path returns at 0xE147D6 still at IPL 7 -- PROC1_$DISPATCH
lowers it.  SUSPEND raises at 0xE14850 and never lowers it at all.  Both
re-read the flag byte at PCB+0x55 after each call; nothing is cached
across the raise.

## proc1_$process_exit_handler (0xE20ACC)

The return address INIT_STACK pushes under the entry point.  22 bytes:
`move #0x2000,SR` / `movea.l (A7)+,A0` / `jsr (A0)` and, if the body
returns, a 4-byte status slot + `PROC1_$UNBIND(PROC1_$CURRENT, &status)` +
`trap #15`.  Emitted in `proc1/sau2/init_stack.s`.

## Segment-table storage at 0xEC5400 is 1-based aste_t[]

Both `mmap_$trim_wsl` (0xE0C8C0) and `AREA_$DEACTIVATE_ASTE` (0xE0A096)
form `0xEC5400 + seg*0x14` and then use a negative displacement into the
previous record, so record(seg) = 0xEC5400 + (seg-1)*sizeof(aste_t).
`MMAP_$SEG_ASTE_FOR(seg)` in mmap/mmap_internal.h is the typed accessor;
the old `void *SEGMENT_TABLE[]` view is wrong (bead source-uxu3).
