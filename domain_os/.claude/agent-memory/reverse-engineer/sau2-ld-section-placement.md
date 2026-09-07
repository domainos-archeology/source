---
name: sau2-ld-section-placement
description: How sau2.ld reproduces the image's code/data adjacency so `lea (sym:w,%pc)` R_68K_PC16 relocations do not overflow, and the ld gotchas around counting them.
metadata:
  type: project
---

Faithful `lea (SYM:w,%pc)` emission (byte fidelity) creates R_68K_PC16
relocations that only link if the target is within +/-32KB of the code. The
image's segments put those tables a few hundred bytes past their dispatchers;
`sau2.ld`'s default `*(.text .text.*)` / `*(.data .data.*)` split puts them
~0x11000 apart, so they overflow.

**Why:** the archivist rule forbids changing the instruction form back to
absolute `lea`, so the adjacency has to be reproduced at link time instead.

**How to apply:** at the definition site give the table a dedicated section
(svc does this with `SVC_TABLE_SECTION` in `svc/svc_internal.h` ->
`.text.svc_tables`, guarded `#if defined(ARCH_M68K)` because the host build has
no such section names); then in `sau2.ld`'s `.text` name the code objects and
that section explicitly, *before* the `*(.text .text.*)` gather. ld assigns
each input section to the **first** matching output-section statement, so order
in the script is what does the work. Object patterns match the path as spelled
on the link command line and `*` crosses `/`, so `*svc/sau2/trap0.o(.text)`
survives a BUILD_DIR change. Where the target is a Pascal module data block
pinned at an A5 address, use source-0i3's `.moddata.<name>` scheme instead.

Two counting traps:

- **GNU ld prints at most 10 relocation overflows tree-wide**, then
  "additional relocation overflows omitted from the output". A `grep -c` of a
  failing build is a floor, not a count. Fix one subsystem and re-run to see
  the rest.
- **A compile error anywhere makes the count read 0** because the link never
  runs. Check `grep -c LINK` in the log before believing a zero.

**Pulling one object out of the gather can strand its neighbours.** The
adjacency unit is the image *segment*, not the referencing object. Naming only
`smd/sau2/disp1_int.o` moved it away from `cursor_thunks.o` / `scroll.o` /
`start_blt.o`, whose own `lea (SMD_$DISP1_INT:w,%pc),%a0` then overflowed - one
fixed site traded for five new ones. List the whole run (or the whole
subsystem, as ec+proc1 needed for their mutual `bsr.w`), in image order.

**Not every PC16 target is data.** `bsr.w`/`bra.w` are 16-bit too, so
code->code references across subsystems overflow the same way and are fixed by
the same ordering, with no section attribute anywhere (ec/sau2 -> PROC1_ASM's
dispatcher, proc1/sau2 -> ec's ADVANCE_INT).

**Check the SAU2 map before assuming an A5 block.** A cell the code reaches
`(d16,PC)` is almost always *inside* a code segment, so it takes `.text.<name>`
and is out of scope for source-0i3's `.moddata.<name>`. Confirmed for
`PROC1_$CURRENT` 0xE20608 (inside PROC1_ASM 0xE1EAC8+0x24A4),
`SIO2681_$PTRS` 0xE2DF80 (opens SIO_INT, 0x8C), `SMD_$DISPLAY_COM` /
`SMD_DISPLAY_INFO` 0xE27376 (closes SMD_WIRED 0xE26F20+0x5E0).

**Verify placement with `-Map`, not just a clean link.** Relink the same
objects with `m68k-elf-ld -T sau2.ld -Map out.map -o /dev/null` and read the
resulting gaps: SIO2681_$PTRS -> SIO2681_$INT1_RTE came out at exactly the
image's 0x20.

See [[feedback_shared_worktree]]: other agents edit the same tree, so gate a
link-order change against `git archive HEAD` plus your own files copied over it,
built in a scratch directory - otherwise their in-flight compile errors mask
your result.
