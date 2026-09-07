---
name: header-ownership-rule
description: How to decide which subsystem header owns a declaration when the name prefix is not a subsystem directory - use the SAU2 map segment.
metadata:
  type: project
---

A declaration belongs in `<sub>/<sub>.h` where `<sub>` owns the **SAU2 map
segment the storage lives in**, not where the name prefix happens to point.

**Why:** many kernel names carry a prefix that is not a subsystem at all
(`PV_LABEL_$UID`, `MNK_$KTT_MAX`, `RTWIRED_$SEND_FLAGS`, `NIL_$NETWORK_UID`,
`PPO_$NIL_ORG_UID`, `DUMP_$ADDRS`, `NODE_$ME`).  Chasing the prefix invents
subsystems that never existed; the map's segment table says which module the
linker actually put the cell in.

**How to apply:** look the symbol up in
`~/src/domainos-archeology/sau2-maps/domain_os.10.2.map`, find the enclosing
segment line, and use that module's directory.  Confirmed mappings:

| map segment | owning dir |
|---|---|
| `UID_LIST` (0xE1737C, 0x210) | `uid/` - holds every `*_$UID` pool cell incl. NIL_$/USER_$/PPO_$ |
| `SMD_WIRED` (0xE26F20, 0x5E0) | `smd/` - MNK_$KTT_PTRS / MNK_$KTT_MAX live here |
| `RIP_RTWIRED` (I 0xE87000 0x3EC, D 0xE87D68 0x18) | `rip/` - RTWIRED_$SEND_FLAGS, RTWIRED_$CALLBACK |
| `ROUTE_RTWIRED` (D 0xE87D80, 0x4A8) | `route/` - ROUTE_$USER_STAT at 0xE87FD6 |
| `REM_NAME` (I 0xE4A408, D 0xE7DBB8) | `rem_name/` |
| `NET_ASM` (0xE2459C, 0xC) | four cells, four owners; NODE_$ME got `node/node.h` |
| `DUMP` (0xE00400, 0x400) | `dump/dump.h` (header-only; storage still in mmap/) |

The map lists an outer loader segment (`I12 E87000 RTWIRED_CODE`) and an inner
module segment (`I E87000 RIP_RTWIRED`) at the **same address**; the inner one
(no digit after the I/D) is the owner.

A header-only directory with no `.c` is fine and does not go in the Makefile's
`SUBSYSTEMS` list (that list drives `lib<sub>.a`); `rem_name/`, `dump/` and
`node/` are all header-only.  `-I.` makes `#include "dump/dump.h"` work anyway.

See [[gate-scripts-header-hygiene]].
