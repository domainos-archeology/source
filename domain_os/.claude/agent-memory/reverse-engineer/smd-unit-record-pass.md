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
- Careful: `SMD_GLOBALS.default_unit` (globals+0x1D98) and the separate
  `SMD_DEFAULT_DISPLAY_UNIT` global at 0x00E84924 are **different** words and
  several files conflate them (bead source-nuan).

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

Related: [[handwritten-asm-verification]], [[feedback_fidelity_gates]].
