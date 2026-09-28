---
name: mmu-asm-and-pmap-notes
description: MMU_ASM segment transcribed to mmu/sau2 (register ABIs, immediate-source encodings, host-model .c pattern) and the PMAP timer/segmap/ASTE-table facts recovered 2026-09-27
metadata:
  type: project
---

Recovered 2026-09-27 (batch mmap2: mmu/, pmap/, wp/):

- **MMU_ASM (0xE23D2C..0xE248E3)** is entirely hand-written: every routine
  is now `mmu/sau2/*.s`, byte-checked against the image with a script
  (assemble, objcopy .text, cmp against the slice of a `gsk read` dump).
  The `.c` files stay as `#if !defined(ARCH_M68K)` host models (the
  misc/crash_system.c precedent) so the unit tests keep running.
  Separate assembly forces +2 per PC-relative data read (`move.w
  (d,PC),Dn` -> absolute) and +2/+4 per `bsr` -> `jsr`; document each.
  gas picks ANDI/ADDI/SUBI for `and.w #imm,Dn` etc.; the image uses the
  immediate-SOURCE forms (c07c/c0bc/d0bc/94bc/c03c) - spell them `.short`.
- Register ABIs: `mmu_$installi` (0xE2409C) D2=ppn A4=va D4=packed, leaves
  A3 = PFT entry (INSTALL_PRIVATE relies on it); the remove run
  0xE23DCC/DD8/DF4 falls through (remove_internal -> remove_pmape ->
  unlink_from_hash with D2 ppn, D1 predecessor offset or 0, D3 value, A2
  PTT entry, A3 PFT entry).  unlink XORs `((pred & ~0x8000) ^ val) &
  0x8fff` into the ORIGINAL predecessor word and points the PTT at the
  predecessor.  REMOVE_VIRTUAL walks rings in groups of 32 pages
  (`dbeq D7` on D7 & 0x1f) dropping the IPL/CSR bracket between groups;
  REMOVE_ASID uses bare `ori #0x700,SR` / `andi #0xf8ff,SR` (forced IPL 0).
- Cells: MMU_$SET_CSR stores the LOW byte of its word into the HIGH byte
  of MMU_$PID_PRIV; SET_PROT edits bits 4..8 of the PFT entry's FIRST
  word (bits 20..24 of the longword); SET_SYSREV writes 0xE24271 = low
  byte of the MMU_$SYSTEM_REV longword at 0xE2426E (mmu.h used to say
  0xE2426F); MCR_SHADOW is the 2-byte cell at 0xE242D2 (f0 00) right after
  MCR_CHANGE's rts, reached `lea (0x30,PC)`.
- Host-side PFT access: never `PMAPE_FOR_VPN(vpn)[1]` (big-endian word
  index) - use `*PFT_FOR_PPN(vpn) & PFT_FLAG_MODIFIED` on the longword.
- **PMAP**: timer intervals are 48-bit clocks: purifier {7, 0x270E}
  (`clr.w +0x14 / move.l #0x7270e,+0x16`), update {0xE5, 0}, ws-scan
  expire = interval = {3, 0xD090} = 250000.  WS timer elements have a
  0x1C stride from 0xE24D68 (1-based, 65 fit) and pair with TIME_$VTQ[pid-1]
  (bead filed).  The 0x14-byte table at 0xEC53F0 is the 1-based ASTE table
  (ASTE_BASE[seg-1]: +4 aote, +8 fm_block (hint >> 4), +0xc segment, +0x12
  flags bit 5 set after BAT_$ALLOCATE).  Segment-map entries are
  longwords: bit 31 WRITING, bit 30 VALID, bit 29 INSTALLED, low word VPN
  (PMAP_SEGMAP_L_* in pmap.h).  PMAP_$FLUSH's batch array is 1-based
  (-0x44 + n*4, n=1..16); update_seg_map runs for NOT-modified pages or
  under NOWRITE, and once after a direct write; the outer loop repeats
  only when an in-use (bit 31) entry was seen, waiting on
  AST_$PMAP_IN_TRANS_EC only if nothing was written that pass.
- MMAP_$WS_SCAN's `mode` is a boolean byte in the high half of its word
  (`st -(SP)` = 0xFF00 -> partial scan; `clr.w` = 0 -> full); PMAP_$PURGE_WS
  reads its second word the same way (callers pass 0xFF00 / 0).
- The time-queue callback argument is `time_$callback_arg_t` = address of
  a longword holding the element address (`*cell` is already a pointer).
