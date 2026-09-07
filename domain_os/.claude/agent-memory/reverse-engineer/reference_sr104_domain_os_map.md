---
name: reference-sr104-domain-os-map
description: sr10.4-install/sau{7,8,9,11,12,14}/domain_os.map are real kernel link maps with module names, symbol names and sizes in address order - use them to name FUN_ functions in the SAU2 image
metadata:
  type: reference
---

`sr10.4-install/sau7/domain_os.map` (and the sau8 / sau9 / sau11 / sau12 /
sau14 siblings, plus the same files under
`sr10.4-install/install/ri.apollo.os.v.10.4/sau*/`) are the **link maps of
the shipped SR10.4 kernels**.  ~3200 lines each.  Format:

```
I  3C40DE3C  FIM_               size = 968      <- module, size in hex
   3C40DE3C  FIM_$INIT_FF_POOL                  <- entry inside it
   3C40DEBA  FIM_$RESTORE_FF
D  3C42BDC0  FIM_               size = 28       <- D = data section
   3C42BDC0  FIM_$CLEANUP_PTRS
```

**How to use it against the SAU2 image (which has no map):** the *order* of
entries inside a module and the gaps between them carry over between SAUs
even though the addresses do not.  Anchor on one already-named function in
the module, lay the map's order over the SAU2 addresses, and check a size or
two.  That is how FIM_$INSTALL / FIM_$GET_FIM_ADDR / FIM_$INIT_PID /
FIM_$FREE_PID were named (bead source-cry): FIM_$BUILD_DF anchored the
module and FIM_$INIT_PID's 72 bytes matched the map exactly.

Other things the map settles cheaply:

- **Whether a routine is a module entry at all.** A `bsr` target that the map
  does not list is a nested Pascal procedure or a local helper.
- **Aliases sharing one cell.** The data-section entry `VFMT_$WRITEN size =
  10` carries three names (VFMT_$WRITE2 / WRITE5 / WRITE10) at one address:
  those are procedure-variable descriptors, not three routines.
- **Section names**, e.g. RTWIRED_PROC / DISK_BUFFERS / RING_RCV_PAGE, which
  match the labels already in Ghidra.

Caveat: sau7 is a different machine, so some modules (MAC_, MAC_OS_) are
absent and some are present that SAU2 lacks.  Check more than one sau*.map
when a module is missing.

Related: [[reference-sr104-userspace-binaries]] (the /etc/netmain strings
trick for field names), [[ring-stats-counter-names]].
