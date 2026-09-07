---
name: do_op-case-argument-order
description: DIR_$DO_OP case blocks: how the pea order maps to parameters, and the DIR_$X vs dir_$do_op_x client/server twin trap.
metadata:
  type: feedback
---

# DIR_$DO_OP case blocks: re-derive the argument order from the pushes

Rule: in a `DIR_$DO_OP` (0x00E4C02C) case block, the **first** `pea` executed
is the **last** parameter; the last `pea` before the `bsr` is parameter 1.
Several cases had been emitted with the middle two arguments swapped because
they were read top-to-bottom.

**Why:** the case blocks all look like `pea (0x4,A3) / pea (X,A2) / pea (Y,A2)
/ pea (-0x10,A6) / bsr.w`, so a top-to-bottom reading is plausible and silent -
both arguments are `void *` into the same request buffer, so nothing fails to
compile and no test catches it unless the test compares pointers.

**How to apply:** for every case, read the callee's own `link`/`(0xN,A6)`
loads to fix parameter positions, then check the `lea (N,SP),SP` after the
`bsr` matches the pushed byte count. Confirmed 2026-09-07 for
0x4C (0xE4C794 -> 0xE52FA6, param_2 = req+0x8E, param_3 = req+0x96) and
0x54 (0xE4C81C -> 0xE52044, param_2 = req+0x8E, param_3 = req+0x96,
param_4 = req+0xC2). The case 0x52 audit call (0xE4C806) joins the case 0x54
audit call at a **shared tail** (0xE4C854), so its argument list is split
across two blocks: it passes `&ACL_$DIRIN_ACL` (0xE1745C) as the 4th argument
and the *word* at req+0xC2 as the 6th, where case 0x54 passes literal 4.

**Client/server twins:** every server handler `dir_$do_op_x` has a client-side
request builder `DIR_$X` a few hundred bytes away (e.g. dir_$do_op_set_acl
0xE52BC2 vs DIR_$SET_ACL 0xE52C86). Naming the wrong one in a DO_OP case makes
the server re-send the request. Always resolve the `bsr` target address with
`gsk analyze` rather than trusting a name that "reads right".
