---
name: ring-ctl-vs-ring-data
description: 0xE86400 is RING_$CTL (module block) and 0xE261E0 is RING_$DATA (per-unit stats array) - the tree had these two names swapped.
metadata:
  type: project
---

SAU2 map (`~/src/domainos-archeology/sau2-maps/domain_os.10.2.map`):

```
D    E86400  RING               size = 5D0
     E86400  RING_$CTL
     E261E0  RING_$DATA                       MARKED
```

so **0xE86400 is `RING_$CTL`** - the `ring_global_t` A5 module base every ring
routine loads with `lea (0xe86400).l,A5` - and **0xE261E0 is `RING_$DATA`**,
the `ring_$stats_t[2]` per-unit statistics array (0x3C stride, last object in
`D E261AC RING_WIRED size = AC`).  Until 2026-09-07 the tree had those two
names swapped (0xE86400 = RING_$DATA, 0xE261E0 = RING_$STATS); `RING_$STATS`
no longer exists anywhere.  `RING_CTL_BASE` / `RING_DATA_BASE` in
ring/ring_internal.h follow the same spelling.

**Why:** the map is authoritative per AGENTS.md, and the swap made every
"RING_$DATA" citation in ring/, asknode/ and network/ point at the wrong cell.

**How to apply:** the type names did *not* change - `ring_global_t` is
RING_$CTL's type and `ring_$stats_t` is RING_$DATA's element type - so match on
addresses, not on the word "stats", when reading older notes.  RING_$GET_STATS
lives in ring/get_stats.c; the array itself is defined in ring/data.c with the
rest of the module data.

Related: [[ring-info-record]], [[ring-stats-counter-names]],
[[reference_sau2_domain_os_map]].
