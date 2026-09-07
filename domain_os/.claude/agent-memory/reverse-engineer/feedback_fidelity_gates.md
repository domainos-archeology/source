---
name: feedback-fidelity-gates
description: Required gates and archivist rules when re-emitting a decompiled function (build with -Werror, full-tree error scan, make test 0 failed, no commit unless asked)
metadata:
  type: feedback
---

Before reporting on any re-emitted function, run and quote three gates:

1. `make -j8 <subsystems>` — default CFLAGS carry `-Werror`, so this must be silent.
2. `make -k -j8 2>&1 | grep error` — confirm nothing in *your* files fails; other agents
   often have in-flight breakage elsewhere in the tree, and that is not yours to fix.
3. `make test` — must end "N tests, 0 failed".

**Why:** the 2026-09-06 fidelity audit (docs/audit-2026-09-06.md) found a median fidelity of
1.5/5 across 20 sampled functions; the recurring causes were unverified claims and tests that
exercised a local re-implementation rather than the real function. The gates are how a claim
of "done" is made checkable.

**How to apply:** also honour the archivist rules the audit turned into process — every basic
block accounted for (no prose ellipses; anything unfinished gets a `TODO:` *and* a `bd create`
P2 referencing it), `_Static_assert`s on every recovered struct layout under
`#if defined(ARCH_M68K)`, Domain booleans typed `boolean`/`int8_t` and tested with `< 0`,
`pea (d,PC)` constants turned into named file-statics with the address in a comment, and unit
tests that `#include` the real .c and call the real function through mocks. Do not `git commit`
or `git push` unless explicitly asked. Related: [[project-audit-2026-09-06]].
