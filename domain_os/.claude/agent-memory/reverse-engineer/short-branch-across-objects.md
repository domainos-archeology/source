---
name: short-branch-across-objects
description: An image `bsr.b`/`bra.b` to another object's symbol CAN be emitted symbolically - the fix is sau2.ld ordering plus a 2**1-aligned section, not a `.short` literal.
metadata:
  type: project
---

gas keeps the 2-byte form for `bsr.b SYM` with an external symbol: it emits
`61 00` plus an `R_68K_PC8` fixup on the displacement byte (objdump misreads
the `61 00` as `bsrw`; ignore that).  The linker resolves it fine **provided
the two objects end up adjacent**.  An 8-bit displacement reaches only
+/-127 bytes, and `sau2.ld`'s per-subsystem gathers take objects in Makefile
order - every `<sub>/*.c` object before the `<sub>/sau2/*.s` objects - so a
naive gather strands the branch and the link fails with
`relocation truncated to fit: R_68K_PC8`.

**Do NOT emit `.short 0x6134` literals for this.**  An earlier pass did, which
silently drops the symbolic dependency and hard-codes a displacement that is
wrong in the linked image.  The real fix, and the one now in the tree:

1. Spell the run out in `sau2.ld` in SAU2-map order (see
   [[sau2-ld-section-placement]]), so the branch and its target are neighbours.
2. Make sure no *padding* creeps in between them - see the alignment trap
   below, which is what kept the ec run 2 bytes off for a while.
3. Then just write `bsr.b SYM`.

Displacement arithmetic differs by form and is easy to get wrong:
`bsr.b` target = addr + 2 + disp8; `bsr.w` target = (addr + 2) + disp16, i.e.
both are measured from the *extension word*, not from the next instruction.
`bsr.w` is not a substitute for `bsr.b`: it is 4 bytes and displaces
everything after it.

## The gas `.text` 2**2 alignment trap

gas fixes the pre-created `.text` section's alignment at **2**2** and gives no
directive to lower it - `.balign 2`, `.section .text,"ax",@progbits` and
`.pushsection` all leave `Al 4` in `readelf -S`.  So any hand-written `.s`
whose image size is not a multiple of 4 gets padded by ld, and every object
after it in the run slides off the image's gaps.  (`ec/sau2/advance_all.s` is
0x16 bytes and cost the run 2 bytes this way.)

A section created *by name* with `.section` starts at 2**0, and `.balign 2`
raises it to exactly the m68k minimum:

        .section .text.ec_advance_int,"ax",@progbits
        .balign 2

Then have `sau2.ld` gather `*(.text.ec_advance_int)` instead of
`*/ec/sau2/advance_int.o(.text)`.  Name-based gathers must stay ahead of the
generic `*(.text .text.*)`, which would otherwise swallow them (ld's
first-match rule).

**How to apply:** whenever a hand-written `.s` sits inside an image run whose
internal gaps must be reproduced, give it its own `.text.<name>` section with
`.balign 2` and name that section in `sau2.ld`.  Verify with a scratch link
(`-T` at the run's image address, externals `--defsym`'d to their image
addresses) - the whole run should come out byte-identical to `gsk read`, not
just "identical except relocation fields".  Worked example: the 0xE6-byte
EC_$ADVANCE..ADVANCE_ALL_INT run, 0xE206EE..0xE207D3 (source-0ke7).

See [[handwritten-asm-verification]] for the objcopy-vs-`gsk read` recipe and
[[sau2-ld-section-placement]] for the 16-bit ordering work.
