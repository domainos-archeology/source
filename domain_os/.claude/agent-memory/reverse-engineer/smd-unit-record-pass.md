---
name: smd-unit-record-pass
description: "SMD display-unit record, HDM free list, font table, info table and position layouts recovered while migrating smd/ off the old slot view (beads source-nhzy/5nq5/06qh/1god/c3ap, 2026-09-06)"
metadata:
  type: project
---

Recovered during the source-nhzy migration; all offsets verified against the
disassembly, all now carry `_Static_assert`s under `ARCH_M68K`.

**Why:** the whole SMD subsystem addresses its per-unit state through a
*biased* pointer, and almost every earlier C file modelled that as an array of
0x10C-byte "slots" indexed by the unit number, which is off by one slot for
every field below +0x18.

**How to apply:** use these when touching any SMD file; check the cited
address before trusting a field name.

## The addressing rule

`A3 = 0x00E2E3FC + unit * 0x10C`, and the record's fields live at
displacements **-0xF4 .. +0x17**. So the record *starts* at `A3 - 0xF4`.
`smd_$unit_rec(unit)` in `smd/smd_internal.h` is exactly that computation.
`SMD_DISPLAY_UNITS` is now a raw byte block, because 0x00E2E3FC..0x00E2E413 is
not a record at all - it is the two standalone eventcounts SMD_EC_1 and
SMD_EC_2. Unit 1's record is 0x00E2E414..0x00E2E51F.

Record fields (offsets from the record base):

| off | field | evidence |
|---|---|---|
| 0x00 | `hw` (smd_display_hw_t *) | 0x00E34DC2 `movea.l (-0xf4,A3),A4` |
| 0x04 | `owner_asid` | 0x00E6D8F6 `clr.w (-0xf0,A3)` in SMD_$ASSOC |
| 0x06 | `borrowed_asid` | 0x00E75272 |
| 0x08 | `field_08` - the ASID with a pending async operation | SMD_$BLT 0x00E6EDF8, SMD_$SOFT_SCROLL 0x00E6F398, SMD_$DM_COND_EVENT_WAIT 0x00E6F15E |
| 0x10 | `mapped_addresses[57]`, **1-based on the ASID** | indexed as `(-0xE8,A3) + asid*4` (SMD_$MAP_DISPLAY_U 0x00E6F918) |
| 0xF4 | `font_table` (smd_font_entry_t *, 8 entries, 1-based) | SMD_$LOAD_FONT 0x00E6DC5A reads it as `(A2)` |
| 0xF8 | `hdm_list` (smd_hdm_list_t *) | SMD_$ALLOC_HDM 0x00E6D974 `movea.l (0x4,A0),A2` |
| 0xFC | `ctrl_regs` (volatile uint16_t *) | SMD_$EOF_WAIT 0x00E6F408 `movea.l (0x8,A3),A1` |
| 0x100 | `display_uid` (uid_t, 8 bytes) | SMD_$MAP_DISPLAY_U 0x00E6F940 `pea (0xc,A3)` -> MST_$MAP's uid arg |
| 0x108 | `display_base` (uint32_t) | SMD_$DISPLAY_LOGO 0x00E70290 `movea.l (0x14,A0),A4` |

The `+0xFC` register base is passed **by value** to SMD_$START_BLT,
SMD_$START_SCROLL and SMD_$CONTINUE_SCROLL; all three used to be typed
`ec_$eventcount_t *` in the C, which is wrong.

## Everything else in SMD is 1-based too

- **Display info table** (0x00E27376, 0x60 stride): addressed as
  `base + unit*0x60 - 0x60`. `smd_$validate_unit` 0x00E6D722
  `tst.w (-0x60,A0,D1*0x1)`, SMD_$INQ_DISP_TYPE 0x00E6DE4C. Use
  `smd_$unit_info(unit)`. Recovered fields: `kbd_cursor_pos` at +0x32,
  `field_36` at +0x36, `kbd_cursor_type` at +0x38.
- **Font table**: entry `s` is at `font_table + s*8 - 8`
  (SMD_$LOAD_FONT 0x00E6DCC4, SMD_$UNLOAD_FONT 0x00E6DD5A). Entry layout is
  `{void *font_ptr; smd_hdm_pos_t hdm_pos;}` - SMD_$LOAD_FONT hands
  `&entry->hdm_pos` straight to SMD_$ALLOC_HDM, it keeps no local copy.
- **HDM free list**: `{uint16_t count; smd_hdm_block_t blocks[];}` with the
  blocks starting at **+0x02**, no padding. SMD_$ALLOC_HDM walks it with
  `A0 = list + 4 + 4*(k-1)`, reading the size at `(A0)` and the offset at
  `(-0x2,A0)` (0x00E6D98A / 0x00E6D992 / 0x00E6D9B4). `smd_$reset_unit_display`
  seeds it with one block: `{0x0000,0x0357}` for display types 2 and 6,
  `{0x0031,0x03CE}` otherwise - and that second pair is exactly the
  `[0x31,0x3FF]` range SMD_$FREE_HDM validates for type 1.
- **Request queue**: entry `index` is `request_queue[index - 1]`
  (`A5 + 0x17D0 + index*36`, 0x00E6F0CA).

## Positions are stored Y first

Every SMD position is a longword with **X in the low half and Y in the high
half**, i.e. on m68k the row is at +0x00 and the column at +0x02:

- `smd_cursor_pos_t` is now a `uint32_t` typedef plus `SMD_POS_X`/`SMD_POS_Y`/
  `SMD_POS_MAKE` (in smd.h). Proof: smd_$write_str_clip_impl 0x00E703F6.
- `smd_hdm_pos_t` is `{uint16_t y; uint16_t x;}`. Proof:
  SMD_$COPY_FONT_TO_HDM 0x00E70328 `move.w (A4),D1w` / `lsl.l #0x7,D1`
  (row * 0x80 bytes) versus 0x00E70340 `move.w (0x2,A4),D1w` / `lsr.w #0x3`
  (byte column). Corroborated by SMD_$ALLOC_HDM writing the constant 800 to
  +0x02 for the 800-pixel-wide display type 1.
- tpad's `union smd_$pos_t {int32_t raw; struct {int16_t y; int16_t x;};}`
  already had it right and is independent corroboration.

## Display types 1 and 2

SMD_$INIT 0x00E34E00 gives **type 1** max_x 0x31F / max_y 0x3FF = 800x1024,
**portrait**; 0x00E34E0E gives **type 2** 1024x800, **landscape**. The
constant names in smd_internal.h used to be the other way round. HDM lives to
the *right* of the visible area on type 1 (column 800) and *below* it on type 2
(the 224-line band from row 800, tiled 224 columns at a time).

## smd_globals_t additions

- `display_map_length[18]` occupies 0x00..0x47: SMD_$MAP_DISPLAY_U passes
  `pea (0x0,A5,D1w*0x1)` with `D1 = display_type * 4` as MST_$MAP's length
  argument (0x00E6F938), and SMD_$UNMAP_DISPLAY_U passes the same cell.
- `field_173d`/`field_173e`/`field_173f` at 0x173D..0x173F, touched only by
  `smd_$reset_display_globals`.
- ~~`SMD_GLOBALS.default_unit` and `SMD_DEFAULT_DISPLAY_UNIT` are different
  words~~ **WRONG** - see "0x00E84924 is not a separate global" below. They
  are the same cell; `SMD_DEFAULT_DISPLAY_UNIT` no longer exists.

## Newly named functions

- `smd_$reset_unit_display` (0x00E6D736, was FUN_00e6d736)
- `smd_$reset_display_globals` (0x00E6D7E2, was FUN_00e6d7e2) - note it never
  loads A5; it runs on the *caller's* A5
- `smd_$reset_display_state` (0x00E6F514, was FUN_00e6f514)

## Hand-written assembly in SMD

`SMD_$LOCK_DISPLAY` (0x00E15CCE) and `SMD_$BIT_SET` (0x00E15D12) are now
`smd/sau2/*.s`; `SMD_$START_BLT`'s body at 0x00E15D1E has the same shape and
still needs moving (bead source-x81r). The convention that worked: keep the C
file as a portable model behind `#if !defined(ARCH_M68K)` so the m68k build
links the assembly and `make test` keeps a callable version. Both return a
Domain boolean in the **low byte** of D0 (`clr.b`/`st`, `seq`), so the C
prototype is `int8_t`, not `int16_t`.

## The display "info table" IS the hardware record (source-fqne)

`SMD_DISPLAY_INFO` at 0x00E27376 and the record `smd_display_unit_t::hw`
points at are the **same 0x60-byte object**. Ghidra already labels it
`SMD_$DISPLAY_COM`. Proof: SMD_$INIT stores the literal into the hw pointer
(0x00E34D92 `move.l #0xe27376,(0x18,A0)`) and then writes +0x50/+0x54 through
`A4 = rec->hw` (0x00E34E00-0x00E34E18). `smd_display_info_t` is now a typedef
alias of `smd_display_hw_t`; the old struct's clip window at +0x0C..+0x1B was
invented.

**There is exactly one entry.** 0x00E273D6 is `SMD_TIME_$COM`, so the table is
0x60 bytes long, and `smd_$validate_unit` accepts only unit 1
(0x00E6D70A `cmpi.w #0x1,D0w`). smd_data.c still over-allocates 4 - bead
source-9j2l.

Offsets beyond the ones listed earlier:

| off | field | evidence |
|---|---|---|
| 0x02 | `lock_state` | SMD_$START_BLT 0x00E15D68 |
| 0x10 | `op_ec` | SMD_$GET_EC 0x00E6FDFA `pea (0x10,A3)` |
| 0x1C | `field_1c` | SMD_$START_BLT 0x00E15D72 |
| 0x22 | `video_flags` | SMD_$START_BLT 0x00E15D5E |
| 0x38 | `cursor_visible` | what SMD_$INQ_KBD_CURSOR *returns* (0x00E6E112) |
| 0x40 | `cursor_ec` | SMD_$SEND_RESPONSE 0x00E6F500 `pea (-0x20,A2)`; SMD_$BORROW_DISPLAY waits on it (0x00E6F63A) |
| 0x4C | `field_4c` | `bset.b #0x7,(0x4c,A3)` = bit **15** of the word |
| 0x4E..0x54 | `min_x, max_x, min_y, max_y` | SMD_$SET_CLIP_WINDOW 0x00E6FE7E-0x00E6FEA6 |
| 0x56..0x5C | `clip_x1, clip_x2, clip_y1, clip_y2` | smd_$write_str_clip_impl tests X against +0x56/+0x58 (0x00E70402) and Y against +0x5A/+0x5C (0x00E7040E) |

Consequence: **SMD_$SET_CLIP_WINDOW's argument is {x1, x2, y1, y2}**, not
{x1, y1, x2, y2} - 0x00E6FE76/0x00E6FE7A store it as two longwords into the
(min, max) pairs.

## 0x00E84924 is not a separate global

`SMD_GLOBALS` is at 0x00E82B8C and 0x00E82B8C + 0x1D98 = **0x00E84924**, so
every `(0x1d98,A5)` reference and that absolute address are one word:
`SMD_GLOBALS.default_unit`. `SMD_DEFAULT_DISPLAY_UNIT` is gone (bead
source-nuan supersedes the "they are different words" note above). Two other
phantoms went with it: `SMD_BORROW_EC` is `SMD_EC_2` (0x00E6F61E
`pea (0xe2e408).l`) and `SMD_BORROW_RESPONSE` is
`SMD_GLOBALS.response_pending[unit-1]` (SMD_GLOBALS + 0x1D99 + unit;
0x00E6F4FC writes it, 0x00E6F650 reads it).

**Rule:** whenever a "second global at address X" turns up in this subsystem,
first compute X - 0x00E82B8C and check it against smd_globals_t.

## Code-region constant cells (source-2c9v)

Named in smd_internal.h/smd_data.c and labelled in Ghidra:

| addr | value | name |
|---|---|---|
| 0x00E6E59A | word 0xFFFF | `SMD_MINUS_ONE_DATA` - SHOW_CURSOR "keep the current cursor number" (0x00E6E256) |
| 0x00E6E458 | byte 0xFF | `SMD_TRUE_DATA` |
| 0x00E6E45A | byte 0x00 | `SMD_FALSE_DATA` |
| 0x00E6D92A | word 1 | `SMD_ONE_LOCK_DATA` |
| 0x00E6D92C | word 0 | `SMD_ACQ_LOCK_DATA` |
| 0x00E6DFF8 | word 1 | `SMD_SYNC_LOCK_DATA` |

SHOW_CURSOR's arg 2 is dereferenced as a **word** (0x00E6E1EA) and arg 3 as a
**byte** (0x00E6E1EE) - the width matters when typing the cell.

## SMD_$START_BLT is now smd/sau2/start_blt.s

Body 0x00E15D1E..0x00E15D89 assembles **byte for byte identical**. The one
trap: GNU as normalises `and.w #imm,%d0` to ANDI.W (0x0240) where the original
uses AND.W-with-immediate (0xC07C); emit `.short 0xc07c, 0xffde` to match.
(lock_display.s hits the same thing with CMP.W 0xB07C vs CMPI.W 0x0C40 and
could be made exact the same way.) The trampoline at 0x00E272BC loads a
**dead** A0 = 0x00E26F20 (SMD_$DISP1_INT) - bead source-tzn8.

## TPAD per-unit config (source-j999)

`A1/A3 = config + 0x2C` in all three writers, so displacement -0x2C+k is
config offset k. +0x06/+0x08 are **x_scale/y_scale** (TPAD_$SET_UNIT_MODE
0x00E69838/0x00E69840 write its xs/ys arguments there) and +0x0A/+0x0C are
**x_range/y_range** (TPAD_$RE_RANGE_UNIT 0x00E69A74/0x00E69A7A seed them with
0x200 beside x_min/y_min). The factor is always `range / scale`, guarded on
`scale == 0`. TPAD_$INIT copies x_max_disp/y_max_disp (+0x14/+0x18) into the
*scale* pair.

Related: [[handwritten-asm-verification]], [[feedback_fidelity_gates]].

## SMD_TIME_$COM and the one-entry info table (source-9j2l, 2026-09-06)

`SMD_DISPLAY_INFO` (Ghidra `SMD_$DISPLAY_COM`, 0x00E27376) has **exactly one**
0x60-byte entry. Proof by neighbour: `SMD_TIME_$COM` is at 0x00E273D6, and the
labels run 0x00E27366 `SMD_$CURSOR_PTABLE`, 0x00E27376 `SMD_$DISPLAY_COM`,
0x00E273D6 `SMD_TIME_$COM`, 0x00E273DC `MNK_$KTT_PTRS`, 0x00E273FC
`MNK_$KTT_MAX`, 0x00E273FE `SMD_$KTT`. Use `SMD_DISPLAY_INFO_COUNT` (= 1), not
`SMD_MAX_DISPLAY_UNITS`, which still sizes the *unit record* block.

`SMD_TIME_$COM` is 6 bytes and used to be modelled as `SMD_BLINK_STATE` with a
field literally named `smd_time_com`. Correct layout:

| off | field | evidence |
|---|---|---|
| 0x00 | `blink_enable` (boolean) | 0x00E6FF72 `tst.b (A2)` / `bpl` gates the whole blink block |
| 0x02 | `cursor_painted` (boolean) | 0x00E6FF8E `tst.b (0x2,A2)` picks 250000us over 125000us |
| 0x04 | `blink_defer` (uint16) | 0x00E6FF76 `tst.w (0x4,A2)` skips one tick; 0x00E6FF9C clears it whatever the value |

`blink_enable` is `(cursor_number == 0)`: 0x00E6E424 `move.w D2w,(0xd4,A5)`
sets Z, 0x00E6E42C `seq D4b` turns that into the Domain boolean, 0x00E6E432
stores it. `cursor_painted` is the byte `SMD_$BLINK_CURSOR_1` flips with
`not.b (A2)` at 0x00E27276 - which is why the callback re-tests it *after*
calling through SMD_BLINK_FUNC_PTABLE.

Xrefs Ghidra reports "to 0x00E273D6" from 0x00E6DDDE, 0x00E6E10A, 0x00E6F4EC,
0x00E6FE70 and 0x00E6FF08 are **not** accesses to it: they are
`lea (0,A1,D0),A2` with A1 = 0x00E27376 and D0 = unit*0x60, i.e. the biased
one-past pointer the code then uses as `(-0x60,A2)`. Check the addressing mode
before believing a data xref in this table.

`smd/blink_cursor_1.c` had `SMD_UNIT1_DISPLAY_COM = 0x00E27316`, 0x60 low.
0x00E2724C `lea (0x128,PC),A0` -> 0x00E27376 and 0x00E27248
`lea (0x18e,PC),A1` -> 0x00E273D8 = `&SMD_TIME_$COM.cursor_painted`.

## SMD_$DISP1_INT (source-tzn8, 2026-09-06)

Ghidra has **no function** at 0x00E26F20 - `gsk analyze` and `gsk disassemble`
both say "Function not found", only the label exists. Recover such a routine by
`gsk read <addr> <len>` and piping the hex through
`m68k-elf-objdump -D -b binary -m m68k:68020 --adjust-vma=<addr>`. Note
`gsk read` caps at 256 bytes per call, so long routines need several reads.

Now `smd/sau2/disp1_int.s`, 0x00E26F20..0x00E27025 (0x106 bytes) plus the four
status cells at 0x00E27026..0x00E27035 (0x00130007, 0x00130008, 0x0013001C,
0x0013001D = SMD_Invalid_Direction_From_SM / _BLT_In_Use / _BLT_Done_Interrupt
/ _Interrupt_Routine_State). Those cells belong in the *code* region and both
this handler and `smd_$disp1_setup_blt` reach them with `pea (d16,PC)`, so they
must be emitted inline for the displacements to reproduce.
`smd/sau2/scroll_blt_setup.s` had defined two of them in `.data`, swapped and
split into words in the wrong order - deleted.

Structure: interrupt prologue (`ori #0x600,SR`, `jsr IO_$USE_INT_STACK`), then
`A1 = SMD_DISPLAY_INFO`, `A0 = 0xFF9800`, `tst.b (1,A0)` to distinguish
"BLT done" from anything else, and an 8-slot `bra.w` jump table at 0x00E26F4E
entered by `jmp (2,PC,D0.w)` with D0 = `hw->lock_state * 4`. Exits are
`jmp PROC1_$INT_EXIT` (0x00E208FE) and `jmp PROC1_$INT_ADVANCE` (0x00E208F6),
never `rts`.

`jsr 0x00E20728` is the four-byte gate `movea.l (0x4,SP),A0` that falls through
into `ADVANCE_INT` at 0x00E2072C; `ec/advance_int.c` models gate+body as one C
function, so call `ADVANCE_INT` from assembly.

`smd_$disp1_setup_blt` (labelled at 0x00E27036, bead source-k6h0) is the
interrupt-level twin of the routine at 0x00E27070 that
`smd/sau2/scroll_blt_setup.s` models. Its label is defined at the end of
disp1_int.s so the `jsr (0xa8,PC)` at 0x00E26F8C keeps its displacement.

## Name reconciliation, 2026-09-07 (source-dhie)

Four SMD helper names disagreed between Ghidra and the C tree.  Settled as:

- `0x00E6F514` = **`smd_$init_display_state`** (Ghidra renamed from
  `smd_$reset_display_state`).  It is the entire body of the exported
  `SMD_$INIT_STATE` (0x00E6F818, an SR10.4 map name) and is also called by
  `SMD_$BORROW_DISPLAY` 0x00E6F69C.  The old Ghidra name collided with a C
  alias for a *different* address.  Still has no `.c` (bead source-vkwk).
- `0x00E6E8D6` = **`smd_$enqueue_event`** (not `smd_$send_loc_event`): it
  enqueues *any* event, keyboard chars included, into the ring at
  A5+0x728/+0x72A with 16-byte entries at +0x72C.
- `0x00E6D7E2` = **`smd_$reset_display_globals`** (not
  `smd_$reset_tracking_state`): clears info-entry fields and blink state as
  well as the tracking globals.
- `0x00E6E9A0` = **`SMD_$LOC_EVENT`** - an SR10.4 map name (3C4A1262), so it
  wins outright.  Its signature
  `int8_t SMD_$LOC_EVENT(int8_t, int16_t, uint32_t, int16_t)` already matched
  the stack offsets (byte@8, word@0xA, long@0xC, word@0x10; result in D0.b,
  caller pops 12 including the reserved 2-byte slot) and all three call sites
  (TPAD_$DATA 0x00E69794/0x00E697AE, SMD_$SET_TP_CURSOR 0x00E6E994).

**Root cause worth remembering:** `smd/smd_internal.h` carried three *stale
duplicate declarations* - two different C names for one address
(`smd_$reset_display_state` + `smd_$reset_unit_display` both for 0x00E6D736,
`smd_$reset_tracking_state` + `smd_$reset_display_globals` both for
0x00E6D7E2, `smd_$send_loc_event` + `smd_$enqueue_event` both for
0x00E6E8D6).  When a name disagreement shows up, grep the header for the
*address* as well as the name - the duplicate is usually already there under
the right name.

## SMD_WIRED is split across two map segments (2026-09-07)

`I E15B90 SMD_WIRED size=1FC` is the module's *code*; `D E26F20 SMD_WIRED
size=5E0` is its wired data block, which also holds SMD_$DISP1_INT and a row
of small `lea (d16,PC),A0` + `jmp abs.l` trampolines (XOR/OR_CURSOR,
START/CONTINUE_SCROLL, START_BLT).  A0 is a register argument in every one of
them: it always ends up 0x00E26F20, the block base, and the body copies it to
A5 and addresses the block off it - (0x302,A5)=&PROC1_$SET_LOCK,
(0x306,A5)=&PROC1_$CLR_LOCK (unnamed cells at 0x00E27222/0x00E27226),
(0x3a6,A5)=SMD_$CURSOR_TABLE.  A body with *two* entry points that differ only
in a discriminator left in D3 (0x00E15B90 XOR / 0x00E15B9A OR, the first
branching over the second's duplicated prologue) is the giveaway that these
are hand-written, not compiler output.
