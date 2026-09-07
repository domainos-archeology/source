---
name: mem-module-block
description: The MEM_ A5 block at 0xE22930 (0x5C) - board counts are 1-based at +0x0A/+0x0C, page id is bits 21..16, and MEM_$MEM_REC is the 0x56 bytes ASKNODE copies.
metadata:
  type: project
---

`D E22930 MEM_ size = 5C` is one record (mem_data_t, mem/mem.h), established
by `lea (0xe22930).l,A5` at 0x00E0ADB8.  Its whole code segment is the single
routine MEM_$PARITY_LOG (`I E0ADB0 MEM_ size = B8`).

| off  | addr     | field |
|------|----------|-------|
| 0x00 | 0xE22930 | MEM_$SIZE, longword |
| 0x04 | 0xE22934 | MEM_$MEM_REC begins; w_00 = 0x0002 in the image |
| 0x06 | 0xE22936 | w_02 = 0x0002 |
| 0x08 | 0xE22938 | MEM_$BOARD_ERRORS base - **bias slot, never touched** |
| 0x0A | 0xE2293A | board 1 count (phys < 0x300000) |
| 0x0C | 0xE2293C | board 2 count |
| 0x0E / 0x10 | | two unreferenced words |
| 0x12 | 0xE22942 | MEM_$PAGE_ERRORS[0..3], stride 0x12 |
| 0x5A | 0xE2298A | segment tail, outside MEM_$MEM_REC |

Three traps this cost time on:

1. **The board array is 1-based.** `addq.w #0x1,(0x8,A5,D1)` with `D1 = 2*board`
   and board in {1,2} lands on 0x0A/0x0C, *not* 0x08/0x0A.  Ghidra's
   MEM_$BOARD_ERRORS label sits on the displacement, which is the bias base.
   Same shape for the records: the index-computed form is `A5 + 0x12*i` with
   i in 1..4, i.e. `page_errors[i-1]`, while `lea (0x12,A5),A0` gives the real
   base.  See [[biased-tables-and-dead-cells]].
2. **The page identity is bits 21..16, not 8..13.**  `and.b (0x9,A6),D2b` is
   byte 1 of the longword parameter at 8(A6) - a big-endian trap the tree had
   written as `(phys_addr >> 8) & 0x3F` for both the argument and the stored
   address.  Compare [[attr-query-call-shape]].
3. **MEM_$MEM_REC's size comes from its consumer.**  ASKNODE_$INTERNET_INFO at
   0x00E64812 copies 21 longwords + 1 word = 0x56 bytes from 0xE22934, which
   ends exactly at the end of the page-error table.  That is what proves the
   record extent and that the last two segment bytes are outside it.  (The
   tree's asknode/internet_info.c copies 21 *words* - bead source-1b7z.)

**Packing:** only the 18-byte `mem_$page_error_t` needs
`__attribute__((packed))`.  Packing the enclosing records too makes
`&MEM_$MEM_REC` an address-of-packed-member error under `-Werror` in
asknode/.  All-word records lay out identically on m68k and host unpacked.

Related: [[module-block-alias-pattern]], [[reference_sau2_domain_os_map]].
