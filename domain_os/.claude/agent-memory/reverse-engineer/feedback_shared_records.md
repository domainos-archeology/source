---
name: shared-record-ownership
description: When a record crosses subsystems, put ONE definition in the owner's public header and convert every user - duplicated "corrected" copies in an internal header are what drift.
metadata:
  type: feedback
---

When the same on-wire or on-stack record is used by more than one subsystem,
emit exactly one definition, in the *owning* subsystem's public header, and
convert every user in the same pass.

**Why:** the 2026-09-06 audit found the same MAC send descriptor defined twice
(`mac_os_$send_pkt_t` at 0x40 bytes and a corrected `route_$mac_send_rec_t` at
0x4C in route/route_internal.h) and the same socket packet record defined twice
(`sock_pkt_info_t` vs `sock_$pkt_info_t`).  In both cases the corrected copy
lived in an internal header, so the wrong one stayed on the public API and
kept being used.  Leaving the duplicate is worse than not fixing it: the
`_Static_assert`s pass on the copy nobody calls.

**How to apply:** when a bead says "fold X into Y", delete X in the same
change, retype every caller, and move the layout evidence (the instruction
addresses) onto the surviving definition.  If a foreign public header has a
prototype the assembly contradicts - a wrong argument type, an argument the
callee writes through that a caller passes NULL for - fix the header rather
than working around it; that is what makes a faithful call site possible.
Related: [[fidelity-gates]].
