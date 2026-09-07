---
name: aote-clocks-and-obj-loc
description: The AOTE's four 48-bit clocks (0x28 DTM / 0x30 DTU / 0x38 DTV / 0x40 DTA), the location word at 0x08, and the file_$obj_loc_t embedded at aote+0x9C.
metadata:
  type: project
---

# aote_t 0x08, 0x28..0x47 and 0x9C..0xBB (beads source-xk18, source-thsz, source-xntu)

**aote+0x20 (`length`) is the only length in the AOTE.** Everything that used
to be called `len_*` is a clock.

## The DT* run (all "high 32, low 16, two pad bytes")

| offset | field | pin |
|---|---|---|
| 0x28 | `dtm_high`/`dtm_low` | attr 9 = `FILE_ATTR_DTM_AST` writes it (0x00E04D88); attr 0x17 = `FILE_ATTR_DTM_OLD` (0x00E04DDC) |
| 0x30 | `dtu_high`/`dtu_low` | attr 10 = `FILE_ATTR_DTU_AST` (0x00E04D98); attr 0x18 = `FILE_ATTR_DTU_FULL` (0x00E04DEC) |
| 0x38 | `dtv_high`/`dtv_low` | `AST_$GET_DTV` returns exactly this pair (0x00E054F4-0x00E054FC); `TIME_$ABS_CLOCK` refreshes it at 0x00E05132 |
| 0x40 | `dta_high`/`dta_low` | the common attribute tail stamps it on every case but BLOCKS (0x00E05104) |

**The attribute numbers already in `file/file.h` are the decisive evidence** --
they name DTM/DTU independently, and each lands on the offset above. Reach for
that table before arguing from usage patterns.

`ast_$setup_page_read` 0x00E02ABA reads `TIME_$CLOCK` into 0x40 then copies it
into 0x28, so writing a page stamps DTM and DTA from one clock.
`ast/set_attribute_internal.c`'s `ATTR_TYPE_*` constants were shifted the same
way (LEN->DTM, DTM->DTU, and the ROUNDED / FROM_CLOCK variants).

## aote+0x08 is `location`, never a UID

bit 31 set = remote (bits 0..19 node, 20..30 network); bit 31 clear = local
with the LV index in the low byte; zero = "unknown", which selects the hint
search. `AST_$GET_LOCATION` hands this word back verbatim (0x00E04766).

## aote 0x9C..0xBB IS a `file_$obj_loc_t`

`AST_$GET_LOCATION` copies eight longwords out of aote+0x9C (0x00E0476A) and
`AST_$GET_ATTRIBUTES` copies them back, so the offsets line up one for one:

    obj_loc +0x00 reserved_00/volume -> aote 0x9C  (aote_t.obj_uid.high; the
                                        `clr.b (0x9c,A3)` at 0x00E021A4 clears
                                        only its top byte)
            +0x04 block_hint         -> aote 0xA0  (aote_t.obj_uid.low -- this
                                        is what 0x00E022AE copies over the
                                        location word)
            +0x08 uid                -> aote 0xA4  aote_t.obj_loc_uid
            +0x10 loc_info           -> aote 0xAC  aote_t.obj_loc_net
            +0x14 node               -> aote 0xB0  aote_t.obj_loc_node
            +0x18 reserved_18        -> aote 0xB4  aote_t.obj_loc_res_18
            +0x1C rights_bits        -> aote 0xB8  aote_t.vol_index
            +0x1D flags              -> aote 0xB9  aote_t.remote_flag

`aote_t.obj_uid` is therefore a misnomer (it is reserved/volume + block_hint,
not a UID); the real UID copy is `obj_loc_uid` at 0xA4. Splitting `obj_uid`
is still open -- it would touch `ast/get_attributes.c` and
`ast/activate_aote_canned.c`.

**Trap this pass caught twice:** 0xA4 and 0xAC are easy to confuse.
`ast_$deactivate_segment` logs 0xA4/0xA8 (`lea (0xa4,A1),A1` at 0x00E0190A) but
`ast_$set_attribute_internal` copies 0xAC/0xB0 for the remote call
(`lea (0xac,A1),A0` at 0x00E052EA) -- the C had been reading 0xA4 there.

## Concurrent-tree gotcha

`make test` in this shared worktree fails on whichever subsystem another agent
has mid-edit, and a concurrent `make clean` makes an already-built test run
report mass bogus failures. Rebuild and run in ONE bash call before believing
a failure list.
