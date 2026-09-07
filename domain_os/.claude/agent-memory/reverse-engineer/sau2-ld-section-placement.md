---
name: sau2-ld-section-placement
description: How sau2.ld reproduces the image's code/data adjacency so `lea (sym:w,%pc)` R_68K_PC16 relocations do not overflow, and the ld gotchas around counting them.
metadata:
  type: project
---

Faithful `lea (SYM:w,%pc)` emission (byte fidelity) creates R_68K_PC16
relocations that only link if the target is within +/-32KB of the code. The
image's segments put those tables a few hundred bytes past their dispatchers;
`sau2.ld`'s default `*(.text .text.*)` / `*(.data .data.*)` split puts them
~0x11000 apart, so they overflow.

**Why:** the archivist rule forbids changing the instruction form back to
absolute `lea`, so the adjacency has to be reproduced at link time instead.

**How to apply:** at the definition site give the table a dedicated section
(svc does this with `SVC_TABLE_SECTION` in `svc/svc_internal.h` ->
`.text.svc_tables`, guarded `#if defined(ARCH_M68K)` because the host build has
no such section names); then in `sau2.ld`'s `.text` name the code objects and
that section explicitly, *before* the `*(.text .text.*)` gather. ld assigns
each input section to the **first** matching output-section statement, so order
in the script is what does the work. Object patterns match the path as spelled
on the link command line and `*` crosses `/`, so `*svc/sau2/trap0.o(.text)`
survives a BUILD_DIR change. Where the target is a Pascal module data block
pinned at an A5 address, use source-0i3's `.moddata.<name>` scheme instead.

Two counting traps:

- **GNU ld prints at most 10 relocation overflows tree-wide**, then
  "additional relocation overflows omitted from the output". A `grep -c` of a
  failing build is a floor, not a count. Fix one subsystem and re-run to see
  the rest.
- **A compile error anywhere makes the count read 0** because the link never
  runs. Check `grep -c LINK` in the log before believing a zero.

See [[feedback_shared_worktree]]: other agents edit the same tree, so gate a
link-order change against `git archive HEAD` plus your own files copied over it,
built in a scratch directory - otherwise their in-flight compile errors mask
your result.
