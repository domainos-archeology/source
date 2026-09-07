---
name: term-data-block-and-peb-asm
description: TERM_$DATA's two initialised windows and recovered fields, TONE_$CHANNEL as a record, and the byte-identical peb/sau2/int.s (including the one encoding gas cannot emit).
metadata:
  type: project
---

# TERM_$DATA (0x00E2C9F0) and the PEB_ASM module

## TERM_$DATA is almost entirely zero

Map: `D E2C9F0 OS_TERM_INIT size = 1398` (0x00E2C9F0..0x00E2DD88), with
`TTY_$SPIN_LOCK` (+0x1384) and `TERM_$MAX_DTTE` (+0x1388) as interior symbols.
Only **59 bytes** are non-zero, in two windows: +0x00..+0xC3 and +0x138D plus
the keyboard string at +0x1390..+0x1394.  Everything else - the per-line
records at +0x158, the SIO descriptors, the DTTE array - is built at run time
by TERM_$INIT.  Do not describe this block as "image-initialised over its full
extent"; dump it and list the runs first.

Recovered pointer fields (values are the SAU2 map's addresses):

| off | field | value |
|-----|-------|-------|
| 0x18 | ptr_tty_i_rcv | TTY_$I_RCV 0xE1B92A |
| 0x1C | ptr_tty_i_drain | TTY_$I_OUTPUT_BUFFER_DRAINED 0xE1B394 |
| 0x20 | ptr_tty_i_hup | TTY_$I_HUP 0xE1BECE |
| 0x24 | ptr_tty_i_int | TTY_$I_INTERRUPT 0xE1BEA8 |
| 0x28 | ptr_tty_i_err | TTY_$I_ERR 0xE1BE08 |
| 0x48 | ptr_sio_i_tstart | SIO_$I_TSTART 0xE1C7A8 |
| 0x4C | ptr_sio_i_inhibit_xmit | 0xE1C9CE |
| 0x50 | ptr_sio_i_inhibit_rcv | 0xE1C94A |
| 0x54 | ptr_sio_i_err | SIO_$I_ERR 0xE67D9C |
| 0x78 | ptr_dtty_tstart | DTTY_$TSTART 0xE1D6D0 |
| 0x88 | ptr_kbd_rcv | KBD_$RCV 0xE1CCC0 |
| 0x8C | ptr_kbd_drain | KBD_$OUTPUT_BUFFER_DRAINED 0xE1CE96 |
| 0xB4 | ptr_sio_i_tstart_b4 | SIO_$I_TSTART 0xE1C7A8 |
| 0xC0 | ptr_tty_i_rcv_alt | TTY_$I_RCV 0xE1B92A |

`kbd_string_data` is **8** bytes, not 16: 0x1390 + 8 = 0x1398 is the segment
end, and TERM_$SEND_KBD_STRING sends 5 (`pea (0x1390,A5)` 0x00E1AC74 with the
length word at 0x00E1AC9C).  Asserting `sizeof(term_data_t) == 0x1398` catches
this class of overrun.

`TONE_$CHANNEL` (0xE2DC58 = +0x1268) is the SIO2681 channel-A **record**, not a
pointer variable: TONE_$ENABLE does `lea (0x1268,A5),A0` (0x00E1ACFC) and hands
SIO2681_$TONE a pointer to the local holding that address.  It lives in
tone/tone.h as a macro onto TERM_$DATA.  `term/term.h` cannot include
sio2681/sio2681.h (sio/sio.h already includes term/term.h), which is why the
alias belongs in the *using* subsystem's public header.

## peb/sau2/int.s is byte-identical - and one instruction needs .short

`D E24468 PEB_ASM size = 88` holds PEB_$STATUS_REG (0xE24468), PEB_$INT
(0xE2446C) and PEB_$DISP_INT_ADDR (0xE24478).  PEB_$DISP_INT_ADDR is **the
32-bit operand of PEB_$INT's opening `jmp <abs>.l`** (0x00E24476: 4E F9 00 E2
1F 20), which is why it can only be expressed in assembly - writing it
re-targets the jump (SMD_$INTERRUPT_INIT does exactly that).  Emit the opcode
as `.short 0x4EF9` so the label can sit on its own operand.

Every cross-module reference in it is already an absolute long, so `.set`
constants keep the bytes identical.  The single divergence is 0x00E2449E
`and.l #0x3F,%d0` in the Apollo AND-immediate-effective-address form
(**0xC0BC**); gas always emits ANDI (0x0280).  When byte identity is required
rather than instruction equivalence, spell it `.short 0xC0BC` + `.long`.

Verification loop that actually proves it:

    m68k-elf-ld -Ttext=<segment base> -o x.elf build/sau2/<sub>/sau2/<f>.o
    m68k-elf-objcopy -O binary --only-section=.text x.elf x.bin
    gsk read <base> <len> | sed 's/|.*//' | cut -c11- | tr -d ' \n'   # then unhexlify
    # note: gsk read's length argument may return more than asked; truncate.

Related: [[handwritten-asm-verification]], [[module-block-alias-pattern]],
[[smd-term-sio-layouts]], [[biased-tables-and-dead-cells]].
