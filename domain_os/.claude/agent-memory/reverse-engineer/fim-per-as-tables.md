---
name: fim-per-as-tables
description: Every FIM per-address-space table holds 58 entries; the closed address chain that proves it, and the FIM_$INIT_PID/FIM_$FREE_PID ABI.
metadata:
  type: project
---

# FIM per-AS tables are 58 entries, and the chain that proves it

`FIM_AS_COUNT` = 58 (0x3A), defined in `fim/fim.h`.  The FIM module's per-AS
tables sit back to back in its data area, so each table's element count is
pinned by the address of the next object, and the chain closes on addresses
that are independently known (code entry points and a table with a literal
initialiser):

```
0x00E2126C FIM_IN_FIM          58 * 1  -> 0x00E212A6 (+2 align)
0x00E212A8 FIM_$USER_FIM_ADDR  58 * 4  -> 0x00E21390 FIM_FRAME_SIZE_TABLE
0x00E21890 FIM_$TRACE_BIT      58 * 1  -> 0x00E218CA JMP_TO_BUS_ERR
0x00E22002 FIM_$QUIT_EC        58 * 12 -> 0x00E222BA FIM_$QUIT_VALUE
0x00E222BA FIM_$QUIT_VALUE     58 * 4  -> 0x00E223A2 FIM_$TRACE_STS
0x00E223A2 FIM_$TRACE_STS      58 * 4  -> 0x00E2248A FIM_$QUIT_INH
0x00E2248A FIM_$QUIT_INH       58 * 1  -> 0x00E224C4 FIM_$DELIV_EC
0x00E224C4 FIM_$DELIV_EC       58 * 12 -> 0x00E2277C FIM_$GET_USER_SR_PTR
```

The tree previously guessed 64 for all of these.  **How to apply:** when a
Domain table's count is unknown, walk the neighbouring objects rather than
guessing a round number - the module data area is dense and the next symbol's
address is the count.

`FIM_$TRACE_BIT` is at **0x00E21890**, not 0x00E21888, and
`FIM_$PENDING_TRACE_FAULTS` at **0x00E21FFE**, not 0x00E21FF6 (both comments
in `fim/fim_data.c` were off by 8).  `FIM_$CLEAR_TRACE_FAULT` (0x00E22890)
opens `lea (-0x1002,PC),A1`; the PC for that displacement is the *extension
word* address 0x00E22892, giving A1 = 0x00E21890.  The same A1 then reaches
`(0x76E,A1)` = 0x00E21FFE and `(0x102C,A1)` = 0x00E228BC = `FIM_$EXIT`, which
is the independent check.  **Getting the PC base wrong on a `lea (d,PC)` is
the easy way to be off by 2-8 on a whole table.**

## FIM_$INIT_PID / FIM_$FREE_PID (0x00E0AA24, 0x00E0AA6C)

Both take the **address** of a word (Pascal `var`), not the value: the
callers are `pea (0x96,A3)` at 0x00E73314 (= `&proc2_info_t.asid`) and
`pea (-0xb0,A6)` at 0x00E749DA.  Despite the "PID" in the names the argument
is an **address-space id** - every table they touch is a per-AS table.

Both call `FIM_$CLEAR_TRACE_FAULT` with the classic Domain word-argument
sequence `subq.l #2,SP / move.w Dn,-(SP)`: a word in the *high* half of a
longword stack slot, read back as `move.w (0x4,SP)`.

Both **set** `FIM_$QUIT_INH[as]` with `st` (0xFF).  Quits stay inhibited for
an address space until `FIM_$INSTALL` (0x00E0A9C2) puts the first user fault
handler in place, or `FIM_$ACKNOWLEDGE` (0x00E0A96C) clears it.

`FIM_$ACKNOWLEDGE` is the SR10.4 map name for 0x00E0A96C (the tree had called
it FIM_$ADVANCE_SIGNAL_DELIVERY); see [[reference_sr104_domain_os_map]].
