---
name: mmap-seg-aste-table
description: The 0xEC5400 segment table is a 1-based aste_t[0x14] array whose aote link the mmap page-replacement code reads at (-0x10,An); which aote fields each site tests.
metadata:
  type: project
---

The storage at 0xEC5400 is a **1-based array of 0x14-byte `aste_t`**, never a
`void *[]`.  Every site builds the address the same way:

```
move.w (-0x1ffe,An),Dw     ; mmape->segment
movea.l #0xec5400,Ai
lsl.w #0x2 / lsl.w #0x2 / add.w   ; seg * 0x14  (a 16-bit multiply)
lea (0x0,Ai,Dw),Ai
movea.l (-0x10,Ai),Aj      ; = record(seg)+0x04 = aste->aote
```

so `record(seg) = 0xEC5400 + (seg-1)*0x14`, i.e.
`MMAP_$SEG_ASTE_FOR(seg)` in `mmap/mmap_internal.h`.  **The dereference lands
on the AOTE, not the ASTE** — that is what made the old `void *SEGMENT_TABLE`
view look plausible: the offsets the code then applies (0x0E, 0x28, 0x08,
0xB9) are all `aote_t` fields.

Sites and what each tests:

| address | function | field |
|---|---|---|
| 0x00E0D43A | MMAP_$WS_SCAN | `move.w (0xe,A4)` / `btst.l #0xc` — bit 12 of the AOTE attribute-flags word |
| 0x00E0D4F0 | MMAP_$WS_SCAN (ON_DISK) | `tst.w (0x28,A1)` / sne — high word of `aote->len_high` |
| 0x00E0D512 | MMAP_$WS_SCAN (not ON_DISK) | `tst.w (0x8,A1)` / smi — sign of the high word of `aote->vol_uid` |
| 0x00E0D67E | MMAP_$GET_IMPURE | same bit-12 test as 0x00E0D43A |
| 0x00E0D0C0 | MMAP_$RELEASE_PAGES (ON_DISK) | `tst.w (0x28,A0)` — `aote->len_high` high word |
| 0x00E0D0DC | MMAP_$RELEASE_PAGES (not ON_DISK) | `tst.b (0xb9,A0)` / smi — `aote->remote_flag < 0` |
| 0x00E0C8C0 | mmap_$trim_wsl | same pair as RELEASE_PAGES |
| 0x00E0A096 | AREA_$DEACTIVATE_ASTE | same addressing |

Note WS_SCAN and RELEASE_PAGES disagree on how "remote" is decided for a
non-ON_DISK dirty page (`vol_uid` sign vs `remote_flag`); both are original,
do not unify them.

`aote+0x0E` bit 12 is bit 4 of `aote_t.attr_flags_hi` — the bit
`AST_$SET_ATTR_DISPATCH` writes for attr type 0 and for a non-zero refcount
(0xE04CF2, 0xE04D0C).  `MMAP_AOTE_ATTR_FLAGS()` /
`MMAP_AOTE_ATTR_FLAG_BIT12` in `mmap/mmap_internal.h` read it byte-order
safely.

Host tests: `aste_t.aote` is a real pointer, so an mmap host test needs **no**
`ARCH_HOST_VA_BASE` arena — just an `aste_t MMAP_$SEG_ASTE[]` array (segment 1
is slot 0) plus `mmap_wsl` / the `MMAP_$MMAPE` block (ppn >= 0x200 only,
since source-fyjc) / `mmu_pft_base` /
`mmap_pte_base` / `mmap_pid_to_wsl`.  See
`mmap/test/test_ws_scan.c` and `mmap/test/test_release_pages.c`.
