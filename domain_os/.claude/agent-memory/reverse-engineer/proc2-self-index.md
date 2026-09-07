---
name: proc2-self-index
description: proc2_info_t+0x1C is self_index (the slot's own table index), not a session id; plus the PROC2 entry-init and UPID-allocator facts recovered with it.
metadata:
  type: project
---

`proc2_info_t` +0x1C is **`self_index`** — the entry's own 1-based index into the
process table. Settled by bead source-e8c8 (closed 2026-09-07); it used to be
called `owner_session`.

**Why:** PROC2_$INIT's free-list loop stamps every slot with its own index
(`0x00E304CA move.w D1w,(-0xc8,A0)`, A0 = entry(i)+0xE4, i = 2..70) and gives
entry 1 the value 1 (`0x00E30502`). PROC2_$DEBUG pushes the raw current index
for a callee argument (`0x00E41688`) where PROC2_$OVERRIDE_DEBUG pushes
entry+0x1C for the *same* argument (`0x00E41788`). PROC2_$CREATE seeds the
child's parent link +0x1E from the parent's +0x1C (`0x00E72928`).
PROC2_$INIT_ENTRY_INTERNAL (0x00E732E4) never writes it — slot identity must
survive entry reuse.

The old "session" reading came from PROC2_$SET_PGROUP; it is the POSIX setpgid
permission rule, not a session test: `0x00E41142/6` asks "is the target me?",
`0x00E4114C` asks "am I the target's parent?", and the real session comparison
is separate, on +0x5C, at `0x00E41178`.

**How to apply:** +0x1E is `parent_pgroup_idx` = the parent's table index; the
EC pair (`PROC_FORK_EC`/`PROC_CR_REC_EC`) is legitimately indexed by +0x1C
because that *is* the index.

Other facts recovered in the same pass:
- 0xE7C06A (`PROC2_$NEXT_UPID`, = 0xE7BE84 + 0x1E6) is the rolling UPID allocator,
  **initialised data** in the image (0x0041). Wraps 30000 -> 0x41
  (0x00E73334/0x00E7333C). A candidate is rejected if any entry on the
  allocated list matches it on +0x16 (upid), +0x5C (session_id) or, when the
  candidate names a live group, +0x10 (pgroup index) — UPIDs, session ids and
  pgids share one number space.
- `PROC2_$UID` (0xE7BE94) is indexed by **ASID** (entry+0x96), not by table index
  (0x00E73308).
- `FIM_$INIT_ASID` (0x00E0AA24) takes the **address** of the pid word;
  PROC2_$INIT_ENTRY_INTERNAL passes `&entry->asid`.
- 0x00E4216E renamed `PROC2_$PGROUP_INHERIT_INTERNAL(from, to)` — bumps the source
  group's ref_count then copies +0x10.
- `andi.b`/`or.b` at 0x00E73400/0x00E73408 hit the HIGH byte of the flags word
  at entry+0x2A, so the propagated bit is **0x0200**, not 0x02.

See [[dxm-proc2-mst-notes]] and [[mm-proc-nested-procedures]].
