---
name: dir-flp-op-tables
description: Recovered layouts for DIR_$OP_TAB (one biased table) / DIR_$NAME_OFFSET_TABLE, FLP_DATA and MNK_$KTT_PTRS.
metadata:
  type: project
---

**DIR module block** `D E7DBF8 DIR size = 212C`.  DIR_$DO_OP does
`lea (0xe7dc00).l,A5` at 0x00E4C030, so A5 = block base + 8.
- There is no separate `DIR_$OP_PARAMS`: A5+0x1F9C = 0x00E7FB9C is record+0x02
  of the one biased DIR_$OP_TAB family (virtual base 0xE7FB9A).  See
  [[biased-tables-and-dead-cells]] for the record shape and the bias.
- `DIR_$NAME_OFFSET_TABLE` = A5+0x2000 = 0x00E7FC00: 8 words
  {0,4,16,20,12,0,0,0}, ends at DIR_$ENTRY_CACHE_TOO_LONG_NAME (0x00E7FC10).
- `DIR_$OP_TAB` 0x00E7FC42: 26 records of 8 bytes.  The DIR_$<op>U client
  wrappers read +0x00 (request header version -> request.reserved) and +0x04
  (fixed request size, added to the name length).  DIR_$SERVER indexes the same
  family with a -0xA8 bias, which puts the family base at 0x00E7FB9A and makes
  DIR_$OP_TAB record 21; the two bases differ by 2 and are still modelled as
  separate objects.
- The DIR segment's last 0x12 bytes: a 2-byte alignment pad, three longword
  directory-read continuation cookies (0xE7FD14 first real entry, 0xE7FD18
  "..", 0xE7FD1C ".") that dir_$do_op_dir_readu reads as (0x2114/0x2118/
  0x211c,A5), and the unreferenced string ".bak" at 0xE7FD20.

**FLP_ module block** `D E7AEF4 FLP_ size = 13C`, A5 = 0x00E7AEF4.  Extents are
pinned by NEC 8272 command strings in the image and by the by-reference word
counts the code passes:
  +0x02C FORMAT TRACK, 6 words (count cell 0x00E3DDC4 = 6) - image 4D 00 03 08 74 4E
  +0x04A READ/WRITE DATA, 9 words (0x00E3DFE0 = 9); its first 3 words are also SEEK (0x00E3DDC2 = 3)
  +0x060 FLP_$EC, +0x070 FLP_$SREGS, +0x078 per-unit cylinder, +0x080 I/O buffer
  +0x0E8 controller table (8 bytes/ctlr: info ptr at +0, hw addr at +4)
  +0x108 SPECIFY 03 DF 3C, +0x110 SENSE DRIVE STATUS, +0x114 RECALIBRATE (count 0x00E3E21C = 2), +0x118 SEEK
DAT_00e7a55c is DISK_$DATA + 0x390, not an flp object.

**Keyboard translation tables**: MNK_$KTT_PTRS 0x00E273DC is 8 pointers,
MNK_$KTT_MAX 0x00E273FC = 7.  Targets: SMD_$KTT 0x00E273FE, SMD_$GERMAN_KTT
0x00E8497E, SMD_$FRENCH_KTT 0x00E84A7E, SMD_$SWEDISH_KTT = SMD_$NORWEGIAN_KTT
0x00E84B7E (index 3 and 4), SMD_$UK_KTT 0x00E84C7E, SMD_$KTT again (index 6),
SMD_$SWISS_KTT 0x00E84D7E.  Each is exactly 0x100 bytes.

Related: [[module-block-alias-pattern]], [[rem-file-opcodes]].
