---
name: dxm-proc2-mst-notes
description: Recovered ABIs and layouts for DXM_$ADD_CALLBACK, MST_$FORK and PROC2_$FORK, plus the confirmed original bug in PROC2_$FORK's ASID failure path (Sept 2026)
metadata:
  type: project
---

Verified against the disassembly, not the decompiler.

**Why:** these three were re-emitted after the 2026-09-06 audit; the ABIs and
the bug conclusion cost the most work to establish and nothing else records them.

## DXM_$ADD_CALLBACK (0x00E16FE0)

Six Pascal parameters, 20 bytes, every call site cleans up 0x14:

    +0x08 queue      long   +0x0C callback  long   +0x10 data      long
    +0x14 data_size  word   +0x16 check_dup boolean byte (high byte of the slot)
    +0x18 status_ret long

`callback` and `data` each point at a *cell* holding an address, not at the
thing itself. Callers push the boolean with `st -(SP)`, which predecrements A7
by two, so the byte lands at the slot's low address — that is why the callee
reads it as `move.b (0x16,A6),D2b`.

Real call sites and their (data_size, check_dup):
KBD_$RCV 0x00E1CDF2 (4,true) · TTY_$I_SIGNAL 0x00E1B872 (12,true) ·
AST_$SAVE_CLOBBERED_UID 0x00E07248 (8,true) · SUMA_$RCV 0x00E1AE7E (4,true) ·
DXM_$ADD_SIGNAL 0x00E172A2 (10,param) · TIME_$Q_SCAN_QUEUE 0x00E16F66 (4,var).

Gotcha: the duplicate scan scales the entry index as a longword
(`lsl.l #0x4,D0`, 0x00E17060) but the insert scales it as a word and uses it
sign-extended (`lsl.w #0x4,D1w`, 0x00E17102). Not interchangeable.

Two `pea (d16,PC)` cells in its own code: 0x00E17164 is the *status* longword
0x00170002 ("datum too large for deferred execution"), NOT a string — an easy
one to mis-model; 0x00E17154 is the crash string "(DXM) No room%" (the '%' is
crash_puts_string's terminator, so the " H" bytes at 0x00E17162 are padding,
and Ghidra's auto string label swallows them).

`dxm_entry_t` is 16 bytes in the image but 24 on a 64-bit host because
`callback` is a native function pointer — host tests must index the entry
array with an explicit 16-byte stride (bead source-wy9y).

## MST_$FORK (0x00E739F8)

    +0x08 asid word · +0x0A pid word · +0x0C flags LONGWORD · +0x10 status long

Sole caller PROC2_$FORK pushes *fork_flags with `move.l` at 0x00E72F52 and
cleans up 12 bytes.

## PROC2_$FORK (0x00E72BCE) — confirmed original bug

On the MST_$ALLOC_ASID failure branch (taken at 0x00E72CC6) the code sets bit
31 of the *caller's* status at 0x00E72CC8, then `bra.w 0x00E73240`. The shared
exit at 0x00E732D6 does `move.l (-0x2c,A6),(A2)`, and nothing on that path ever
writes A6-0x2C: all ten `pea (-0x2c,A6)` sites lie between 0x00E72CCC and
0x00E73240, the range 0x00E73240..0x00E732D4 never mentions the slot, and
nothing before 0x00E72CCC touches it. So the real error is discarded and stack
garbage is returned. The "process table full" exit at 0x00E72C2E deliberately
branches to 0x00E732DA — *past* that store — which is how you can tell the
0x00E72CCC path is a mistake rather than a convention.

**How to apply:** when a Pascal procedure has one shared `*status_ret = local`
epilogue, check whether each `bra` to the teardown lands before or after that
store; landing before it is often an original bug worth documenting rather than
a decompilation error.
