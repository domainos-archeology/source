---
name: os-stop-reemission
description: Layouts, constant cells and hand-written-asm conventions recovered while re-emitting OS_$INIT (0xE337F4) and STOP_$WATCH (0xE81814) in Sept 2026
metadata:
  type: project
---

Recovered facts worth keeping. Verified against the disassembly, not the
decompiler.

**Why:** both functions were re-emitted from scratch after the 2026-09-06
fidelity audit scored them 1/5; the layouts below cost the most work to
recover and nothing else in the tree records them.

**How to apply:** trust these over any decompiler output; re-derive only if
the assembly disagrees.

## STOP_$WATCH (0x00E81814)

- A5 = the entry point itself. Module data follows the code; the useful
  offsets are +0x3D8 saved regs, +0x3F4/+0x3F8 the two calibration longwords
  (only their low words are written), +0x3FC the per-process trap counter
  array, +0x500 the calibration patch record, +0x508/+0x50C the wire-area
  pointer cells, +0x510/+0x512 the wire flag and count, +0x514 the 16 slots.
- The stopwatch works by patching A-line traps (0xA000+slot at the entry
  address, 0xA100+slot at the exit address) over real instructions;
  STOP_$WATCH_UII restores the instruction and arms STOP_$WATCH_TRACE_FLAG
  (0x00E21596) so the trace exception re-enters STOP_$WATCH_TRACE.
- `tst.l` on the calibration longword IS the "already initialised" test —
  there is no separate flag anywhere in the module.
- Operations 2..7 are a peek/poke branch table of two-byte `bra`s at
  0x00E81854; the three pokes are gated by DISK_$DIAG being merely non-zero
  (`tst.b`/`bne`, not the usual boolean `< 0`). The gate escapes non-locally
  by popping its own `bsr` return address.
- There is no upper bound on either the operation code or the parent slot
  index.

## OS_$INIT (0x00E337F4)

- A5 is *not* the module base here: `lea (0xe351f4).l,A5` makes it
  BOOT_INFO_TABLE.
- Boot record (param_1, 9 longwords, copied twice): words at +0 device,
  +2 controller, +4 unit, +6 flags. `btst.b #n,(-0x21,A6)` is bit n of the
  flags *word* because -0x21 is its low byte.
- OS_$BOOT_DEVICE (0xE82728) is an 8-byte record, not a word: device, a
  cleared word, then the {controller, unit} longword.
- Vector install walk: entry i carries a {first vector : word, count : word}
  descriptor 0xDC bytes on; the values live in the *following* count entries
  (the pointers advance before the value is read), also 0xDC bytes on. The
  outer index bound is `<= 0x28`.
- MST_$MAP_CANNED_AT (0x00E30FAA) takes NINE parameters: va, uid, offset,
  size, flags, then two booleans in separate 2-byte slots (pushed with
  `st`/`clr.w`, or both at once with `clr.l`), then a location descriptor and
  the status. The callee reads the second one as `move.b (0x1e,A6),D3b`.
- VTOP_OR_CRASH (0x00E6D1E8) takes the ADDRESS of a longword virtual address.
- 0x00E2E0A0 carries both the ROUTE_$PORT and NETWORK_$ME labels;
  0x00E2B950 is AS_$STACK_HIGH (= AS_$INFO.stack_high, AS_$INFO is 0xE2B914).
- FUN_00e2f1d4 is AST_$ACTIVATE_CANNED_SEG(uid, seg) -> ASTE in A0.
- The VTOCE result buffer at A6-0x128 is 0x90 bytes (36 longwords cleared),
  which contradicts vtoce_$result_t's declared 0x150 — see bead source-eb9k.
- (-0x1bc,A6), the location descriptor, is read by the first five
  MST_$MAP_CANNED_AT calls before it is ever written: an original bug.
- Several vfmt literals share their NUL terminator with the next constant
  (msg_salvage's NUL is the FALSE boolean cell at 0xE349DA), and three of the
  "no paging file" strings have no NUL at all — they are tails of one another
  and rely on "%." terminating the format.

## Emitting hand-written asm helpers

Register-argument routines go in `<sub>/sau2/*.s` in GAS m68k syntax (`|`
comments). Since the rebuilt kernel does not live at the original addresses,
reference C symbols and put the original `(off,A5)` / PC-relative form in a
comment. Add small C-ABI wrappers in the same file so the C translation can
call them, and declare only the wrappers in the internal header — the tests
then mock the wrappers.
