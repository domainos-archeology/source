---
name: biased-tables-and-dead-cells
description: How to settle "is this one table or two?" and "is this cell dead?" - the DIR_$OP_TAB bias, the KBD 8/32 split, and the whole-image reference scan that proves a negative.
metadata:
  type: project
---

# Biased tables, split tables, and genuinely dead cells

## A base address two bytes off is a field offset, not a second table

`DIR_$OP_PARAMS` (0xE7FB9C) and `DIR_$OP_TAB` (0xE7FC42) looked like two
objects because their bases differ by 2.  They are one **biased** 8-byte-record
table whose virtual base is **0xE7FB9A**:

- `DIR_$SERVER` 0x00E5824A `movea.l #0xe7fc42,A1` / `lsl.l #3,D1` /
  0x00E58258 `move.w (-0xa8,A0),D1w` -> base - 0xA8 = base - 21*8.
- `DIR_$DO_OP` A5 = 0xE7DC00, so `(0x1f9c,A0)` = record + 0x02 and
  `(0x1fa0,A0)` = record + 0x06.
- the `DIR_$<op>U` client wrappers read record + 0x00 and record + 0x04 by
  absolute address.

Record: +0 request body version, +2 reply body version, +4 fixed request size,
+6 fixed reply body size.  Records 21..46 = opcodes 0x2A..0x5C, which is
exactly what DIR_$DO_OP's jump table admits (`subi.w #0x2a` / `cmpi.w #0x33` /
`bcc` at 0x00E4C266) and exactly the 26 records at 0xE7FC42..0xE7FD12.  The
bytes records 0..20 would occupy are *other* DIR globals, so the C array starts
at record 21 and a `DIR_$OP_REC(half)` macro applies the bias.

**How to apply:** when two "tables" share a stride and their bases differ by
less than the stride, they are one record family.  Find the widest indexing
range in the code (a jump table's `subi`/`cmpi` pair is the best bound) and the
lowest base any reader forms; the physical array is the intersection.

## An index's bound decides where a table ends

`DAT_00e2ddec` was modelled as 40 words filling 0xE2DDEC..0xE2DE3C.  Both
readers double a word index bounded by `MNK_$KTT_MAX` (7):
`KBD_$RCV` 0x00E1CE30 uses `state->kbd_type_idx`, `kbd_$fetch_key` 0x00E1CBDA
the mode word.  So the table is **8** words and the remaining 32 words at
0xE2DDFC are a different object.

## Proving a cell is dead (a real negative, not a gap)

Extract the image once and scan it whole rather than trusting `gsk xrefs`,
which only records the base of an indexed access:

    tar xf ~/src/domainos-archeology/sau2.10.2.tar ./sau2/domain_os
    # VA = file offset + 0xDFFC00  (verify by locating a known instruction)

Then, for the address range in question, search the raw bytes for
(a) every 4-byte big-endian absolute address in the range, and
(b) every `(d16,An)` displacement that would reach it, filtered by
`(prev_byte & 0x3f) == 0o55` for A5.  Also search for the `lea <base>.l,A5`
that establishes the module base, to enumerate *which* routines can reach it at
all.  0xE2DDFC..0xE2DE3A and 0xE7FD20 (".bak") both came back with zero hits
from either direction; that is reportable as "no instruction in the image
reaches it", not as a TODO.

Related: [[byte-pool-vs-typed-array]], [[module-block-alias-pattern]],
[[dir-flp-op-tables]], [[handwritten-asm-verification]].
