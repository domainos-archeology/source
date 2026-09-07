---
name: svc-dispatcher-block
description: The SVC_CATCHER dispatcher block 0xE7B044..0xE7B2DD - shared error stubs, the branch-chain trick, the gas CMP-immediate macros, and the whole-block byte verification recipe.
metadata:
  type: project
---

# The SVC TRAP dispatcher block (0xE7B044..0xE7B2DD, 666 bytes)

`svc/sau2/trap0.s .. trap8.s` are one contiguous image region, not eight
independent routines.  Every file's size and every branch displacement depends
on its neighbours, so they must be re-emitted and verified together.

**Why:** the dispatchers share three error stubs and reach them with 8-bit
branches across what we split into separate objects; TRAP7's `bhi.b` to
0x00E7B174 sits at displacement -128, the exact limit.

**How to apply:** never change one trapN.s size in isolation - re-run the
whole-block check below.

## Layout (SAU2 map + entry sizes)

    E7B044 TRAP0 0x18 | E7B05C TRAP1 0x38 | E7B094 TRAP2 0x44
    E7B0D8 TRAP3 0x48 | E7B120 TRAP4 0x5C | E7B17C TRAP5 0x5C
    E7B1D8 TRAP7 0x68 | E7B240 TRAP8 0x9E (there is no TRAP6)

trap1/trap2/trap3/trap5 each end with a 2-byte zero pad that realigns the next
entry to a longword; trap0/trap4/trap7 have none.  Emit the pads as `.short 0`.

Shared stubs (all global, all reached cross-object):

    E7B08E TRAP1_BAD_PTR_STUB      bra.w -> E7B178
    E7B0CE TRAP2_INVALID_STUB      bra.w -> E7B28E (TRAP8_INVALID)
    E7B0D2 TRAP2_ILLEGAL_USP_STUB  bra.w -> E7B2C6
    E7B170 TRAP4_INVALID_STUB      bra.w -> E7B28E
    E7B174 TRAP4_ILLEGAL_USP_STUB  bra.w -> E7B2C6
    E7B178 TRAP4_BAD_PTR_STUB      bra.w -> E7B2A0 (BAD_USER_PTR)
    E7B23C TRAP7_BAD_PTR_STUB      bra.w -> E7B2A0

The illegal-USP stubs target **E7B2C6 (`SVC_$ILLEGAL_USP_JMP`), not E7B2C4**:
only SVC_$TRAP8 builds a LINK frame, so the fixed-argument dispatchers must
skip the `unlk %a6` at E7B2C4.  An earlier pass wrote `bra.w FIM_$ILLEGAL_USP`
instead - wrong target *and* the source of two link truncation errors.

**The branch-chain trick:** SVC_$TRAP0's `bcc.b` at E7B048 jumps to E7B060,
which is *SVC_$TRAP1's own `bcc.b`* (`SVC_$TRAP1_RANGE_CHECK`).  Nothing
between them touches the CCR, so TRAP1's branch re-tests TRAP0's carry and
takes the out-of-range path.  Two range checks, one error stub.

## Dispatch tables (all `lea (d16,PC)` in the image)

    E7B2DE TRAP0(32)  E7B35E TRAP1(66)  E7B466 TRAP2(133) E7B67A TRAP3(155)
    E7B8E6 TRAP4(131) E7BAF2 TRAP5(99)  E7BC7E TRAP7(59)  E7BD6A TRAP8(56)
    E7BE4A TRAP8_ARGCOUNT(56 bytes), ending E7BE82 +2 pad = E7BE84 = PROC2

Each base = previous base + entries*4, which is how to settle a disputed
address (svc_tables.c's comment for TRAP1 says E7B360; the chain says E7B35E).

## gas CMP-immediate macros

`svc/sau2/svc_macros.inc` (included as `.include "svc/sau2/svc_macros.inc"` -
gas resolves it against the Makefile's working directory, `domain_os/`):

    cmp_w_imm imm, dreg   ->  0xB07C | (dreg<<9), imm.w    CMP.W #imm,Dn
    cmp_l_imm imm, dreg   ->  0xB0BC | (dreg<<9), imm.l    CMP.L #imm,Dn

The register operand is a *number*, not `%dN`.  gas always spells these CMPI
(0x0C40 / 0x0C80) and offers no syntax for the CMP form.  `cmpa.l #imm,%aN`
needs no macro - CMPA has no CMPI variant, so gas already emits 0xB3FC.

The generic `%.o: %.s` rule cannot see a gas `.include`, so the Makefile has an
explicit `$(svc_ASM_OBJS): svc/$(TARGET_SAU)/svc_macros.inc`.

## Whole-block byte verification (the check that actually proves it)

Per-file `objcopy | diff` leaves every cross-object field zeroed, so it can
only show "differs in relocation fields".  Link all eight objects at the ROM
address with the tables defined at theirs, and the block must be *identical*:

    m68k-elf-ld -Ttext=0xE7B044 -e 0 \
      --defsym 'FIM_$EXIT'=0xE228BC --defsym 'SVC_$TRAP0_TABLE'=0xE7B2DE ... \
      -o block.elf trap0.o trap1.o trap2.o trap3.o trap4.o trap5.o trap7.o trap8.o
    m68k-elf-objcopy -O binary --only-section=.text block.elf block.bin
    cmp block.bin <concatenated gsk-read image>

This resolves every PC8/PC16 displacement and catches a wrong stub target that
a per-file diff would silently pass.

## Known link artifact (bead source-a5t8)

All 11 PC16 table `lea`s overflow under `sau2.ld`, which puts `.data` ~0x11000
past `.text` where the image keeps the tables 0x290..0xC08 bytes past the code.
**GNU ld prints only the first 10 relocation overflows tree-wide** and then says
"additional relocation overflows omitted" - so a clean-looking link is not
evidence that yours resolved.  Grep for that omission line before believing a
truncation count.
