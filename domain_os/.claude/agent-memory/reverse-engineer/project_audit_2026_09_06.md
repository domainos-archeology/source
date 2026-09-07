---
name: project-audit-2026-09-06
description: The 2026-09-06 C-fidelity audit is the driver for the current re-emission work; its systemic failure modes are the review checklist
metadata:
  type: project
---

`docs/audit-2026-09-06.md` scored 20 decompiled functions against the Ghidra disassembly
(median fidelity 1.5/5) and filed a bead per function. Work is being parcelled out by
subsystem ownership, several agents at once, so the tree often has unrelated build breakage
while you work.

**Why:** the audit exists because the generated C had drifted into plausible-looking code that
did not match the machine code — the goal of the project is preservation, so a wrong-but-tidy
translation is worse than an incomplete one.

**How to apply:** treat the audit's "Systemic failure modes" list (value-vs-reference Pascal
`var` params, `uint8_t` booleans, byte ops on the high byte of a word, struct layouts, A6
displacements used as struct offsets, prose ellipses, uplevel variables copied by value,
by-value arrays, forced-IPL-0 vs SR restore, optimizer-deleted MMIO/delay loops, wrong half of
`status_$t`, 1-based Pascal arrays) as the checklist for every function you touch, and check
the project memory note `domain-pascal-codegen-conventions` for the codegen idioms behind them.
When the full-tree build fails, confirm the failing file is not one of yours before chasing it.
Related: [[feedback-fidelity-gates]].
