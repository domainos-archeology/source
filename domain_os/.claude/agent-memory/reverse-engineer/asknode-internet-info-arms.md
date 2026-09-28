---
name: asknode-internet-info-arms
description: ASKNODE_$INTERNET_INFO's 42-arm local dispatch and its constant pool, the WHO_NOTOPO/WHO_REMOTE request records, and the layout corrections the re-emission forced in rip/route/ring/network/disk/peb/proc2/mst
metadata:
  type: project
---

# ASKNODE_$INTERNET_INFO (0xE645EA) local dispatch

The local arm is one `cmpi.w` chain at 0xE6463E-0xE64786 with 41 entries.
**0x45 is NOT in it** — the WHO time-sync request only exists in
ASKNODE_$SERVER; here it falls through to the default at 0xE65502.

Arms and their code, in address order:
0x06 E6478A, 0x51 E6484A, 0x08 E64866, 0x0C E64870, 0x04/0x18 E6489A,
0x10 E64916, 0x02 E6495A, 0x0A E64972, 0x12 E6498E, 0x4F E649C2,
0x57 E649EC, 0x59 E64A0C, 0x16 E64A24, 0x14 E64A3C, 0x1A E64A54,
0x1C E64A66, 0x1F E64B2C, 0x21 E64BB0, 0x23 E64BE4, 0x25 E64C00,
0x27 E64C4E, 0x29 E64DE2, 0x2B E64F06, 0x2F E64F1E, 0x31 E64F3C,
0x33 E64F78, 0x35 E64F8E, 0x37 E64FC6, 0x39 E64FD2, 0x3B E65088,
0x3F E650C6, 0x3D/0x5B E6514A, 0x41 E6524C, 0x43 E65328, 0x47 E65356,
0x49 E6536C, 0x4B E6538C, 0x4D E6543E, 0x55 E6544C, default E65502.

Shared tails worth knowing: **0xE64F86** is `clr.l (-0x110,A6)` — arms that
branch there force local_status to 0 (0x12, 0x33, 0x4F, 0x57, 0x59, and the
0x41 scan hit).  **0xE6537E** is a 12-byte `lea (0x8,A1),A2` copy shared by
the timezone arm (0x08) and PROC1_$GET_LOADAV (0x49).

Two arms are reached by branching *into another arm's call site*:
0x59 (`bra.b` at 0xE64A22 → the 0x4F `jsr PROC2_$ZOMBIE_LIST` at 0xE649E0)
and 0x35 selector 2/3 (jump table at 0xE64FA6 → the 0x16 arm at 0xE64A24).
Read the jump table with `gsk read`; Ghidra's listing skips it.

## The code-segment constant pool 0xE658AE..0xE658CD

`gsk read 0xE658AE 32`:
```
00e658ae  00 39 00 00 00 00 ff ff  ff ff 00 00 00 2a 00 02
00e658be  00 01 00 04 00 03 00 00  01 f8 00 13 00 3e 00 00
```
AE=57 proc-list cap; B0..B9 the canned local route {0, -1, 0}; BA=42
mnt-info size; BC=2 / BE=1 display units; C0=4 RINGLOG stop / C2=3 clear;
C4=0 PROC2_$INFO scan key; C6=504 proc-info len; C8=19 signal; CA=62
proc-list2 cap; CC=0 ASKNODE_$EMPTY_DATA.  All labelled in Ghidra as
`ASKNODE_$C_*`.

## Traps that cost time here

- `pea (d,PC)`'s PC is instruction address **+2**; two different sites can
  resolve to the same cell (0xE658AE is reached from both 0xE64992 and
  0xE649D6).
- 0xE64D10 `beq` in the request-0x27 boot-volume test **skips** the clear, so
  the surviving condition is `flags<0 && status==0 && dev_type==0` — the
  natural reading is inverted.
- `move.l #0x40000,-(SP)` is a two-WORD push (0x0004 then 0x0000), not a
  longword argument: that is how the 0x10 arm gives DISK_$GET_STATS its
  ctype and cnum.
- `sgt`/`seq`/`or.b`/`bpl` sets a Domain boolean and then branches on **bit 7
  of the byte**, i.e. "neither condition held".

## Layout corrections this re-emission forced

- `rip_$stats_t` — errors and unknown_commands are WORDS at +0x08/+0x0A
  (0xE68B16 / 0xE68DFC), then `local_net_pkts` long at +0x0C and
  `net_pkts[64]` at +0x10; total 0x110.  Now packed with asserts in rip.h.
- `ring_global_t` +0x5C0 is a WORD (`send_null_cnt`) followed by
  `clobbered_hdr` at +0x5C2 — the map names RING_$CLOBBERED_HDR at 0xE869C2.
- `NETWORK_$PAGING_BACKLOG` / `NETWORK_$FILE_BACKLOG` are nine-longword
  histograms (0xE24BAC and 0xE24BD0).  `NETWORK_$FILE_BACKLOG_OVERFLOW` is
  bucket **8**, the same longword the `depth <= 8` path writes (0xE63644 and
  0xE6364E both reach 0xE24BF0) — it is a macro alias now, not storage.
- `DISK_$GET_STATS` takes FIVE arguments (ctype, cnum, unit, has_stats*,
  stats*) and its driver callback takes three; the stats buffer is 22 bytes.
- `PEB_$GET_INFO`'s first argument is a `uint16_t *` (`clr.w (A0)`), with the
  flag bits `bset.b` into byte 0 of that word.
- `PROC2_$GET_ASID` returns the ASID in D0; `PROC2_$INFO`'s first two
  arguments are (scan key, pid), not (pid, offset).
- `MST_$GET_PRIVATE_SIZE` takes (asid*, size*, size2*, status*).
- `route_$port_t` 0x4C..0x57: NET_IO_$CREATE_PORT writes longwords at +0x4C,
  +0x50, +0x54 but ASKNODE reads a long at +0x4E and a word at +0x52, so the
  block is spelled as four uint16_t (bead source-i54r).

## Where things had to move to satisfy the header rule

rip_$route_t / rip_$entry_t / rip_$data_t / RIP_$DATA / RIP_$STATS /
RIP_$INFO and the RIP_TABLE_*/RIP_STATE_* constants moved
rip_internal.h → rip.h.  ROUTE_$Q_DEPTH and the eight forwarding counters
plus ROUTE_$NETBUF_ALLOC / ROUTE_$Q_OFLO moved route_internal.h → route.h
(ROUTE_$START_TIME at 0xE825DC is new there).  The RING_$CTL counter
aliases moved ring_internal.h → ring.h.

# The two WHO listers

Both build an **asknode_request_t** on the stack and hand PKT_$SEND_INTERNET
0x18 bytes of it, leaving the fields they do not write as whatever the frame
held.  Reading the stores as consecutive longwords is the trap - they are not.

ASKNODE_$WHO_NOTOPO (0xE65FDC), record at A6-0x288:
one `move.l #0x30045` covers version 3 + request_type 0x45 (0xE6610E), then
param1 (+0x08) = NODE_$ME, param2 (+0x0C) = ROUTE_$PORTP[idx]->network,
param3 (+0x14) = 0x5B8D8.  Its RIP destination record at A6-0x50 is a
rip_$dest_addr_t whose **host_lo (the LONG at +0x06)** gets
`andi.l #0xFFF00000 / ori.l #1` (0xE6605A) - node 1, not a flag word at +0x04.
0xE660A0 tests **D0**, RIP_$FIND_NEXTHOP's return value (0 = direct route),
not the port index; and 0xE66158 clears a **word** at pkt_info+0x08.

ASKNODE_$WHO_REMOTE (0xE66334), record at A6-0x268: SOCK_$OPEN gets
**0x00200020**.  Simple form = request_type 0 with a **WORD** max count at
+0x08 (max-1 for the local query, which already listed this node); remote form
= 0x2D at +0x02, NODE_$ME at +0x08, ROUTE_$PORT at +0x0C, the `st` flag BYTE
at +0x10, the max_nodes WORD at +0x12, the queried node at +0x04 and 0x4000 at
+0x14.  In the sequential scan a node that answers **twice ends the whole
listing** (0xE666F6 branches to the exit at 0xE66714, not to the loop
condition) - that is how the broadcast form knows it has been round the ring.

Host-testing note: a test that lets the code dereference
app_$receive_rec_t.reply/.data must point `ARCH_HOST_VA_BASE` at its own
arena first (arch/host/arch.h), or the 32-bit round trip truncates the
pointer and segfaults before the first printf flushes.
