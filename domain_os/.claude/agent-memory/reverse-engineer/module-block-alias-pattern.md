---
name: module-block-alias-pattern
description: How to define A5-relative module data blocks (FLP_DATA, TERM_$DATA, DIR_$OP_TAB) so overlapping DAT_ labels stay one object.
metadata:
  type: project
---

When a subsystem's `DAT_00xxxxxx` labels all fall inside one map data segment
(`D <addr> <MODULE> size = N`), they are fields of one A5 block, not separate
objects.  Defining them as loose globals silently splits storage that the
original aliases - e.g. FLP_$SREGS (+0x70) and DAT_00e7af66 (+0x72) are the
same FDC status words.

**How to apply:** find the `lea (<addr>).l,A5` in any routine of the module,
subtract to get each label's offset, then either
1. give the block a struct in the subsystem's public header and turn every
   `extern` into a `#define NAME (BLOCK.field)` alias (endian-clean, preferred
   - this is what flp/flp.h and dir/dir_internal.h now do), or
2. when a block struct already exists, add `#define DAT_xxx BLOCK_AT(0xNNN)`
   with `#define BLOCK_AT(off) ((char *)&BLOCK + (off))` (term/term_internal.h).

Byte-offset macros into a byte array are endian-neutral; `*(uint16_t *)&b[off]`
is not, so prefer typed struct fields and mask-extract single bytes
(`(uint8_t)(field & 0xFF)`) for read-only byte aliases.

The Makefile has **no header dependency tracking** - after editing a header you
must `make clean` before trusting the link errors.

Related: [[sau2-map-name-corrections]], [[reference_sau2_domain_os_map]].
