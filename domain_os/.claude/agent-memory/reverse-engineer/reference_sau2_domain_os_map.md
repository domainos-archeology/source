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

## The full sweep (bead source-9y3r, closed 2026-09-07)

An extracted copy lives at `~/src/domainos-archeology/sau2-maps/domain_os.10.2.map`
(2023 symbol addresses, 412 segment lines).  Parse it with:
`^(?P<pfx>[A-Z]?[0-9]*)\s+(?P<addr>[0-9A-F]+)\s\s+(?P<name>\S+)(?P<rest>.*)$` -
an empty prefix means a symbol line, `I`/`D`/`Dnn` a code/data segment header.
Multiple names can share one address (aliases).

Results of diffing it against the whole Ghidra symbol table:

- **Only one `FUN_` had an exact map symbol** (`E0A454 DMA_$FREE_ASID`, a bare
  `rts`).  The other 47 sit *inside* a module between two map symbols: the map
  exports module entry points only, so a module-local routine is a real
  negative, not a gap.  Several `FUN_` addresses are exactly a code segment's
  start (`AREA_`, `NET_IO`, `NETWORK`, `SMD_WIRED`, `IO_`, `DISK_`) - that is
  still module-local, the module's first unexported routine.
- **Ghidra has no auto `DAT_` symbols to sweep**: `gsk label list` returns only
  real labels (1670 of them), and Ghidra's dynamic `DAT_xxxxxxxx` names never
  appear.  The `DAT_` tokens that matter live in the *C tree's comments*; grep
  the tree for them and look each address up in the map instead.
- **24 addresses disagreed**; 13 were guessed tree names the map corrected.

**Rule that settled every judgment call: a map name without `$` is
module-local** (`CHKSUM`, `TESTPAGE`, `NULL_LOOP`, `RELOC`, `MAYBE_OPEN_ERROR_SOCKET`,
`PENDING_TRACE_FAULTS`, `RTWIRED_DATA_START`) and does **not** displace a more
descriptive tree name; a `$` name always wins.  `MARKED` in the map is not a
usable discriminator - plenty of exported `$` symbols lack it.

Two traps worth remembering:

- **The map reuses one module name for its code and its data halves.**
  `VFMT_$FORMATN` and `VFMT_$WRITEN` are each an `I` segment (the thunk) *and*
  a 0x10-byte `D` segment (the procedure-variable descriptor).  Ghidra's
  `ERROR_$PRINT` at `E825F4` was a guess - no map of any SAU build contains an
  `ERROR_$PRINT` symbol; the descriptor's real aliases are
  `VFMT_$WRITE2/5/10` (SR2/SR5/SR10 compatibility spellings, exactly like
  `VFMT_$FORMAT2/5/10` + `VFMT_$ENCODE2/5/10` at `E825E4`).
- **Duplicate Ghidra labels hide real bugs.**  `AS_$INFO_SIZE` was on both
  `E2B914` and `E2B970`; the map shows `E2B914 AS_$INFO` (0x5C bytes, the
  `AS_ASM` segment is 0x64) and `E2B970 AS_$INFO_SIZE`.

See [[sau2-map-name-corrections]] for the address-by-address table.
