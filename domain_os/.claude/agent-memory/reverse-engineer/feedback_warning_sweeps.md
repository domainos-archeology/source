---
name: warning-sweeps-are-frame-model-audits
description: A -Wincompatible-pointer-types / -Wint-conversion warning in this tree is almost always a wrong recovered frame model, not a typing nit - fix the model, never the cast
metadata:
  type: feedback
---

When asked to clear pointer-type warnings in domain_os, treat each one as a
frame-model bug and re-derive the callee's ABI from `gsk analyze`, rather than
adjusting a cast until the compiler is quiet.

**Why:** in the 2026-09-07 sweep every one of the 47 warnings sat on a real
fidelity defect - undersized stack records the callee overruns, a value passed
where the machine code pea's a pointer, an *address* constant (`0x00E85648`,
`0x00E8029C`) rendered as the integer literal `0x5648` / `0x29C`, NULL passed
where the compiler pea's a named constant cell, and two structs whose C field
offsets did not match the image at all.  A cast would have hidden all of them.

**How to apply:** for each warning, `gsk analyze` the callee's prologue for the
real widths, then `gsk analyze` the caller and read its pushes right-to-left.
Only keep a cast when it expresses exactly what the machine code does (passing
a record base where the prototype wants its first member, say) and say so in a
comment naming the address.  Resolve `pea (d,PC)` as *instruction address + 2 +
d* and `gsk read` the target before naming it.  Expect fixing one prototype to
surface new warnings at other call sites - that is the sweep working.
