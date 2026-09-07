---
name: ml-dxm-fp-chksum-notes
description: Recovered ABIs and layouts for the PROC1_$SET_LOCK gate, the 16-byte DXM callback cell, the FP save-area pointer FP_$SAVEP, and CHKSUM_$GET_CHKSUM.
metadata:
  type: project
---

Fixed in the 2026-09-06 re-emission pass (beads source-fay8, source-wy9y,
source-djly, source-op2l).  See also [[project_audit_2026_09_06]] and
[[feedback_fidelity_gates]].

**PROC1_$SET_LOCK is a gate, not an ML nested procedure.** 0xE20AE4 is one
instruction (`move.w (0x4,SP),D0w`) falling through into the body at
0xE20AE8; `proc1/sau2/set_lock.s` holds both.  ML_$LOCK (0xE20B12) loads D0
itself and `bsr`s the body, so `ml/lock.c` calls the gate.  ML_$LOCK's retry
branch goes to 0xE20B18, so the lock-ordering bookkeeping runs once per call,
and the contended path stays at IPL 7 across PROC1_$EC_WAITN.  Its exit is a
forced IPL 0 (`andi #-0x701,SR`), not an SR restore.  The crash cell 0xE20DE4
(0x000A0002) is now the single global `Illegal_lock_err` in set_lock.s.

**DXM callback cells are 4-byte cells everywhere.** `dxm_$callback_t` (=
m68k_ptr_t) makes `dxm_entry_t` 16 bytes on the host too, so its
`_Static_assert`s are unconditional.  Define a cell with
`DXM_$DEFINE_CALLBACK_CELL(name, fn)`; on a 64-bit host that registers the
function in dxm/dxm_data.c's table through a constructor (a truncated
function address is not a constant expression) and `dxm_$callback_fn()` maps
a cell back to a callable pointer.

**FP_$SAVEP (0xE218D0) is a pointer, not a flag.** It is the base of the
per-AS save-area table; a slot is 0x14A bytes and its LAST longword points at
the top of a frame that grows DOWNWARD
(`[0xFFFF][FPCR FPSR FPIAR][FP0..FP7][FSAVE frame]`).  asid 0 is skipped.
0xE21928 is inside FIM_$BUS_ERR and was never a save area.  The whole FP
cluster is hand-written asm (`fp/sau2/fp_context.s`) because A1 has to
survive between helper calls -- FP_$GET_FP / FP_$PUT_FP cannot be C.

**CHKSUM_$GET_CHKSUM (0xE0A314)** is 20 bytes of hand-written asm, emitted
byte-identical in `chksum/sau2/get_chksum.s`; `chksum/get_chksum.c` is the
`!ARCH_M68K` model and assembles each word from its two bytes so the
big-endian sum survives on a little-endian host.

**How to match a cross-object PC-relative operand in gas.** `sym(%pc)` widens
to a 32-bit base displacement for an external symbol; write `(sym:w,%pc)` to
get the image's 4-byte `303a` / `487a` / `227a` encodings.  m68k FPU
instructions (fsave/frestore/fmovem/fmove.l #imm,%fpcr) assemble fine under
plain `-mcpu=68020`.
