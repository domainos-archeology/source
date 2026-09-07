
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

Two more forms confirmed 2026-09-07 (smd/sau2/cursor_thunks.s, 0x00E15B90):
- AND.W immediate-source `0xC27C` (D1) needs the same `.short` treatment as
  `0xC07C`; the ANDI.W form gas picks is `0x0241`.
- A `bra.w` whose target is the *next* instruction (`6000 0002`, a real
  artifact in several SMD_WIRED routines) must keep the explicit `.w`; gas
  relaxes a plain `bra` to `6002` and the routine comes out two bytes short.
