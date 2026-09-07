---
name: reference-sau2-domain-os-map
description: sau2.10.2.tar contains sau2/domain_os.map - the link map for the EXACT image loaded in Ghidra; use it before declaring any address unnameable
metadata:
  type: reference
---

`~/src/domainos-archeology/sau2.10.2.tar` holds `sau2/domain_os.map`, and it
is the link map **for the image in Ghidra**: it places `FILE_$LOCK_INIT` at
`E32744`, our address.  Check that anchor before trusting it - the sibling
`sau2.10.3.tar` map is a different build (`FILE_$LOCK_INIT` at `E2F204`), and
the sr10.4 `sau7/8/9/11/12/14` maps are different machines entirely
(see [[reference-sr104-domain-os-map]], which is only good for *order*).

Extract with `tar xf ~/src/domainos-archeology/sau2.10.2.tar -C <dir>
./sau2/domain_os.map` (note the leading `./`; the 10.3 tar has no `./`).

It names **data as well as code**, in address order, with segment sizes, so
addresses land directly:

```
D69  E88834  ACL_$DATA          loaded at 196B4E, size = AD98
D71  E935CC  FILE_$LOT_DATA     loaded at 1A18E6, size = 1086C
D53  EA3E38  RINGLOG_$DATA      loaded at 1B2152, size = 11FC
     E82128  OS_DATA_SHUTWIRED
     E821F0  FILE_$LOT_HASHTAB
     E823F6  FILE_$LOT_FREE
     E82740  OS_DATA_SHUTWIRED_END
```

Two lessons from the first use (bead source-9r49):

- **The sr10.4 name can be wrong for SAU2.** The cell at
  `OS_DATA_SHUTWIRED+0xC8` is `FILE_$ASID_LOCKS` in every sr10.4 map but
  `FILE_$LOT_HASHTAB` here.  Prefer this map.
- **A segment with no interior symbols is a real negative.**
  `FILE_$LOT_DATA` (E935CC..EA3E38 - the LOT entries, the 58x300 per-ASID lock
  table and its count array, ending exactly at EA3E38) exports nothing, which
  is why 0xE9F9C4 could be closed as not-nameable rather than left as a TODO.

Bead source-9y3r tracks sweeping the whole map against Ghidra's `FUN_`/`DAT_`
names.
