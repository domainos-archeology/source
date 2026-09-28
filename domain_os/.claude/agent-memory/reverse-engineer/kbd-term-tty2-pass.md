---
name: kbd-term-tty2-pass
description: KBD state machine, KBD_$GET_DESC/PUT quirks, TERM_$SET_REAL_LINE_DISCIPLINE's two targets (sio_desc_t vs kbd handler cell), the shared OS_TERM word cells, TERM_$GET_EC's embedded eventcounts, and SIO2681_$TONE's cell argument - recovered in the tty2 re-emission (2026-09-19..22)
metadata:
  type: project
---

Verified against the disassembly during batch tty2 (kbd/, term/, tone/, tpad/).

**Why:** the earlier C in these files mis-typed the spin-lock argument (value
instead of address), indexed a `void **` on a 64-bit host for byte offsets,
invented private copies of shared literal cells, and had the manual-stop arm
fall through into process_key, none of which the image does.

**How to apply:** trust these over the decompiler; the beads listed carry the
still-open shape questions.

## KBD (A5 = 0xE2DDE4, map "D E2DDE4 KBD size = D4")
- State table entries are {key, next} byte pairs at A5+0xC0; kbd_$state_lookup
  returns their address.  `next` high nibble = action (13-way jump table at
  0xE1CCFE), low nibble = next state, 0xF = DAT_00e2ddec[kbd_type_idx].
- Action 2 (manual stop): when MMU_$NORMAL_MODE() is false it crashes with
  Term_Manual_Stop_err (0xE1CE8C), calls KBD_$CRASH_INIT, `or.w #0xf,(A3)` on
  the table entry, then `bra.w` to the state update - it does NOT run
  process_key.  When normal mode is true it is just action 1.
- Touchpad sample: state+0x1C..0x2B (delta_time, clock, tpad_x/y/z, pad_29)
  is the 16-byte suma_sample_t copied into TERM_$TPAD_BUFFER.samples[head]
  ((0x5c,A5)); the DXM data cell holds &state->tpad_buffer (state+0x30).
- Handler drain: fn(user_data, translate(key)) only for mode-0 keys; the
  handler is a Pascal function (result slot) whose result is dropped.
- KBD_$GET_DESC returns dtte[line].alt_handler; on its two error exits A0 is
  stack residue (the -0x8 slot is never written).  Line 0 with discipline != 1
  calls TERM_$SET_DISCIPLINE(line_ptr, &WORD 1 @0xE1AAA6).
- KBD_$PUT's body call (bsr 0xE1CA8A) is a bare `rts`: an empty procedure.
- kbd_$set_type reads type_str[1] for the KTT index even when type_len < 2.
- KBD_$INIT's default type cell 0xE333DA is "2\0", length 1.

## TERM
- TERM_$SET_REAL_LINE_DISCIPLINE: disciplines 0/3 write the sio_desc_t that
  SIO_$INIT_DESC stored in dtte.tty_handler (+4 owner = dtte.handler_ptr,
  +0x28..+0x34 = TERM_$DATA +0x18..+0x24 or SUMA_$RCV); 1/2 write the kbd
  descriptor's handler cell (dtte.alt_handler) with 0 / ptr_tty_i_rcv_alt.
  The lock is `pea (0x1384,A5)` = &TERM_$DATA.tty_spin_lock (by ADDRESS).
  Missing descriptor -> 0xb0007 without storing the discipline; discipline
  >= 4 is stored with nothing else touched.
- Shared literal word cells in the OS_TERM code segment: 0xE66896 = 1
  (TTY_$K_GET conditional option; READ and READ_COND), 0xE66898 = 0 (READ
  blocking, WRITE, INQUIRE option 0), 0xE667C4 = 2.  Now term_$const_word_1
  / _0 (term/read.c) and term_$const_word_2 (term_data.c).
- TERM_$GET_EC passes &dtte+0x0C / +0x18 to EC2_$REGISTER_EC1: the DTTE
  embeds two 12-byte eventcounts (bead source-zbdk); its ec_ret gets the A0
  handle as one longword.
- TERM_$READ on a GET_REAL_LINE failure returns the low word of the buffer
  pointer (D2 still holds it).
- TERM_$INQUIRE: 33-way table at 0xE66DB8; option 7's error exit skips
  STATUS_CONVERT; option 21 stores a word whose upper 12 bits are stack
  residue in the image.
- TERM_$P2_CLEANUP: owner UIDs at TERM_$DATA + 0x1A4 + i*0x4DC, i = 0..2.

## TONE / SIO2681
- SIO2681_$TONE (0xE1D172) takes a CELL holding the channel address as its
  first argument (`move.l (A0),D2`), the enable byte pointer, and an unread
  status cell: three args, 12 bytes.  Header/definition fixed 2026-09-22
  (bead filed); TONE_$ENABLE builds the cell with `lea (0x1268,A5),A0`.
- TONE_$TIME cells: 0xE1735A = 0xFF, 0xE1735C = 0x00, 0xE1735E = word 0.

## TPAD
- tpad/data.c, inquire.c, punch_impact.c, re_range.c, set_cursor.c,
  set_mode.c were walked against their listings and left unchanged (exact).
  INQUIRE_UNIT and SET_UNIT_CURSOR validate the GLOBAL tpad_$unit, not the
  *unitp argument; SET_UNIT_MODE / RE_RANGE_UNIT / *PUNCH_IMPACT validate *unitp.

## Host-test traps met here
- `clr.b (A0)` through a word pointer (INQUIRE option 24) clears the FIRST
  byte; assert on `((uint8_t *)&v)[0]`, not on the word's value.
- A descriptor reached through ARCH_VA_TO_PTR must live inside the arena
  ARCH_HOST_VA_BASE points at (place it at arena + VA, not a separate static).
- Kernel headers must come AFTER <stdio.h>/<string.h> in a test, or
  macOS _string.h breaks on the Domain typedefs.
