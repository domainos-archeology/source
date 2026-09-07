---
name: acl-image-cache
description: "The ACL image cache directory (A5+0x800..0xB76), acl_$eval_acl_entries' three phases, acl_$get_obj_acl_attrs' UID normalisation, and the FILE lock-object-table addressing convention - recovered 2026-09-07"
metadata:
  type: project
---

**ACL module A5 = 0xE7CF54.** The image cache bookkeeping is five contiguous
arrays; the contiguity is what fixes each one's element count:

| A5 off | absolute | symbol | shape |
|---|---|---|---|
| 0x800 | 0xE7D754 | `ACL_$CACHE_DIR[31]` | 16 B: uid@0, flag@0x0A, world_rights@0x0D, subsys_rights@0x0F |
| 0x9F0 | 0xE7D944 | `ACL_$CACHE_LRU_LINKS[32]` | `{int16 next; int16 prev}` |
| 0xA70 | 0xE7D9C4 | `ACL_$CACHE_HASH_LINKS[32]` | same |
| 0xAF0 | 0xE7DA44 | `ACL_$CACHE_HASH_BUCKETS[64]` | int16, 61 used |
| 0xB74 | 0xE7DAC8 | `ACL_$CACHE_FREE_HEAD` | int16 |
| 0xB76 | 0xE7DACA | `ACL_$CACHE_LRU_HEAD` | int16 — **aliases `ACL_$SUPER_COUNT[0]`** |

0xAF0 + 64*2 = 0xB70 = `ACL_$LOCAL_LOCKSMITH`, which closes the chain.
`UID_$HASH`'s modulus is the word **0x003D (61) at 0xE45E8C** (`pea (-0x20,PC)`
at 0xE45EAA). The two circular-list primitives are `acl_$cache_list_insert`
(0xE44C3C) and `acl_$cache_list_remove` (0xE44C92) — generic `(head*, links[],
slot)`; the free list and the 61 hash chains share the 0xA70 array with
different heads.

**`acl_$acl_entry_t` is 0x20 B, 1-BASED**: person@0, group@8, org@0x10,
rights word@0x1A. `lea (0x14,slot,i*0x20)` (0xE462DE) proves entry i is at
slot+0x14+i*0x20, so slot+0x34 (the `entries` field) is entry **1**.

**`acl_$eval_acl_entries` (0xE46172)** — three phases; phase 3 reloads only the
index from `max_index` and keeps A0's entry pointer (0xE46364), a real quirk.
Phase 2 is nine passes: pass 0 copies the caller's whole **36-byte** SID block,
passes 1..8 substitute `proj_uids[k-1]` for the group slot. Every rights value
is a **zero-extended byte** (`clr.w D0w` + `move.b`), so a 16-bit entry rights
word never keeps its high half.

**`acl_$get_obj_acl_attrs` (0xE45F78)** is a Pascal *procedure* (no result
slot at any of its five call sites). Its UID normalisation is "clear **bit 24
of the low half**" — `bclr.b #0,(-0x14,A6)`, i.e. bit 0 of byte 4 of the eight.
The funky-ACL selector is `((uid.low >> 20) & 0xE0)`. Well-known short circuit
= zero top BYTE of `high` AND (top WORD 1 or 2, or uid == ACL_$NIL 0xE17384,
or == UID_$NIL 0xE1737C); only UID_$NIL reports 0x000F0001.

**ASID == PID for every ACL per-process table** (bead source-x5dd):
`ACL_$ALLOC_ASID` (0xE73BB8) copies row `PROC1_$CURRENT` into row `new_asid`
with identical bases/strides off 0xE97294 and the same `(n-1)>>3` bitmap
arithmetic. 0xE20608 (PROC1_$CURRENT) and 0xE2060A (PROC1_$AS_ID) stay
separate cells.

**FILE lock-object table addressing.** Every consumer computes
`0xE935CC + index*0x1C`, which is the **END** of 1-based entry `index`, and
reads fields at NEGATIVE displacements: UID -0x10 (field +0x0C), free/hash link
-0x08 (+0x14), refcount -0x04 (+0x18), flags1 -0x03 (+0x19), rights -0x02
(+0x1A), flags2 -0x01 (+0x1B). `FILE_$LOT_ENTRY(n)` in file/file_internal.h and
`file_lock_entry_detail_t` are the correct model; `file_lock_entry_t` in
file/file.h is a stale second model of the same table (bead source-0sgi).
Slot-range asymmetry: `FILE_$CHECK_PROT` `bcc` on 0x96 (0xE5D192) **excludes**
it, `FILE_$EXPORT_LK` `bls` (0xE74152) **includes** it.

See [[acl-rights-abi]], [[acl-dir-name-abi-notes]] and
[[feedback_fidelity_gates]].
