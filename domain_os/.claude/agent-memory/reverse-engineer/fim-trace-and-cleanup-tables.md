---
name: fim-trace-and-cleanup-tables
description: FIM tables that are NOT per-AS (FIM_CLEANUP_STACK is 65 = PROC1_MAX_PROCESSES), the FIM_$TRACE_BIT gap emitted in fim/sau2/fim.s, and how to byte-verify an assembly gap against the image.
metadata:
  type: project
---

# FIM data objects outside the 58-entry per-AS chain

**FIM_CLEANUP_STACK (0x00E216B2) is indexed by PROC1_$CURRENT, not by AS id**, so
it holds `PROC1_MAX_PROCESSES` = **65** longwords (260 bytes), not `FIM_AS_COUNT`.
Extent closes on FIM_$PROC2_STARTUP at 0x00E217B6; the whole range is zero fill.
Three sites materialise the base PC-relatively and all agree:
`lea (0x70,PC)` at 0x00E21640 (FIM_$CLEANUP), `lea (0x48,PC)` at 0x00E21668
(FIM_$RLS_CLEANUP), `lea (0x1e,PC)` at 0x00E21692 (FIM_$SIGNAL); each scales
the word at 0x00E20608 with `lsl.w #2` then `adda.w`.

**Why:** the 58-entry chain in fim/fim.h is seductive — it is easy to assume every
FIM table is per-AS. This one is not, and the count came out exactly equal to the
PROC1 process-table count, which is the confirmation.

**How to apply:** before sizing a FIM table, check which word the index comes from —
0xE20608 is PROC1_$CURRENT (65 processes), 0xE2060A is PROC1_$AS_ID (58 spaces).

## FIM_$TRACE_BIT lives inside the code region

0x00E21890, 58 bytes, all zero, sandwiched between FIM_$SETUP_RETURN (ends
0x00E2188E, then 2 bytes of pad to the 4-byte boundary) and JMP_TO_BUS_ERR
(0x00E218CA). Because it sits in the middle of the FIM code, it is defined in
`fim/sau2/fim.s` (`.globl` + `.space FIM_AS_COUNT, 0`) and only *declared* in
fim/fim.h — fim/fim_data.c must not define it or the link double-defines.
Its extent check is an `.if (end - start) != (0x00E218CA - 0x00E21890) / .error`
pair, the gas analogue of the `_Static_assert`s fim_data.c uses.

## Byte-verifying an emitted gap

Assemble the `.s` on its own, `m68k-elf-objdump -t` to get the symbol offsets,
`m68k-elf-objcopy -O binary --only-section=.text`, then `dd` the window and
diff against `gsk read <addr> <len>`. Parse the gsk hex dump by splitting on
`|` and taking `line.split('|')[0][8:]` — a naive `([0-9a-f]{2} ?)+` regex stops
at the double space between the two 8-byte groups and silently returns half the
bytes as a "mismatch".

Verified identical this way: 0x00E2188E..0x00E218CF (66 bytes, fim.s + bus_err.s)
and 0x00E21878..0x00E2188D (FIM_$SETUP_RETURN, the anchor).

## Naming

The SR10.4 maps (all six sau*/domain_os.map) list **FIM_$FAULT_RETURN** and no
FIM_$FAULT; Ghidra agrees. `svc/sau2/trap8.s` still calls a nonexistent
`FIM_$FAULT` (bead source-si1j) — that is the SVC side's bug, not fim's.


## FIM_$CLEANUP_STACK is emitted in fim.s, not fim_data.c (bead source-z5bf)

Same reasoning as FIM_$TRACE_BIT: the table sits inside the FIM_ code region
fim/sau2/fim.s reproduces byte for byte (between FIM_$SIGNAL 0x00E21688 and
FIM_$PROC2_STARTUP 0x00E217B6), so the **single** definition is
`.globl FIM_$CLEANUP_STACK` + `.space 260, 0` there, with an `.if/.error`
extent check; fim/fim.h only declares it (`extern void
*FIM_$CLEANUP_STACK[PROC1_MAX_PROCESSES];`, needing
`#include "proc1/proc1_config.h"`).  The SAU2 map exports **no** symbol at
0x00E216B2, so the `$` name is a tree name, not a map name.

**Making a gas label `.globl` can silently widen a PC-relative `lea`.**
`lea (cleanup_stack_table,%pc),%a0` assembled to the brief `41fa dddd` while
the label was local; write `lea (FIM_$CLEANUP_STACK:w,%pc),%a0` so it stays
4 bytes.  Verified: the three sites keep displacements 0x70 / 0x48 / 0x1e and
`m68k-elf-objdump -t` shows every offset in fim.o unchanged (only the symbol
name and its `l`->`g` binding differ).

## fim/fim.h address comments were stale for six entries (bead source-afed)

Map + Ghidra agreed in every case; only the header was wrong, and each stale
value pointed *into* another object:

    FIM_$UII          0x00e21326 -> 0x00E2146C
    FIM_$PRIV_VIOL    0x00e212d8 -> 0x00E21530   (was inside FIM_$USER_FIM_ADDR)
    FIM_$FP_GET_STATE 0x00e21d48 -> 0x00E21DC2   (0xE21D48 is FP_$GET_FP)
    FIM_$FP_PUT_STATE 0x00e21e0c -> 0x00E21E86
    FIM_$SPURIOUS_INT 0x00e21ea4 -> 0x00E21F20
    FIM_$PARITY_TRAP  0x00e21efa -> 0x00E21F84

fim/sau2/fim.s carried the same two wrong FP_GET/PUT_STATE addresses in its
ROM-order table.  Other SAU2 map names worth knowing in this window:
`FP_$GET_FP` E21D48, `FP_$PUT_FP` E21D94, `FIM_$SPUR_CNT` E21F7E,
`PARITY_$INFO` E21FE6, `MISS_STATUS` E21FFA, `BUS_ERROR_SWITCH` E218CC,
`FIM_$COM` E213A4 (Ghidra's FIM_$COMMON_FAULT at E213A0 is the 4-byte
`pea (4,%sp)` pre-entry, module-local, no map symbol).

**Recipe for auditing a whole header's Address: comments:** pair each
`Address: 0x...` with the identifier on the *declaration line after the
closing `*/`*, not with the nearest identifier in the prose - the prose in
these headers names other symbols and a naive nearest-match reports seven
false mismatches.

See also [[fim-per-as-tables]].
