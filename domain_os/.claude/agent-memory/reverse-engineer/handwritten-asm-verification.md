
## gas addressing-mode forms that must be forced (2026-09-06)

GNU as on a 68020 target picks the *widest* form when a PC-relative operand is
symbolic, which silently changes the instruction length:

- `lea SYM(%pc),%a1` emits `43fb ...` (full extension word, 6+ bytes).
  Write `lea (SYM:w,%pc),%a1` to get the original `43fa dddd`.
- Same for `pea (SYM:w,%pc)` and `jsr (SYM:w,%pc)`.
- `jmp (LABEL:b,%pc,%d0.w)` forces the brief format `4efb dd`.

Immediate-source vs. immediate-opcode forms still need `.short`; the ones seen
so far are AND.W `0xC07C` (gas gives ANDI.W `0x0240`), OR.W `0x807C` (gas gives
ORI.W `0x0040`) and CMP.W `0xB07C` (gas gives CMPI.W `0x0C40`).

**Verification recipe** (both smd/sau2/disp1_int.s and start_blt.s):
`m68k-elf-gcc -c` then `m68k-elf-objcopy -O binary --only-section=.text`, and
compare against the image bytes from `gsk read`. Every remaining difference
must line up with an entry in `m68k-elf-objdump -r` - if a differing byte is
not inside a relocation field, the transcription is wrong.
