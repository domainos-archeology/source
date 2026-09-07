---
name: proc2-table-extents
description: The PROC2 process table is 70 slots (1..70) with the free-list tail cleared after the build loop; the PID map is 0xEA93D2-based and the EC array holds 70 pairs.
metadata:
  type: project
---

PROC2's tables in the SAU2 image, settled by bead source-nm9e (2026-09-07).

**Process info table** — base 0xEA551C = entry(1), stride 0xE4, **70 slots,
indices 1..70**. `P2_INFO_TABLE_SIZE` (proc2/proc2.h) carries this.

**Why:** the free-list build loop runs `0x00E3049A moveq #0x44,D0` (68) against
the `dbf` at `0x00E304D4` = 69 iterations, with D1 = 2..70 from
`0x00E3049C moveq #0x2,D1`. Entry 70 runs 0xEA9290..0xEA9373; the PID map and
the pgroup table follow immediately, so a slot 71 has nowhere to live.

**The free-list tail is written after the loop, not inside it.**
`0x00E304B4/0x00E304B6` store `next_index := i + 1` **unconditionally**, so the
last iteration leaves 71 in slot 70. `0x00E304D8 clr.w (0x00ea92a2).l` then
zeroes it — 0xEA92A2 == 0xEA551C + 69*0xE4 + 0x12 == entry(70)->next_index.
Modelling the loop as `(i < last) ? i+1 : 0` hides that shape; write the
unconditional store plus a separate tail-clear.

**PID -> index map**: element address is **0xEA93D2 + pid*2** (`0x00E3F35E`
`lea (0x0,A0,D0w*0x1),A1` with A0 = 0xEA551C, D0 = pid*2, then
`0x00E3F364 move.w (0x3eb6,A1),D0w`). PROC2_$INIT clears pids **2..64**
(`0x00E30470`, first word 0xEA93D6, 63 iterations) and sets pid 1 separately
at `0x00E304EA move.w #0x1,(0x00ea93d4).l`. The table ends at 0xEA9452, right
where the pgroup table starts (0xEA9454).

**Per-process EC pairs** (`PROC2_$EC`, 0x18 bytes each): base for index 1 is
0xE2B978 (`0x00E305A4` + `0x00E305B2 pea (-0x18,A3,D2w*0x1)`); the next
labelled object is UID_$GENERATOR_STATE at 0xE2C008, and
(0xE2C008 - 0xE2B978) / 0x18 == 70 exactly. So `PROC2_EC_ENTRIES` is
`P2_INFO_TABLE_SIZE`, one pair per slot — it used to be 69, which made
`PROC_FORK_EC(70)` a host-side overrun.

**How to apply:** when a PROC2 loop bound looks like "69 entries", check
whether the count came from a `dbf` (n+1 iterations) before believing it, and
check what object sits immediately after the table for the upper bound.

See [[proc2-self-index]] and [[dxm-proc2-mst-notes]].
