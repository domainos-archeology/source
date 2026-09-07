---
name: svc-catcher-tail
description: SVC_CATCHER's shared fault tail (0xE7B298-0xE7B2DD), which trapN.s owns it, and how to prove a .s file is byte-exact against the image.
metadata:
  type: project
---

# SVC_CATCHER fault tail and byte-exact .s verification

**The tail lives in `svc/sau2/trap8.s`, not trap5.s.** Segment `SVC_CATCHER`
(0xE7B044, size 0xE40) holds SVC_$TRAP0/1/2/3/4/5/7/8 (map addresses
E7B044/E7B05C/E7B094/E7B0D8/E7B120/E7B17C/E7B1D8/E7B240 - there is **no**
SVC_$TRAP6) followed by one shared fault tail:

| addr | symbol | notes |
|------|--------|-------|
| 0xE7B298 | SVC_$INVALID_SYSCALL | `move.l #0x00120007,%d0` |
| 0xE7B2A0 | SVC_$BAD_USER_PTR | `move.l #0x0012000B,%d0`, falls through |
| 0xE7B2A6 | SVC_$GENERATE_FAULT | SP := OS_STACK_BASE[PROC1_$CURRENT]-8, push D0, `jsr FIM_$GENERATE` |
| 0xE7B2C4 | SVC_$ILLEGAL_USP_UNLK | `unlk %a6` + `jmp FIM_$ILLEGAL_USP` |
| 0xE7B2CC | SVC_$UNIMPLEMENTED | tears the LINK frame down when A1 == SVC_$TRAP8_TABLE, then #0x0012001C |

SVC_$TRAP8 is the block immediately above the tail and is the **only**
dispatcher reaching it with 8-bit branches (`bhi.b` 0xE7B26A -> 0xE7B2C4,
`bls.b` 0xE7B276 -> 0xE7B2A0). An 8-bit branch to another object file gives
gas an `R_68K_PC8` whose zero placeholder disassembles as a *word* branch and
almost always truncates at link time - so the tail must be assembled in the
same file as TRAP8. TRAP0-5/7 reach it through their own `bra.w` stubs
(word relocs, fine across objects) and `.extern` these labels.

**There is no FIM_$FAULT and no PROC1_$FAULT_INDEX/PROC1_$FAULT_TABLE.** An
older trap8.s invented all three by mis-decoding 0xE7B2A6-0xE7B2C6. The real
cells (SAU2 map) are FIM_$GENERATE 0xE214A8, FIM_$ILLEGAL_USP 0xE2158A,
FIM_$EXIT 0xE228BC, PROC1_$CURRENT 0xE20608, OS_STACK_BASE 0xE25C18.

## Proving a .s is byte-exact

```
m68k-elf-gcc $SAU2_FLAGS -c -o t.o x.s
m68k-elf-objdump -dr t.o          # gives every reloc offset+width
m68k-elf-objcopy -O binary --only-section=.text t.o t.bin
gsk read <addr> <len>             # split on '|', drop the 9-char addr column,
                                  # hex-decode; a naive "\s\s" regex loses
                                  # half of each gsk line
```
then diff, masking exactly the reloc byte ranges. Each masked field should
hold the *correct* target in the image (e.g. `00e214a8` at the `jsr` operand)
- that is what identifies the callee.

`gsk` cannot create functions, so a region Ghidra never disassembled (like
0xE7B240 was) must be decoded by hand from `gsk read`; `gsk label add` and
`gsk comment disassembly` still work there.

**gas quirk:** `cmp.w #imm,%dn` always assembles as CMPI.W (`0c40`), but the
Apollo assembler emitted the legal CMP-immediate form (`b07c`). Use
`.short 0xb07c, 0x0038` with a comment when byte fidelity matters.
