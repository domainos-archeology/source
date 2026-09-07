---
name: short-branch-across-objects
description: Why an image `bsr.b`/`bra.b` to a symbol in another object cannot be emitted symbolically in this tree, and the literal `.short` convention used instead.
metadata:
  type: project
---

gas *does* keep the 2-byte form for `bsr.b SYM` with an external symbol: it
emits `61 00` plus an `R_68K_PC8` fixup on the displacement byte (objdump
misreads the `61 00` as `bsrw`, ignore that).  The problem is the **linker**,
not gas: an 8-bit displacement reaches only +/-127 bytes, and `sau2.ld`'s
per-subsystem gathers take objects in Makefile order - every `<sub>/*.c`
object precedes the `<sub>/sau2/*.s` objects - so a hand-written routine's
short branch into a C sibling lands kilobytes away and the link fails with
`relocation truncated to fit: R_68K_PC8`.

**Why:** source-uwxz's image-order gather fixed the 16-bit `bsr.w`/`lea
(sym:w,%pc)` overflows (+/-32KB is enough to span a subsystem), but it does
nothing for 8-bit ones.

**How to apply:** where the image has a byte branch to another object's symbol,
emit the image encoding literally - `.short 0x6134  /* bsr.b ADVANCE_INT */` -
with a comment giving the image address, the computed target, and a
`TODO(<bead>, <addr>)` for the link-order fix.  `bsr.w` is *not* an option: it
is 4 bytes and displaces every following instruction.  Confirmed sites:
`ec/sau2/advance.s` 0xE206F6 (`61 34`) and
`ec/sau2/advance_without_dispatch.s` 0xE20722 (`61 08`), both to `ADVANCE_INT`
0xE2072C (= C routine `ec/advance_int.c`) - see source-p49c / source-mc3k.

Displacement arithmetic differs by form and is easy to get wrong:
`bsr.b` target = addr + 2 + disp8; `bsr.w` target = (addr + 2) + disp16, i.e.
both are measured from the *extension word*, not from the next instruction.

See [[handwritten-asm-verification]] for the objcopy-vs-`gsk read` recipe and
[[sau2-ld-section-placement]] for the 16-bit ordering work.
