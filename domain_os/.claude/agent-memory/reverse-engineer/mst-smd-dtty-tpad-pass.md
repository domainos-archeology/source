---
name: mst-smd-dtty-tpad-pass
description: Recovered facts for mst_$va_to_pte, SMD_$BORROW_DISPLAY, SMD_$GET_UNIT_EVENT, DTTY_$INIT and TPAD_$DATA - biased MSTE page numbers, EC_$WAIT's by-value arrays, the DTTY module block and the tpad 48-bit clock.
metadata:
  type: project
---

Findings from the 2026-09-07 fidelity pass over mst/, smd/, dtty/ and tpad/.

**MSTE_PAGES holds ONE-BASED page numbers.** `mst_$va_to_pte` (0xE4411C)
builds the entry as `0xEF6400 + MST[..]*0x400 + (seg & 0x3F)*16` and then
`lea (-0x400,A1)` at 0xE4419C. The subtraction is part of the address, not a
fixup on the store, so page 1 lands exactly on MSTE_PAGES (0xEF6400, named in
the SAU2 map). Writing the base as 0xEF6000 *and* subtracting 0x400 is the
classic off-by-a-page bug here.

**EC_$WAIT takes two 3-element arrays BY VALUE** (24 bytes, `lea (0x18,SP),SP`
to clean up). The push order at 0xE6F62C-0xE6F63E is vals[2], vals[1],
vals[0], ecs[2], ecs[1], ecs[0], so `ecs` is the first argument.
`ec/ec.h` already wraps them as `ec_$wait_ecs_t` / `ec_$wait_vals_t`.
EC_$WAIT_1 (with a timeout) is a *different* entry point - do not substitute.

**Domain boolean returns are tested with `bpl` = FALSE.** `SMD_$BORROW_DISPLAY`
0xE6F5AA `tst.b D0b / bpl -> error`: a NON-negative `smd_$validate_unit`
result is the *invalid* unit. Getting this backwards silently inverts the
whole validation.

**SMD_$GET_UNIT_EVENT (0xE6EEA8) leaves D2 uninitialised on most arms.** The
16-word jump table at 0xE6EF16 sends internal types 1-6, 9, 0xA (and, via the
`bcc` at 0xE6EF0A, everything >= 0x10) straight to 0xE6EF68 without loading
D2 or the reply's last word. Reproduce that; do not invent a mapping. Its
14-byte reply takes the entry's first four fields at the SAME offsets, so
`smd_unit_event_t`'s first longword is the packed cursor position and its
second is the timestamp (renamed 2026-09-07; `smd_idm_event_t` still carries
the old shifted names).

**The DTTY module block is 0xE2E00C..0xE2E017** (SAU2 map: `D E2E00C DTTY
size = C`). +0x00 disp_type, +0x02 DTTY_$CTRL, +0x04 and +0x06 two
module-local flag bytes written only by DTTY_$INIT, +0x08 DTTY_$USE_DTTY.
DTTY_$INIT's `bmi` at 0xE34C64 **leaves** when USE_DTTY is true: the
SMD_$ASSOC / clear-window / load-font tail is the DTTY-*disabled* path.
Its window record is an `smd_rect_t` {x1, x2, y1, y2}; the maxima are written
in the display-type arm and the two zeros only later, at 0xE34C92/96.

**Domain Pascal string constants end with `%` and are padded with `$`.**
In DTTY_$INIT's constant pool (0xE34CEE..0xE34D2B) each `pea (d,PC)` string
carries its `%` terminator and an odd-length literal is followed by a single
`$` pad byte. The one-character cell at 0xE34D0A is a real argument: it is
`dtty_$report_error`'s `context`, whose leading `$` means "no context".

**tpad clocks are 48-bit and the delta uses the LOW 32 bits.** A
`tpad_$data_packet_t` is 16 bytes with the clock at +4 (high 32 at +4, low 16
at +8). TPAD_$DATA reads the low 32 bits as one longword at +6 (0xE69640) and
subtracts the same field of `tpad_$last_clock` (0xE825C2 = 0x164+2), then
stores all six bytes back with a long plus a word (0xE6967E/0xE69684).
Its acceleration arm fires only when the velocity is **strictly greater than
100** (`cmpi.w #0x64 / ble`), and the snap-to-horizontal arm's `clr.w D1w` at
0xE6967A **falls through** into `st D4b`, so it raises the same flag.

**`clock_t` is 6 bytes on m68k and 8 on the host** (`m68k-elf-gcc` aligns int
to 2). Any record a host test must lay out byte-exactly and that embeds a
48-bit clock has to spell it as `uint32_t` + `uint16_t` inside a `packed`
struct - embedding `clock_t` in a packed struct does not shrink it.
