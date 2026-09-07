---
name: smd-win-sio-pacct-pass
description: Layouts and ABIs recovered while closing the 2026-09-07 bead batch - SMD v3 fonts and the blink table, WIN's request record and format-track, SIO2681_$DATA, pacct_record_t, ring transmit counters
metadata:
  type: project
---

## SMD

**v3 font +0x34 is a LONGWORD OFFSET, not an inline map (source-2gs7).** The
drawing path does `add.l (0x34,A2),D0` / `move.b (0x0,A2,D0*0x1),D1b`
(0x00E70426); the width-measure path does `move.b (0x34,A2,D0w*0x1),D1b`
(0x00E70634) and is the **original bug**. Three proofs: both paths share
`adda.l (0x38,A2),A1` for the glyph data (which would be map[4..7] if the map
were inline); every other v3 metric sits past 0x34 (0x42 hdm_size, 0x48
descent, 0x4A ascent, 0x5A char_spacing, 0x6E default_missing); and v1 has the
same shape with the map inline at 0x1A and glyphs biased from 0x92.

**v1 font: char_spacing is +0x10 and default_missing +0x12**, not +0x0A/+0x0C.
`add.w (0x10,A2),D0w` at 0x00E70614 and `add.w (0x12,A2),D4w` at 0x00E706B4.
0x0A/0x0C were guesses; they are field_0a/field_0c now.

**smd_globals_t is 0x1DA8, and +0x1DA0 is the blink pointer table
(source-q85g).** `lea (0x0,A5,D0*0x1),A0` / `movea.l (0x1da0,A0),A1` with
D0 = default_unit*4 (0x00E6FF84). The image has 0x00E84930 = 0x00E2722C =
SMD_$BLINK_CURSOR_1, and SMD_GLOBALS + 0x1DA8 = 0x00E84934 is a code
trampoline (`lea (-0x2,PC),A0` / `jmp SMD_$COPY_FONT_TO_HDM`) that 0x00E6DCFE
calls. Entry 0 is never dereferenced (only unit 1 exists), so its two words
double as last_idm_button / power_off_reported - a union, not an overlap bug.
The table entries must be `uint32_t` VAs, not C function pointers, or the
record grows past 0x1DA8 on a 64-bit host.

**SMD_$WRITE_STR_CLIP's "callbacks" are a context table at 0x00E84934**
(A0 from `lea (-0xc,PC),A0` at 0x00E8493E, kept in A5): +0x14 SMD_$WS_INIT,
+0x18 SMD_$ACQ_DISPLAY, +0x1C SMD_$REL_DISPLAY, +0x20 SMD_$START_BLT, +0x24
and +0x26 the two constant lock-mode words (1 and 0). SMD_$WS_INIT's record
is {font@0, blt_regs@8, hw@0xC, status@0x10, hdm_pos@0x14, font_slot@0x18 IN}.
The second argument of SMD_$WRITE_STR_CLIP is a pointer to the font SLOT word.

**0x00E27036 and 0x00E27070 are two entry points of ONE routine** that fall
into a shared direction switch at 0x00E270A0 and share the crash path at
0x00E271FC. Both are now byte-for-byte in smd/sau2/disp1_int.s; the only
difference is the eventcount advance (IPL-raised + ADVANCE_INT gate versus a
bare EC_$ADVANCE). Verified with objdump: the only byte differences against
the image are the 4-byte address fields of `jsr (SYM).l` relocations.

Status 0x0013000E is "error borrowing display from screen manager"
(source-fnzt).

## WIN

**WIN_$FORMAT_TRACK (0x00E196AA, was FUN_00e196aa) takes TWO arguments**
(source-aiy0): 0x00E197B2 `pea (A2)` then `move.l (0x8,A6),-(SP)`, so arg1 is
the device entry and arg2 the request. It seeks, writes status:=0, mode:=9,
go:=3, waits on (unit ec, TIME_$CLOCKH) with (ec+1, clock+0x28), and checks the
drive status - five passes (`moveq #0x4,D2` + `dbf`). SEEK's fourth argument
here is `st` (TRUE) where WIN_$DO_IO passes zero. EC_$WAIT's result is dropped.

**win_$request_t**: next@0x00 (a VA), status@0x0C, pa@0x10, length@0x14,
volume@0x1E (1-based), flags@0x1F (low nibble = op, 3 = format).

**The disk per-volume table is 0x00E7A560, stride 0x1C, and requests index it
1-based**: `clr.b (-0x4,A1,D1w*0x1)` with D1 = 0x1C*volume clears entry
(volume-1) + 0x18. Four routines share the idiom (WIN_$FORMAT_TRACK 0x00E19764,
WIN_$DO_IO 0x00E1994A, FLP_FORMAT_TRACK 0x00E3DDB0, 0x00E3DFBC). DISK's own
index is 0-based (A5 = 0x00E7A1E8, field +0x378). Bead source-8uxv.

Chip/unit numbers are 1-based in WIN too: base = 0xFFB000 - 0x20 + chip*0x20.

## SIO2681

SIO2681_$DATA at 0x00E2DEB8: error_table is **16** longwords at +0x08 starting
with 0; the command bytes are 0x54=0x10 reset-MR-pointer, 0x56=0x45 reset-error
+enable, 0x58=0x2A reset-RX, 0x5A=0x3A reset-TX, 0x5C=0x0A disable - and
SIO2681_$SET_LINE issues them 0x5C, 0x58, 0x5A first and 0x56 last. MR
templates are WORDS whose HIGH byte is edited: 0x5E = 0x0700 (MR2), 0x60 =
0x0B00 (MR1), MR1 written first. Chip numbers are 1-based (TERM_$INIT passes
the constant word 1 at 0x00E33220), so SIO2681_$PTRS at 0x00E2DF80 is entry 1
of a 16-byte-stride table and there are only two chips. Bead source-jvc9.

## pacct

pacct_record_t is 0x80 bytes built at A6-0x190: flags@0x00 (bits 0 and 1 of the
WORD), stat@0x02, 36-byte SID block@0x04 (ACL_$GET_RE_ALL_SIDS arg1), 12-byte
protection block@0x28 (**arg3**, not arg2), devno@0x34, btime@0x38,
utime@0x3C, stime@0x3E, etime@0x40, a cleared long@0x42, io_write@0x46,
io_read@0x48, proc_uid@0x4A, comm[32]@0x52, mem@0x72. Must be packed. arg6/arg7
are &PROC1_$STATS[cur].pages_written / .pages_read (PROC1_$STATS 0x00E25D20,
stride 0x10, 1-based); ac_mem is compress(60 * (written + read)).

## ring

Transmit counters 0x02..0x1A named from /etc/netmain (source-11rf): xmit_call,
xmitcnt (the two longs), then xmit_nack, xmit_wack, xmit_orun, xmit_apar,
xmit_bus, xmit_nortn, xmit_modem, xmit_error, xmit_tim. Two independent
netmain orderings agree (the display block and the menu's descending selector
keys), and xmit_modem@0x16 is pinned outright: netmain's help says "could not
synchronize ... resulting in an Xmit ESB or biphase error" and 0x00E75C88 bumps
+0x16 on exactly the `andi.w #0xc00` arm. RING_$DATA is at 0x00E261E0
(the map's name for the array; the tree used to call it RING_$STATS), stride
0x3C.

netmain's "xmit bph", "rcv bph" and "xmit esb" are the standalone words
RING_$XMIT_BIPHASE / RING_$RCV_BIPHASE / RING_$XMIT_ESB (0x00E261BC / B8 / BE),
not fields of the record - that is why it shows 25 counters for a 22-field
block.

HDR_CHKSUM (0x00E762CA): sum = 0; for hdr[12] .. hdr[len-1], sum = (sum<<1) +
byte in 16 bits; result is the low byte. A header under 13 bytes checksums to 0.
Its second argument points at a WORD holding the length.

## xns

0x00E17876 is **not** a copy routine (source-mck5): it walks the packet's
descriptor chain (head embedded at packet+0x18, links at +0x08) and returns
TRUE only if every address lies in [0x00D64C00, 0x00D94C00), the network buffer
pool - the module globals at A5+0x70 and A5+0x6C, SIGNED compares. Now
`xns_$pkt_bufs_in_netbuf_pool`. Its result tells xns_$setup_error_header
(0x00E17960, a nested procedure of XNS_ERROR_$SEND reached by a bare `bsr`)
whether to page the payload in with NETBUF_$GETVA or just walk the chain.
XNS_ERROR_$SEND's `0x4C - remaining` length reads a FRAME slot (A6-0x36), not
a field of the packet record.

xns_$pkt_desc_t +0x34 is read three ways by three routines; it is modelled as
a union of the slicings. Its `header` field is still a C pointer, which makes
the record non-host-faithful - bead source-ronb.

Related: [[smd-unit-record-pass]], [[ring-stats-counter-names]],
[[disk-win-vtoc-reemission]], [[feedback_fidelity_gates]].
