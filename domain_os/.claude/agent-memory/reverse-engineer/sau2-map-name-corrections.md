---
name: sau2-map-name-corrections
description: The address-by-address table of names the SR10.2 SAU2 link map corrected (and the ones it deliberately did not), applied 2026-09-07 in Ghidra and the C tree
metadata:
  type: project
---

Applied while closing bead source-9y3r.  Source of truth:
`~/src/domainos-archeology/sau2-maps/domain_os.10.2.map` - see
[[reference-sau2-domain-os-map]] for how to read it and the rules used.

## Renamed (Ghidra + C tree)

| addr | map name | old (guessed) name |
|---|---|---|
| 00000008 | `BUS_ERROR_VEC` | `PROM_TRAP_BUS_ERROR` (tree: also `_..._PTR`, `_...`) |
| 00000100 | `PROM_$MACHINE_ID` | `PROM_$SAU_AND_AUX` |
| 00E0A3A6 | `DMA_$CHECK` | `check_dma_error` |
| 00E0A454 | `DMA_$FREE_ASID` | `FUN_00e0a454` (a bare `rts`; now `dma/free_asid.c`) |
| 00E23C98 | `MMAP_$PAGEABLE_PAGES` | `MMAP_$PAGEABLE_PAGES_LOWER_LIMIT` |
| 00E24C08 | `NETWORK_$HDR_PAGE` | `NETWORK_$HDR_PAGE_PA` (Ghidra only) |
| 00E2B914 | `AS_$INFO` | `AS_$INFO_SIZE` (duplicate of the real one at E2B970) |
| 00E2F1D4 | `AST_$ACTIVATE_ASTE_CANNED` | `AST_$ACTIVATE_CANNED_SEG` |
| 00E7BE94 | `PROC2_$UID` | `PROC2_UID` |
| 00E825F4 | `VFMT_$WRITE10` (+ `WRITE2`/`WRITE5` labels) | `ERROR_$PRINT`; file `vfmt/error_print.c` -> `vfmt/write10.c` |
| 00E87DA8 | `ROUTE_$Q_DEPTH` | `ROUTE_$PACKET_STATS` (0x81 longs = a queue-depth histogram) |
| 00E87FAC..C8 | `ROUTE_$STD_DLEN_ERR`, `STD_TOO_FAR`, `STD_MISROUTE`, `STD_PKTS_ROUTED`, `DLEN_ERR`, `TOO_FAR`, `MISROUTE`, `PKTS_ROUTED` | the `ROUTE_$STAT_OVERSIZED_*/DROPPED_*/FORWARDED_*` family |
| 00E87FCC / D0 | `ROUTE_$Q_OFLO` / `ROUTE_$NETBUF_ALLOC` | `ROUTE_$USER_PORT_COUNT` / `..._MAX` (semantics still unconfirmed - bead source-v7nn) |
| 00E88216 | `ROUTE_$PID` | `ROUTE_$PROCESS_UID` |
| 00E88218 | `ROUTE_$USER_CHECKSUM` | `ROUTE_$CHECKSUM_ENABLED` |

## Deliberately kept (map name is module-local or a segment marker)

`00E0A290 CHKSUM` (keep `disk_$chksum_page`), `00E178AA/00E17910
MAYBE_OPEN/CLOSE_ERROR_SOCKET` (keep `xns_$maybe_*`), `00E21FFE
PENDING_TRACE_FAULTS` (keep `FIM_$PENDING_TRACE_FAULTS`), `00E24C60 NULL_LOOP`
(Ghidra `NULLPROC` = the segment name), `00E29138 TESTPAGE` (keep `io_$probe`),
`00E825E4 VFMT_$FORMAT2/5/10` (Ghidra `VFMT_$FORMATN` = the segment name),
`00E87D68 RTWIRED_DATA_START/RTWIRED_PROC_END` (segment boundary markers; keep
`RIP_$HALT_PACKET`), `00E88834 RELOC` (linker placeholder at the `ACL_$DATA`
segment start; keep `ACL_$ACL_CACHE`), `00FFA000 DMA` / `00FFB400 MMU` (IODEFS
page names).  Each of these carries a `gsk comment disassembly` recording the
map spelling.

## Left as FUN_ (module-local, no map symbol)

47 pure `FUN_00exxxxxx` plus six nested-Pascal helpers the tree names
`<PARENT>_FUN_<addr>` (`MST_$INIT` x2, `PROC2_$SUSPEND`, `DIR_$GET_ENTRYU`,
`DIR_$DIR_READU`, `XPD_$CAPTURE_FAULT`).  Those six carry the `FUN_` token by
tree convention, so `gsk search FUN_` returns 53 rows even when the sweep is
complete - do not treat that count as unfinished work.
