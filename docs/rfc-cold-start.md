# The SAU2 domain_os RFC image and COLD_START

Bead source-10hr (step 1 of epic source-nqcs).  Written 2026-10-06 by
Claude Opus 5.5; reviewed against the bytes (objdump of the file, an
independent decode of the tables, the sysboot file) 2026-10-06 by Claude
Fable 5.1.  Corrections from that review are marked **[review]**.

Sources: the SR10.2 SAU2 image
`sr10.2-install/install/ri.apollo.os.v.10.2/sau2/domain_os` (612,174 bytes),
its link map `sau2-maps/domain_os.10.2.map`, the SR10.2 `sysboot`
(`sr10.2-install/sysboot`), the SR10.3 image and map for comparison, and
MAME's `apollo_dn300` driver for the DN3xx MMU register layout.  Every
address below is an instruction or data address in the image as the boot
loader places it (file byte 0 at 0x101400).  `domain_os/tools/rfc_decode.py`
decodes all of it from the image and writes `domain_os/tools/rfc_segments.json`.

## 1. Short version

* COLD does **not** copy the kernel to its run-time addresses.  The loader
  puts the file in physical memory almost as-is, and COLD builds the MMU
  page tables so that the kernel's VAs map onto the physical pages the file
  already occupies.  The kernel runs where it was loaded.
* The file is loaded in **two physical pieces**.  The SAU2 `sysboot`
  relocates itself to 0x17D000 and keeps buffers below that, so it cannot
  load anything into 0x157C00..0x180000.  The info block tells it so: file
  pages whose physical page number would be >= 0x55F (0x157C00) go to
  physical page `MOVE_TO_PPN` (0x600, i.e. 0x180000) and up.  COLD's tables
  follow the same rule.
* COLD walks two tables: its own **MMU map table** (17 entries of count,
  PPN, VA, protection, 0x101850), and, only on a 68020 (DN330), the
  **RELOC fixup table** the binder appended after the last loaded byte
  (10,960 addresses of 32-bit VA cells, which COLD moves into the DN330's
  26-bit address windows).
* bss is not in the file.  It is whatever RAM follows the image (the RELOC
  table's own pages included).  COLD clears only the MMAP part of OS_PMAPS
  (0xEB4800..0xEC2800).
* COLD ends with `jsr OS_$INIT` (0xE337F4) on the stack P1_STACK_BASE
  (0xEB2000), passing the 9-longword boot record at 0x1018FC and the
  12-longword diskless record at 0x101908.

## 2. File and memory layout

| file offset | low (load) address | what | run-time VA |
|---|---|---|---|
| 0x000 | 0x101400 | RFC header (12 bytes) | identity (COLD page) |
| 0x00C | 0x10140C | info block (24 bytes) | identity |
| 0x024 | 0x101424 | COLD_START code, tables, cells (to 0x101BFC = initial SP) | identity, unmapped by OS_$INIT 0xE3384A |
| 0x800 | 0x101C00 | DUMP page (map `D23 E00400 DUMP loaded at 101C00`) | 0xE00400 after COLD copies it to phys 0x100C00 |
| 0xC00 | 0x102000 | kernel, VA 0xE00800..0xE88834 contiguous | VA = low + 0xCFE800 |
| 0x88C34 | 0x18A034 | RELOC table (map `I20 E88834 RELOC loaded at 18A034, size = 0`) | overlaid by bss (ACL_$DATA starts at VA 0xE88834) |
| 0x9574E | 0x196B4E | end of file | |

**What the map's `loaded at` means.**  It is the binder's position in the
output stream, which for this image is the file offset + 0x101400:

* Segments inside the file, code (`I`) and data (`D`) alike, have
  `loaded at` = VA - 0xCFE800.  All 62 such map segments agree (checked by
  rfc_decode.py, `map_agrees`).  Code segments do carry a `loaded at` in
  this map (for example `I 4 E00800 WIRED_PROC loaded at 102000`).
* Segments below the kernel (TRAP_PAGE 0, PTT 0x700000,
  UNWIRED_STACKS..OS_LOW_END_ 0xD00000.., CRASH_RECORD 0xE00000) have
  `loaded at` = VA.  They are not in the file.
* bss segments after RELOC have `loaded at` = VA - 0xCFE800 + 0xCB1A, the
  stream position after the RELOC bytes (ACL_$DATA `loaded at 196B4E` =
  EOF).  This number has no physical meaning.  At run time a bss VA lives
  at physical VA - 0xE56400 + 0x180000 (see the split below).
  **[review]** IODEFS_GUARD (0xF4FC00, size 0) follows this bss rule too
  (`loaded at 25DF1A` = 0xF4FC00 - 0xCFE800 + 0xCB1A), not the VA rule;
  rfc_decode.py was fixed to classify it as bss.

**Physical placement (the split).**  A file byte at low address `L` is at
physical `L` if `L < 0x157C00`, otherwise at `L - 0x157C00 + 0x180000`.
For a kernel VA the physical address is `VA - 0xCFE800` below VA 0xE56400
and `VA - 0xE56400 + 0x180000` from there on.  The split is 0x157 pages
(0x55C00 bytes) into the kernel, inside `.TEXT`.  It does not fall on a
segment boundary, so it is a physical boundary only.

**Pattern cross-check.**  All 124 runs of `tools/asm_image_ref.txt`
(reference bytes taken from the relocated kernel program in Ghidra) occur
in the raw file at low = VA - 0xCFE800, 119 of them uniquely.  Examples:

| VA | length | low address | phys at run time |
|---|---|---|---|
| 0xE15B90 | 0xDE | 0x117390 | same |
| 0xE1E700 | 0xC2 | 0x11FF00 | same |
| 0xE1E864 | 0x12C | 0x120064 | same |
| 0xE1E9F4 | 0xD4 | 0x1201F4 | same |
| 0xE208F6 | 0xBC | 0x1220F6 | same |
| 0xE21688 | 0x12E | 0x122E88 | same |
| 0xE218E8 | 0x1E6 | 0x1230E8 | same |
| 0xE21DC2 | 0xC4 | 0x1235C2 | same |
| 0xE2277C | 0xEA | 0x123F7C | same |
| 0xE23E38 | 0xDE | 0x125638 | same |
| 0xE26F20 | 0x2EE | 0x128720 | same |
| 0xE2B130 | 0x160 | 0x12C930 | same |
| 0xE2E85C | 0x424 | 0x13005C | same |
| 0xE819E2 | 0xD6 | 0x1831E2 | 0x1AB5E2 |

The kernel bytes in the file are the linked (un-relocated) bytes, the same
bytes the kernel program in Ghidra holds.

## 3. RFC header (0x101400, 12 bytes)

| offset | value | meaning |
|---|---|---|
| +0 | 0x00101400 | load address: file byte 0 lands here (confirmed: the pc-relative `move.l (0x101420,pc)` at 0x1014B6 reads 0x600, the map's MOVE_TO_PPN) |
| +4 | 0x00101424 | start address = COLD's first instruction |
| +8 | 0x0002 (word) | machine type = SAU number |
| +10 | 0x0000 (word) | 0 in every sau2 RFC file (calendar, invol, ... all read `0002 0000`) |

Correction to the epic text: the machine type is a word at +8.  The
longword at +8 is 0x00020000, not 0x00000002.

**[review] Evidence that it is a word:** sysboot reads it as one:
0x13F448 `move.w 8(a0),d3` (a0 = the header buffer at 0x174C00), then
0x13F44C `cmp.w 0x100,d3` against the PROM's machine-type word (the high
word of PROM_$MACHINE_ID); 0x13F454 `tst.w d3` / `sgt`: a zero word means
"any machine", a nonzero mismatch prints the word (0x13F4F8 `move.w
8(a0),d7; ext.l d7`) in an error message.  The same word is what 0x13F576
compares with 2 and 3 (section 4).  Every SR10.2/10.3 `sau*/domain_os`
header agrees: sau3 `0003 0000`, sau4 `0004 0000`, sau8 `0008 0000`, and
the SR10.3 sau12 image has `000c 0000` (12), which only makes sense as a
word.  `memtest` has `0000 0000` (any machine).  sysboot itself is not an
RFC file: its third header longword is 0x0013FE04, the address of its
exec helper.

## 4. Info block (0x10140C, 24 bytes)

COLD itself reads only the last longword.  sysboot reads the magic and the
split pair.  The binder's RELOC table lists 0x101410 and 0x101414 as VA
cells, so those two are VAs.

| addr | value (10.2) | 10.3 | meaning | evidence |
|---|---|---|---|---|
| 0x10140C | 0x00000C00 | 0xC00 | offset from the load address to the first kernel page (0x102000, WIRED_PROC); also the size of header+COLD+DUMP | layout; not relocated |
| 0x101410 | 0x00E78400 | 0xE79400 | VA of `.DATA` (OS_DATA), i.e. end of kernel code | map `D21 E78400 .DATA`; 10.3 `.DATA` at E79400; a RELOC cell |
| 0x101414 | 0x00E00800 | 0xE00800 | VA of the first kernel page (pairs with +0) | map WIRED_PROC; a RELOC cell; COLD's own immediates use the same value |
| 0x101418 | 0xFEED2B03 | same | magic: sysboot 0x13F546 checks word 0xFEED, byte 0x2B, and that bits 7..5 of the last byte (the format version) are 0.  Version 1 blocks get loader callbacks stored at +0x28..+0x40 (sysboot 0x13F612..0x13F66A) | sysboot |
| 0x10141C | 0x0000055F | same | split PPN: file pages whose physical page would be >= this are loaded elsewhere (0x55F << 10 = 0x157C00) | sysboot 0x13F56A, 0x13E23E; COLD immediate 0x157C00 |
| 0x101420 | 0x00000600 | same | **MOVE_TO_PPN** (map symbol): the physical page where those pages go (0x180000) | sysboot 0x13F570/0x13E248; COLD 0x1014B6, 0x1015C8, 0x1015D0, 0x101752 |

sysboot's loader (0x13E238..0x13E24C, addresses as the file is laid out;
the file `sr10.2-install/sysboot`, 10,240 bytes, RFC-like header load
0x13D800 start 0x13D82A, runs relocated to 0x17D000): `if (flag && page >=
split) page = page - split + move_to_ppn` (flag = a5+518, set at 0x13F566
when the magic checks pass; split = a5+444, move = a5+440).  0x13E256..
0x13E27C: a page that would land at physical 0x17CC00..0x180000 is a load
error (sysboot's own area); pages below 0x17CC00 are allowed, so the
0x55F split is the image's choice, not sysboot's limit.

**[review]** The call at 0x13F582 is made when the header's machine-type
word (section 3) is 2 or 3 (0x13F576 `cmpi.w #2,d3` / 0x13F57C `#3`), not
for "boot types".  The callee 0x17D424 (file 0x13DC24) hooks the bus-error
vector and probes memory (`tst.b (a0)`) from the given PPN upward, stepping
512 KB on a bus error, and returns the first readable PPN (0 past
0x3FFFC00; sysboot then reports error 29).  It stores the result only in
its own cell a5+440 (0x13F592) and never writes it back to the loaded
image's 0x101420, which the loader (0x13F5C0, load PPN 0x405 from
0x13F52C) fills from the file.  So COLD's four reads of 0x101420 agree
with sysboot only because the probe returns 0x600 whenever RAM exists at
0x180000; a RAM hole there would make the two disagree.  Open question 1
is answered.

## 5. COLD_START, step by step

Entry 0x101424.  **[review]** sysboot's exec helper 0x13FE96 (run
0x17F696, called from 0x13F6AC on the `btst #4,0x103` path) copies the six
longwords at its caller's sp+8..sp+31 to a fresh stack ending at 0x17D000
and `jsr`s the start address, so COLD sees a return address and six
longwords and consumes three.  On that path the caller (0x13F692..
0x13F6AA) pushes zeros except one word, so d1 = 0 (device 0, ctlr 0),
d2 = 0x0000xxxx with the low word = the flags word at fp+24 (sysboot sets
its bit 3 at 0x13F684; bit 15 is the debugger trap COLD tests), and
a1 = 0.  The diskless copy in step 4 therefore never fires on this path;
the other path (helper 0x13FE04 / run 0x17F604, 0x13F6C0) builds a
different frame that this review did not resolve.

1. **0x101424** `A5 = 0x101400` (COLD).  Every cell COLD writes is
   A5-relative: 1254 = 0x1018E6, 1264 = 0x1018F0, 1276 = 0x1018FC,
   1288 = 0x101908, 1316 = 0x101924, 1320 = 0x101928, 1332 = 0x101934,
   1334 = 0x101936, 1338 = 0x10193A.
2. **0x10142A** `VBR = 0` (the PROM's vectors).
3. **0x101434** `movem.l (sp)+,d0-d2` / `movea.l (sp)+,a1`.  d0 is the
   return address, discarded.  d1 and d2 are the boot record's first two
   longwords.  a1 points to a 7-longword block.  **0x10143A** stores d1/d2
   at 0x1018FC (`cold_boot_params`).  **0x101440** `SP = 0x101BFC`.
4. **0x101444..0x101458**: if the word at 0x1018FC (d1's high word, the
   *boot device type* of OS_$INIT's boot_params_t.device, not a machine
   type) is 2 or 6, copy the 7 longwords at a1 to 0x101908
   (`cold_diskless_info`).  The epic text calls this a machine-type check;
   it is not one.
5. **0x10145E** `btst #1,0x103`.  0x103 is the low byte of
   PROM_$MACHINE_ID (0x100).  Bit 1 is set on a 68020 machine (DN330).  The
   branch also leads to `st M68020` at 0x101688, and `movec cacr` would trap
   on a 68010.  Bit clear (DN300/DN320, 68010): go to 0x101544, which
   **clears the word at 0xFFB400** (map: MMU page) and skips steps 6..8.
   **[review]** On the 68010 path the RELOC table is not read at all (the
   `cmpa.l #0x157C00` / `trap #15` check is inside the 68020 path), and
   every absolute cell in the file is left holding its linked value.
   Checked: the DCTES table at VA 0xE2C8BC (file 0x12E0BC) holds
   0xE2FAE0, 0xFF9C00, 0xE75748, 0xE3E002, 0xFFA800, 0xE19F6C, 0xE2C93C,
   0xE2C8FC, 0xE2C8BC; the `jsr OS_$INIT` operand holds 0xE337F4; and
   all 124 asm_image_ref runs match the relocated kernel program.  None of
   the 10,960 cells holds a pre-shifted 0x3xxxxxx value (only the four
   flagged map-table VA cells have bits 31..26 set).
6. **0x101468..0x101474** (68020 path): `moveq #31,d1; lsr.b d1,d0;
   move ccr,d0; cmp.w #4,d0`.  Anything other than CCR = Z alone jumps to
   PROM 0x414 (error/abort).  **[review]** d0 is the discarded return
   address; a byte shifted right by 31 is 0 with C = X = 0 on every
   680x0, so the test can only fail where `move ccr,d0` does not exist
   (68000, which takes an illegal-instruction trap instead).  It is an
   inherited CPU sanity check with no effect on a 68010/68020.
   **0x101478** `CACR = 9` (enable and clear the instruction cache).
7. **0x101482..0x10148E**: switch the MMU geometry cells to the DN330's
   26-bit VA: `0x101934 = 6` (VA shift; default 4), `0x101936 = 0x003F`
   (makes the long VA mask 0x003FFFFF; default 0x000FFFFF),
   `0x10193A = 0x1000` (PTT entries; default 0x400).
8. **RELOC fixups, 0x101494..0x101542** (68020 only):
   * `a0 = 0xE88834 - 0xE00800 + 0x102000 = 0x18A034`.  0xE88834 is the map
     symbol RELOC (the first byte after the last loaded segment), so this is
     RELOC's low address.  **0x1014A6** asserts `a0 > 0x157C00`, else
     `trap #15`.  **0x1014B0** converts to physical:
     `a0 - 0x157C00 + (MOVE_TO_PPN << 10)` = 0x1B2434.  d2 holds
     MOVE_TO_PPN << 10 for the loop.
   * Table format: `count.w` (10,960 = 0x2AD0; dbf, so at most 65,536),
     then `count` longwords.  Each one is the **low (file) address of a
     32-bit cell that holds a VA**.  At 0x1014F8 an address >= 0x157C00 is
     moved by -0x157C00 + d2.  The entries are not sorted.  47 of them point
     below the kernel: 41 into COLD and 6 into DUMP.  So COLD relocates its
     own immediates, the info block's VA fields (0x101410, 0x101414) and the
     VA fields of its map table.
   * Per cell (0x101508): `v = cell & 0x03FFFFFF`, then
     * `0xE00000 <= v < 0xFA0000`: cell += 0x2F00000 (a3), so kernel
       0xE00000 becomes 0x3D00000.  10,639 cells.
     * `v >= 0xFA0000`: cell += 0x3000000 (a4), so I/O 0xFA0000.. becomes
       0x3FA0000.  205 cells.
     * `0xCC0000 <= v < 0xE00000`: cell += 0x2E00000 (d6), so 0xCC0000
       (PROT) becomes 0x3AC0000.  56 cells.
     * `0x700000 <= v < 0x800000`: cell -= 0x300000, so the PTT window
       becomes 0x400000.  9 cells.
     * otherwise unchanged.  51 cells, values 0, 8 and 0x100: PROM/low
       absolute references the binder listed as relocatable.
     The add is applied to the unmasked cell, so flag bits 31..26 survive.
   * Nothing un-applies this.  On a DN330 the whole kernel, including COLD's
     map table and the `jsr OS_$INIT` operand at 0x101708, runs in the
     0x3xxxxxx windows.
9. **0x10154E** copy 256 longwords from 0x101C00 (DUMP) to physical
   0x100C00 (PPN 0x403).
10. **0x101566..0x101584** build the TRAP_PAGE at physical 0x101000
    (PPN 0x404): vectors 0 and 1 are left as they are (`move.l (a1),(a1)+`
    twice), and vectors 2..255 are copied from 0x8.. (the PROM's).
    `VBR = 0x101000`.
11. **0x101588** push the old bus-error vector and save SP in 0x101924
    (`cold_saved_sp`).
12. **0x101590..0x1015CC** kernel page count
    `n = (roundup1K(OS_PAGE_END 0xEC4800) - 0xE00800) >> 10` = 0x310 (the
    compare at 0x1015A6 is on the byte count against 0x55C00, before the
    two `lsr.l #5`).  If `n <= 0x157`, store `n` at 0x1018E6 (the count
    of map entry "kernel low"; the file already holds 0x157 there, so this
    store only matters for a kernel that fits the low piece).  Otherwise
    store `(n - 0x157) << 16 | MOVE_TO_PPN` (the word at 0x101422) at
    0x1018F0 (count and PPN of map entry "kernel high"; the file holds
    0 0 there and that entry's VA 0xE56400 is static).  For 10.2 that is
    0x1B9 pages at PPN 0x600.
13. **0x1015D0..0x101604**
    `a4 = (MOVE_TO_PPN << 10) - 0xE00800 - 0x55C00 + 0xEB4800` = 0x1DE400,
    the physical address of MMAP (VA 0xEB4800, OS_PMAPS).  It is saved in
    0x101928 (`cold_mmap_phys`).  The bus-error vector is pointed at
    0x101642, and 0xE000 bytes (3584 entries of 16 bytes) of MMAP are
    cleared.
14. **0x101608..0x101660 memory sizing**: for each 1 KB physical page from
    0x80000 to 0x400000, read and rewrite every longword (non-destructive),
    then store 0x40000000 in that page's MMAP entry.  The page at 0x101000
    is skipped (its entry is left alone).  A bus error goes to 0x101642
    (`cold_probe_buserr`), which restores SP from 0x101924 and calls
    0x101654 (`cold_probe_skip_256k`): skip 256 KB, which is 4096 bytes of
    MMAP.  The MMAP index is (PPN - 0x200) * 16.
15. **0x101662** restore vector 2.  `VBR = 0`.
16. **0x101670** `cold_map_pages(0x101850)` (0x101718), which loads the MMU
    (section 6) and turns it on.  From 0x101678 on, kernel VAs are live.
17. **0x101678** `MMU_$PID_PRIV (0xE23D2C) = 1`.
18. **0x101680..0x1016AC** (68020 only): `st M68020` (0xE23D2E), save SP,
    line-F vector (0x2C) = 0x1016B2, clear byte 0xFFB402,
    `fmove.l #0,fpcr`.  If there is no FPU the line-F trap lands at
    0x1016B2.  Otherwise `st M68881_EXISTS` (0xE8180C).
19. **0x1016B2..0x1016F6** SP = saved SP.  Clear the MMU word 0xFFB402 and
    copy it to MMU_$INIT_BSR (0xE24294).  Then, on a 68010:
    `0xFFB406 = 0`, `0xFFB404 = 1`.  On a 68020: `0xFFB402 = 0`,
    `0xFFB406 = 0x40`, `0xFFB40A = 0x4000` (the MMU parity register; see
    apollo-hardware-docs).
20. **0x1016F8** `SP = 0xEB2000` (P1_STACK_BASE).
    `pea 0x101908; pea 0x1018FC`, so the call is
    `OS_$INIT(&boot_params /*9 longs 0x1018FC..0x10191F*/,
    &diskless /*12 longs 0x101908..0x101937*/)`.  The two overlap: the
    second record's last five longs are COLD's saved SP, MMAP physical
    address and geometry cells.  **0x10170C** if bit 15 of d2 (boot flags)
    is set, `trap #15` (break into the debugger before the kernel).
    **0x101716** `jsr (0xE337F4)` = OS_$INIT.  It never returns.  OS_$INIT
    unmaps PPN 0x405/0x406 (COLD's two pages) at 0xE3384A.

## 6. cold_map_pages (0x101718) and the MMU map table

DN3xx MMU (MAME apollo_dn300): PFT at physical 0x4000..0x7FFF (4096
4-byte entries, one per physical page), PTT at 0x700000 (one word per
1 KB of the window), control register 0x8000 (pid/priv/power).

* 0x101718 `(0x8000).w = 2`.  0x101720..0x101736 clear the PTT (count from
  0x10193A).  0x10173A..0x10174A: PFT[0] = 0xFE008000, PFT[1..4095] = 0.
* 0x10174E `a1` = physical address of MMU_$PTTX (VA 0xEC2800), via the same
  MOVE_TO_PPN formula.
* Table entries (0x101772): `count.w` (0 ends the table), `ppn.w`,
  `va.l` (bits 31..29 are flags, the rest is the VA), `prot.w`.  For each
  page the loop does the following:
  * Build PFT entry = `prot << 16 | (va >> shift) & 0xF0000`.  Bit 12 is
    set if VA flag bit 29 is clear.  **[review]** The DN3xx PFT entry, as
    the local MAME driver `mame/src/mame/apollo/apollo_dn300_mmu.cpp`
    decodes it: bits 0..11 link (next PPN in the hash chain), 12 global
    (matches any ASID), 13 used, 14 modified, 15 end of chain, 16..19
    xsvpn (the VA bits above the PTT window, the hash tag), 20 X, 21 R,
    22 W, 23 domain, 24 elaccess, 25..31 elsid (ASID).  So prot 0x170 =
    X+R+W, 0x160 = R+W (I/O pages), 0x130 = X+R (the low-memory identity
    pages), all with elaccess and ASID 0, and bit 12 makes every mapping
    global except COLD's own two pages (the only entry with flag bit 29).
  * Hash into the PTT word at `0x700000 + (va & mask)`.  If it is empty, set
    bit 15 (end of chain) and link to self, then PTT = ppn.  Otherwise
    insert after the head using an eor-swap of the 12-bit links.
  * Store `PTTX[ppn] = (va & mask) >> 6`.  For ppn >= 0x200, store
    `MMAP[ppn - 0x200].w0 = (va & 0xC0000000) >> 16`.  **[review]** w0 is
    `mmape_t.wire_count` (byte 0) and `seg_offset` (byte 1) of
    mmap/mmap.h: kernel pages (flag bit 31) get wire_count 0x80, COLD's
    pages (flag bit 30) 0x40, the same 0x40 the memory sizing gives every
    present RAM page; MMAP_$INIT reads these.
  * Advance: ppn += 1, va += 0x400.
* 0x10180C `(0x8000).w = 1` (MMU on).  0x101814 `(0).b = 0xFF` (byte 0
  of the new TRAP_PAGE).

Map table at 0x101850, 10.2 (identical in 10.3 apart from the two cells
COLD fills in):

| at | pages | PPN (phys) | VA | flags | prot | what |
|---|---|---|---|---|---|---|
| 0x101850 | 15 | 0x001 (0x400) | 0x000400 | - | 0x130 | low memory identity |
| 0x10185A | 8 | 0x050 (0x14000) | 0x004000 | - | 0x130 | |
| 0x101864 | 16 | 0x010 (0x4000) | 0xFFB800 | - | 0x160 | PFT |
| 0x10186E | 1 | 0x020 (0x8000) | 0xFFB400 | - | 0x160 | MMU |
| 0x101878 | 1 | 0x021 (0x8400) | 0xFFB000 | - | 0x160 | SIO |
| 0x101882 | 1 | 0x022 (0x8800) | 0xFFAC00 | - | 0x160 | TIMR |
| 0x10188C | 1 | 0x027 (0x9C00) | 0xFFA800 | - | 0x160 | DISK/FLOP/CALENDAR |
| 0x101896 | 1 | 0x026 (0x9800) | 0xFF9C00 | - | 0x160 | RING2 |
| 0x1018A0 | 1 | 0x025 (0x9400) | 0xFF9800 | - | 0x160 | DISP1 |
| 0x1018AA | 1 | 0x024 (0x9000) | 0xFFA000 | - | 0x160 | DMA |
| 0x1018B4 | 128 | 0x080 (0x20000) | 0xFC0000 | - | 0x160 | DISP1_MEM |
| 0x1018BE | 1 | 0x400 (0x100000) | 0xE00000 | 4 | 0x170 | CRASH_RECORD |
| 0x1018C8 | 1 | 0x403 (0x100C00) | 0xE00400 | 4 | 0x170 | DUMP |
| 0x1018D2 | 1 | 0x404 (0x101000) | 0x000000 | 4 | 0x170 | TRAP_PAGE |
| 0x1018DC | 2 | 0x405 (0x101400) | 0x101400 | 3 | 0x170 | COLD itself |
| 0x1018E6 | 0x157 (static in the file; rewritten only if the kernel fits the low piece) | 0x408 (0x102000) | 0xE00800 | 4 | 0x170 | kernel, low piece |
| 0x1018F0 | 0x1B9 (filled; 0 in the file) | 0x600 (filled; 0 in the file) | 0xE56400 | 4 | 0x170 | kernel, high piece through OS_PAGE_END |
| 0x1018FA | 0 | | | | | end |

The I/O-page names come from the map's IODEFS symbols.  A second table at
0x10181C is not referenced by COLD, by RELOC or by any absolute longword
in the image.  **[review]** Decoded, it has five entries in the same
format: 1 page PPN 0x404 at VA 0 (the trap page), 0x4F pages PPN 1 at
VA 0x400, 8 pages PPN 0x50 at VA 0x4000, 0x3AC pages PPN 0x58 at VA
0x16000, 0xBFB pages PPN 0x405 at VA 0x101400 (all prot 0x170 except the
0x130 PROM-area entry): an identity map of 0..0x400000 with the same
PPN-0x50 quirk as the live table.  It looks like a leftover
"run the kernel unrelocated" map.  Its purpose is still unknown.

## 7. The 10.3 image

607,550 bytes.  Same header, the same COLD code byte for byte apart from
link-time immediates, and the same static map table.  The info block
differs only in +4 (.DATA VA 0xE79400).  The changed immediates are RELOC
0xE87834, OS_PAGE_END 0xEAC000, MMAP 0xE9C000, MMU_$PTTX 0xEAA000,
P1_STACK_BASE 0xE99800, OS_$INIT 0xE3042C, MMU_$PID_PRIV 0xE23C70,
M68020 0xE23C72, M68881_EXISTS 0xE81420 and MMU_$INIT_BSR 0xE241C4.
RELOC has 10,752 cells, followed by 0x2108 bytes of zero padding.  Kernel
pages are 0x157 + 0x157 at PPN 0x600.  The split constants (0x55F, 0x600,
0x157C00, 0x55C00) are the same.  **[review]** Confirmed by `cmp` of the
two files' first 0xC00 bytes: 25 differing bytes in 13 runs, all inside
the immediates listed above plus the DUMP VA cell at 0x101CBE; the map
table and the info block (bar +4) are byte-identical.  The zero tail is
not page rounding (10.2 ends at low 0x196B4E, 10.3 at 0x19593E, neither
1 KB aligned) and matches no obvious rounding of the count; its purpose
is unknown and it is harmless (it lands in ACL_$DATA's RAM, which
ACL_$INIT zeroes anyway).

## 8. What step 3 (the layout generator) must produce

**Implemented 2026-10-06 (source-yheb, source-m4xs; check: source-j46b).**
The rules chosen are in section 8a below; items 1..8 are the requirements
as the review stated them.

1. **Header**: `00101400 00101424 0002 0000`.
2. **Info block**: `00000C00`, `<VA of first data block>`, `<VA of first
   kernel page>`, `FEED2B03`, `0000055F`, `00000600`.  Fields +4 and +8 are
   RELOC cells.  sysboot only needs the magic and the last two fields.
3. **COLD** at 0x101424..0x101BFC with the table at 0x101850 and the cells
   at 0x1018E6..0x10193A.  Its absolute operands must be re-linked to our
   symbols: RELOC, the first kernel VA and its low address, OS_PAGE_END,
   MMAP, MMU_$PTTX, P1_STACK_BASE, OS_$INIT, MMU_$PID_PRIV, M68020,
   M68881_EXISTS and MMU_$INIT_BSR (`cold_constants` in the JSON).  0x101BFC
   is the initial SP, so COLD+data must stay at or below 0x7FC bytes.
4. **DUMP** page at file 0x800 (VA 0xE00400, exactly 0x400 bytes).
5. **Kernel** from file 0xC00 (low 0x102000) as one contiguous stream with
   **VA - low = constant**.  The first kernel VA must be page aligned (COLD
   maps it whole pages from PPN 0x408).  Holes between sections must be
   real bytes in the file: COLD maps, it never moves anything.
   `MMAP`, `MMU_$PTTX` and `P1_STACK_BASE` may be anywhere at or above the
   low piece, but they are reached through the high piece's PPN formula.
   That formula is only right for VAs at or above `first VA + 0x55C00`, so
   they must lie at or above it, and below OS_PAGE_END.
6. **RELOC** immediately after the last loaded byte: `count.w`, then the
   low addresses of every absolute 32-bit VA cell (all of ours, not just
   kernel windows), at most 65,536 entries.  The table must start above
   0x157C00 (COLD traps otherwise).  It is needed only on a 68020 (DN330).
   **[review]** On DN300/DN320 (68010, the MAME `dn300`/`dn320` drivers)
   COLD never touches the table: the count read, the 0x157C00 check and
   the loop are all inside the 68020 branch.  A 68010 build can therefore
   boot with no fixup table at all, and the kernel's linked 0xE00000-based
   cells are exactly what the MMU map expects (section 5 step 5).  For
   the DN330 the table must have `count >= 1`: `count = 0` with `dbf`
   runs 65,536 times over garbage.
   **How to generate it [review]:** the entries are the file (low)
   addresses of every 32-bit cell that holds an absolute address: in the
   original, 10,639 kernel VAs, 205 I/O addresses (>= 0xFA0000), 56
   0xCC0000-window addresses, 9 PTT (0x700000) addresses and 51 low
   absolutes (0, 8, 0x2C.., 0x100, 0x101400, 0x800000), including COLD's
   41 own operand/table cells and 6 cells in DUMP.  That is exactly the
   set of `R_68K_32` relocations the linker resolves, so link with
   `--emit-relocs` and take the `.rela.*` entries of type R_68K_32 (not
   R_68K_PC32 / PC16 / 16) for every output section that lands in the
   file (COLD, DUMP and the kernel alike); entry = r_offset's VMA -
   0xCFE800 (our VA-to-low delta).  Hand-written tables in the COLD
   transcription must use `.long sym` (not `.long sym-base` or pc-relative
   forms) so they appear in that list.  Cells whose value has bits 31..26
   set (the map table's flagged VAs) are allowed: COLD masks the value for
   the window test and adds the delta to the unmasked cell.
7. **bss**: not in the file.  Our zero blocks become VAs after RELOC.  The
   file ends with the RELOC table (the original pads it with zeros).  bss
   memory is not zeroed by COLD except MMAP.  The original's bss therefore
   starts with the RELOC table's bytes and arbitrary RAM.  **[review]**
   Nothing in COLD or OS_$INIT clears it as a whole: the map table gives
   the bss pages the same prot 0x170 as the rest of the kernel (they are
   just more pages of the "kernel high" entry up to OS_PAGE_END), and the
   only clearing loops in COLD are MMAP (0x1015F8) and the PTT/PFT.  Each
   Pascal module zeroes its own data in its init (acl/init.c:
   `OS_$DATA_ZERO(&ACL_$DATA, ACL_$DATA_SIZE)`, xpd/init.c likewise;
   MMAP_$INIT fills MMAP), so bss is zeroed piecemeal by the modules that
   care.  Our C relies on zeroed bss everywhere, so either COLD's
   transcription adds a clear of first-bss..OS_PAGE_END (a deliberate,
   documented deviation) or step 3 emits the zero blocks as file bytes.
   Emitting them as bytes costs file size and still sits under the split
   rule.
8. **Physical hole**: 0x157C00..0x180000 belongs to sysboot.  It runs at
   0x17D000 and reads the first file block to 0x174C00.  The low piece holds
   at most 0x55C00 bytes of kernel.  Everything after it, RELOC included,
   lands at 0x180000 and up, and COLD maps pages up to OS_PAGE_END there.

## 8a. The layout as built (step 3)

Written by Claude Opus 5.5.  `domain_os/sau2.ld` links an ELF
(`build/sau2/domain_os.elf`, `--emit-relocs`);
`tools/gen_rfc_fixups.py` writes `dist/sau2/domain_os` from it
(`objcopy -O binary` of the loaded sections, then the fixup table);
`make check-rfc` (`tools/check_rfc.py`) checks the file.  The build of
2026-10-06:

| file | low | VMA | output section | what |
|---|---|---|---|---|
| 0x000 | 0x101400 | 0x101400 | `.cold` | header, info block, COLD (`.text.COLD`, 0x800 bytes) |
| - | - | 0xE00000 | `.crash_record` (NOBITS) | CRASH_$RECORD, LOG_$LAST_ENTRY: the page COLD maps to PPN 0x400 |
| 0x800 | 0x101C00 | 0xE00400 | `.dump` | the DUMP page (`.text.DUMP`, 0x400 bytes) |
| 0xC00 | 0x102000 | 0xE00800 | `.text` | OS_PROC: code and the module data blocks with file bytes, map order (layout.ld) |
| | | | `.rodata`, `.data` | C rodata; OS_DATA = the start of `.data`; then the C bss objects that are file zeros (layout_data.ld) |
| 0xB5DA0 | 0x1B71A0 | 0xEB59A0 | (appended) | RELOC: the fixup table, 19,093 entries; the file ends with it (821,238 bytes) |
| - | - | 0xEB59A0 | `.bss` (NOBITS) | RELOC..RFC_FIXUP_TABLE_END 0xEC83F6: room for the fixup table; then the image's own bss, map order (layout_bss.ld), to OS_PAGE_END 0xF0BC00 |

* **VMA/LMA.**  LMA is the low address throughout.  `.cold`: VMA = LMA =
  0x101400 (COLD runs there physically with the MMU off, so its A5- and
  PC-relative cells and `movea.l #COLD` are 0x101400-based; the kernel
  symbols it names get their kernel VMAs).  Every other loaded section:
  LMA = VMA - 0xCFE800, one constant, so OS_PROC 0xE00800 is file 0xC00 =
  PPN 0x408 and holes between sections are zero bytes in the file.
* **bss rule** (`tools/gen_layout_ld.py`, IMAGE_BSS_START/END): NOBITS is
  exactly what the image had no file bytes for.  A zero-filled block
  (MODULE_DATA_DEFINE, now a `.bss.moddata.<name>` NOBITS input) keyed in
  [RELOC 0xE88834, IODEFS_GUARD 0xF4FC00), and a C bss object defining a
  map symbol in that range (only `MST` today), go to `.bss` in map order; a
  key whose image address is page aligned starts a page (OS_$STACK, MMAP,
  MMU_$PTTX, AST_$AOT, PMAP_$SEGMAP, MST).  Every other zero-filled block
  stays among the code at its map position as zeros in the file (as the
  image had them: PROC1_$DATA, PEB_$INFO, ...), and every other C bss
  object is zeros at the end of `.data` (243 of them, ~110 KB: C code
  that relies on zeroed bss keeps that guarantee for everything the image
  itself had zeroed in its file).  Since every NOBITS key is past every
  loaded key, the map order of the whole link is unchanged and `make
  check-layout`'s order check covers the NOBITS part too (0 inversions).
  The NOBITS part is not cleared by COLD (bar MMAP): each module's init
  must zero its own block, as the Pascal ones do.
* **The fixup table's RAM.**  The image let its 43 KB table overlay
  ACL_$DATA (0xAD98 bytes at RELOC), which ACL_$INIT zeroes.  Ours (76 KB)
  is larger than ACL_$DATA and would reach RINGLOG_$DATA, XPD_$DATA and
  PROC2_$DATA, so `.bss` starts with room of its own for it:
  RFC_FIXUP_TABLE_END = RELOC + 2 + 4 * N, N counted by the generator
  from the inputs' R_68K_32 relocations; gen_rfc_fixups.py fails if the
  table does not fit and check-rfc checks that no bss object lies under
  it.  A deviation in addresses only; the order is the map's.
  **[review 2026-10-06]** Confirmed against the map: RELOC 0xE88834 is
  ACL_$DATA's first byte (`D69 E88834 ACL_$DATA size = AD98`), and the
  image's table (2 + 4 * 10,960 = 43,842 bytes) ends at 0xE9343A, inside
  ACL_$DATA (0xE935CC), so the binder simply let the table share the RAM
  of the first bss block and ACL_$INIT's OS_$DATA_ZERO erases it after
  COLD is done with it.  Ours (76,374 bytes) would end at 0xEC83F6 in the
  original's arrangement, over RINGLOG_$DATA, XPD_$DATA and PROC2_$DATA.
  The alternative, placing RELOC at ACL_$DATA as the image does and
  letting ACL_$INIT zero it, would need ACL_$DATA padded to at least the
  table's size (changing a record's size the map fixes at 0xAD98: not an
  archivist's move) or a proof that every block under the table is zeroed
  or fully written by its init before any read, which the table's growth
  with every translated function would silently invalidate (the image's
  trick only works because its table happened to be smaller than its
  first bss block).  Decision: keep the reservation.  It is sized from the
  inputs' R_68K_32 counts, the generator fails if the table outgrows it,
  nothing in the kernel names that range (no symbol lies in it, and no
  literal in the tree points into 0xEB59A0..0xEC83F6; the one in
  mmu/sau2/*.s, 0xEC2800, is MMU_$PTTX's ordering key), and the image's
  own arrangement is still reproduced in every other respect: RELOC is
  the first byte after the last loaded byte and the first bss block
  follows the table.
* **Symbols** (sau2.ld; map names): OS_BEGIN 0xE00000, OS_PROC (first
  kernel page), OS_DATA, RELOC (= the end of `.data` = the start of
  `.bss`), OS_PAGE_END (the end of `.bss` rounded up to a page).
  cold_start.s names them in the 11 cells that were literals, plus
  `KERNEL_HI_VA = OS_PROC + 0x55C00` (`MAP_WIRED + KERNEL_HI_VA` is the
  0x80E56400 cell); the 47 R_68K_32 cells of COLD and DUMP are exactly
  the original's 47 (`check-rfc`).  The split constants (0x55F, 0x600,
  0x157C00, 0x55C00, 0x102000) stay literals, as in the image.
* **Info block** (this build): 0xC00, OS_DATA 0xE6E800, OS_PROC 0xE00800,
  0xFEED2B03, 0x55F, 0x600.  **[review 2026-10-06]** Until the source-wh9b
  review OS_DATA was the start of the C `.data` section (0xE98454 in the
  build above, 0xE9CCE0 after wh9b), which is not what the map calls
  OS_DATA: its `D39 E78400 OS_DATA` is `.DATA`, the start of the unwired
  data, the position layout.ld now names OS_DATA_UNWIRED.  sau2.ld defines
  `OS_DATA = OS_DATA_UNWIRED` (section 4: sysboot and COLD never read the
  cell, so this is faithfulness only; it stays a fixup cell).
* **COLD's page count** (section 5 step 12): (0xF0BC00 - 0xE00800) >> 10 =
  0x42D pages > 0x157, so the low entry keeps its static 0x157 at PPN
  0x408 and COLD stores 0x2D6 << 16 | 0x600 in the high entry (VA
  OS_PROC + 0x55C00 = 0xE56400).  MMAP 0xEDB800, MMU_$PTTX 0xEE9800 and
  P1_STACK_BASE 0xEDAC00 are in the high piece; MMAP's physical address
  is 0x600 << 10 - 0xE00800 - 0x55C00 + 0xEDB800 = 0x205400.  The kernel
  needs RAM through physical 0x235800 (10.2: 0x1EE400).  **[review]**
  DN3xx RAM starts at physical 0x100000 (handbook 7-1; the MAME dn300
  driver offers 512K, 1M and 1536K there, 0x100000..0x27FFFF at the
  default 1536K), so the original fits a 1 MB machine (0x1EE400 <
  0x200000) and ours needs the 1.5 MB one.  COLD's memory sizing (step
  14) only records which pages exist, in MMAP, and runs after sysboot has
  already written the file's pages: a machine with RAM ending below
  0x235800 bus-errors in sysboot, not in COLD.  Nothing in COLD checks
  that the kernel fits.
* **Fixup table**: the R_68K_32 relocations of `.cold`, `.dump`, `.text`,
  `.rodata` and `.data`, as low addresses, sorted; a placeholder entry
  (0x101400, the header's own load address) if there were none, so the
  count is never 0.  It cannot see literal addresses (C integer casts,
  `.equ`): our table has 15 I/O and 1 PROT cells against the image's 205
  and 56, because our C reaches the I/O pages through hw.h constants;
  no R_68K_16/8 absolute relocations exist.  The 68010 path does not read
  the table.
* **Asserted at link time** (sau2.ld): COLD 0x800 bytes, DUMP 0x400 at
  0xE00400, OS_PROC 0xE00800 at file 0xC00, RELOC's low address past
  0x157C00, MMAP / MMU_$PTTX / P1_STACK_BASE in [OS_PROC + 0x55C00,
  OS_PAGE_END).
* **Not done here**: the image maps only up to its OS_PAGE_END 0xEC4800,
  leaving AUDIT_LIST, AST_AOT and VM_TABLES to the kernel, where our
  OS_PAGE_END covered all of `.bss` (the rule chosen for this step).
  Since source-o7s2 the run-time windows of VM_TABLES (AREA_$RPMAP_CACHE,
  MST, PIT_PAGES, MSTE_PAGES) lie past OS_PAGE_END, unmapped by COLD:
  section 8c.  os/init.c's literal boundaries are gone: section 8b.

## 8b. What OS_$INIT does with the layout (source-wh9b)

Written 2026-10-06 by Claude Opus 5.5.  OS_$INIT (0xE337F4) builds the
kernel's address space from section boundaries that in the image are
`move.l #<link-time value>` immediates (every one a RELOC cell), rounded
at run time to a 32 KB segment (`andi.l #-0x8000`) or a 1 KB page
(`andi.w #-0x400`); where a boundary is rounded up the binder folded
`+0x7FFF` / `+0x3FF` into the immediate.  Since this step they come from
the link with the rounding unchanged; `os/os_internal.h` has the full table
(immediate, instruction addresses, map evidence, replacement), and the
compiler folds the `+0x7FFF` / `+0x3FF` into the R_68K_32 addend exactly as
the binder did.

**What each boundary is** (map symbols; the old os/init.c names in
brackets):

| image value | map | used for |
|---|---|---|
| 0xD00000, 0xDAC7FF | OS_LOW, OS_LOW_END + 0x7FFF [OS_WIRED_LOW/HIGH] | canned map 1, the fixed low region (stacks, disk buffers, netpool, area pages) |
| 0xE00000 | OS_BEGIN [OS_DATA_LOW] | canned map 2 start |
| 0xE38000 | .TEXT = OS_PROC_UNWIRED [OS_DATA_HIGH] | end of map 2 (wired code, wired data, boot-only part), start of map 3 (unwired code, read-only on a type-4 boot), the paging file's origin and the paging loop's start |
| 0xE78400 | .DATA = OS_DATA = OS_DATA_UNWIRED, rounded down [OS_TEXT_LOW] | end of map 3, start of map 4 (unwired data) |
| 0xEB1683 | OS_DATA_END + 0x7FFF [OS_TEXT_HIGH] | end of map 4 and of the paging loop, start of map 5 (= OS_PAGE 0xEB0000) |
| 0xEF6400 | MSTE_PAGES [OS_MST_BASE] | end of map 5: MSTE_PAGES + MST_PAGES_LIMIT pages, rounded up |
| 0xF4FC00, 0xFC0000 | IODEFS_GUARD = VM_TABLES_END, IODEFS [OS_WIRED_END/LIMIT] | the tables-below-I/O check |
| 0xE00BFF, 0xE1DC00 | OS_PROC + 0x3FF, OS_DATA_WIRED | the type-4 walk's read-only range (the wired code) |
| 0xE3824C, 0xE3EB45 | OS_DISK_PROC, OS_DISK_PROC_END + 0x3FF [OS_KEEP_WIRED1] | disk code a disked node keeps wired |
| 0xE784D0, 0xE7B443 | OS_DISK_DATA, OS_DISK_DATA_END + 0x3FF [OS_KEEP_WIRED2] | disk data kept wired |
| 0xEA9684 | OS_DATA_END [OS_INIT_FREE_ABOVE] | pages at or past it are freed, not given to the paging file |
| 0xE2F000, 0xE3D37F | OS_INIT_START, OS_INIT_END + 0x7FFF | os_$start_proc2 frees the boot-only part, up to .TEXT |

(OS_INIT_FREE_ABOVE was not the init code's end: it is OS_DATA_END, the
end of the image's own bss blocks ACL_$DATA .. PROC2_$DATA.  The fifth
canned map starts at OS_PAGE, not at the MST: 0xEF6400 is MSTE_PAGES, the
MST itself is at 0xEE5800.)

**How the link provides them.**  `tools/gen_layout_ld.py` emits each map
symbol (`NAME = .;`) in layout.ld / layout_bss.ld just before the first
placed item whose ordering key is at or past its map address: OS_DATA_WIRED,
OS_INIT_START, OS_INIT_END, OS_PROC_UNWIRED, OS_DISK_PROC_END,
OS_DATA_UNWIRED, OS_DISK_DATA, OS_DISK_DATA_END, then OS_DATA_END and
OS_PAGE in `.bss`.  OS_DISK_PROC is the function the map names there;
OS_PROC and OS_BEGIN were already defined; OS_LOW and OS_LOW_END are
absolute symbols in sau2.ld with the map's values (fixed VAs, but symbols
so the cells are fixups as in the image).  Map order puts the same objects
on each side of every boundary as in the image.  Two of the run-time
roundings need alignment the image's binder also gave: .TEXT starts a
32 KB segment (`. = ALIGN(0x8000)` before OS_PROC_UNWIRED; otherwise the
rounding down would hand the tail of the wired kernel to map 3 and the
paging loop) and OS_PAGE starts one (otherwise OS_DATA_END rounded up
would overrun the stacks and MMAP, which the loop would then free).
WIRED_DATA, .DATA and OS_INIT_PROC start pages, as in the map.  In this
build: OS_INIT_END 0xE2BB5E -> .TEXT 0xE30000 (17.6 KB of zeros in the
file, as the image's 0xE35380..0xE38000), OS_DATA_END 0xEDD2F2 -> OS_PAGE
0xEE0000 (11.3 KB of bss the loop frees, as the image's 0xEA9800..
0xEB0000).  File 839,950 bytes; RAM needed through physical 0x23CC00.
sau2.ld ASSERTs the alignments and the order of all the symbols, and that
OS_$STACK, MMAP and PTTX lie at or past OS_PAGE.

**Where our layout differs, and what OS_$INIT does there.**

1. *What the map does not place* (`OS_LINK_UNPLACED` .. RELOC, 0xE78D2A..
   0xEBA22C, ~261 KB): the catch-all code (353 sections: memcpy, WIN_ and
   disk helpers, statics of objects with no placed section, os_$start_proc2
   and os_$free_va_page themselves), the C `.rodata`, and the C `.data`
   with the C bss the image had as file zeros (vtoc_$data, OS_WIRED_$UID,
   OS_$SHUTDOWN_EC, ...).  Wired and unwired modules are mixed there; in
   the image every wired module's code and data lay below .TEXT and never
   reached the paging file.  Decision: OS_$INIT keeps this range wired,
   with a third test in the paging loop shaped like the two disk ranges'
   (0x00E3450A..0x00E3452A), the image's own mechanism for what must stay
   resident, but applied on every node: the image lets its two disk ranges
   go on a diskless node (0x00E34506 `tst.b NETWORK_$REALLY_DISKLESS'),
   whereas the unplaced part holds wired modules' code (memcpy, the driver
   helpers) that a diskless node runs under its own page faults.
   **[review 2026-10-06]** The first version had the third test inside the
   disked branch, which would have paged the wired modules on a diskless
   node; fixed.  This is a deviation (the image has two ranges, disked
   only), marked in os/init.c; source-s5lm would place those objects by
   module and drop it.
2. *The fixup table's room* (RELOC .. RFC_FIXUP_TABLE_END): the image's
   table lay in ACL_$DATA's first pages, which the loop gives to the paging
   file and unwires like any page below OS_DATA_END (and ACL_$INIT zeroes).
   Ours is below OS_DATA_END too, so it gets the same treatment with no
   special case: given to the file, unwired, not freed.
3. *The image's bss blocks* (ACL_$DATA .. PROC2_$DATA, after the room):
   pageable, exactly as in the image.
4. *OS_PAGE up* (OS_$STACK, MMAP, PTTX, AST_$AOT, PMAP_$SEGMAP, then
   past OS_PAGE_END the VM-table windows AREA_$RPMAP_CACHE, MST,
   PIT_PAGES, MSTE_PAGES): past the paging loop, mapped by canned map 5
   and wired, as the image's OS_PAGE .. VM_TABLES.  Map 5 ends at
   MSTE_PAGES + MST_PAGES_LIMIT pages, rounded up to a segment.  Until
   source-o7s2 MSTE_PAGES was mst/mst.h's literal 0xEF6400, inside our
   AST_$AOT (and AREA's 0xEE4C00 / 0xEE6400 windows inside MMAP); now
   MST_PAGE_TABLE_BASE is the link symbol MSTE_PAGES and OS_IODEFS_GUARD
   the link symbol IODEFS_GUARD = VM_TABLES_END (section 8c).
5. *The high-piece split* is physical only; OS_$INIT works on VAs.
6. *The crash record and DUMP pages* lie in [OS_BEGIN, OS_PROC), map 2, as
   in the image.

**Zeroing of the NOBITS blocks (source-olmv).**  Neither COLD (bar MMAP)
nor OS_$INIT clears bss in the image, and OS_$INIT does not here either:
each owner zeroes or writes its block before use, as the Pascal does.
ACL_$DATA and XPD_$DATA: OS_$DATA_ZERO in ACL_$INIT / XPD_$INIT.
RINGLOG_$DATA: its index is reset before the first entry
(RINGLOG_$CTL.first_entry_flag = -1 is file data) and entries are written
before they are read.  PROC2_$DATA: PROC2_$INIT builds the pid map, the
process-group counts and the free list.  OS_$STACK: OS_$INIT frees the
guard pages and zeroes the interrupt stack.  MMAP_$MMAPE: COLD.
MMU_$PTTX: written per page by COLD and MMU_$INSTALL before MMU_$PTOV
reads an entry.  AST_$AOT and PMAP_$SEGMAP: AST_$ADD_AOTES / ADD_ASTES
zero each entry and its segment-map block as they carve them.  MST:
MST_$INIT installs fresh pages for it and clears every MST word it uses
(since source-o7s2 MST is in the run-time windows past OS_PAGE_END, which
COLD does not map: section 8c).

**Checks.**  sau2.ld's ASSERTs run at every link (make and make check);
os/test/test_init.c applies the boundary macros to the map's values and
gets the image's immediates back, and walks a model of the paging loop's
page classes (the loop is inside OS_$INIT, which the test never runs) for
the image's layout and a sample of ours, disked and diskless.

## 8c. VM tables (source-o7s2)

Written 2026-10-07 by Claude Opus 5.5.

**What the image does.**  The map's last kernel data segment is
`D98 ED5000 VM_TABLES size = 7AC00`, after `D00 EC5400 AST_AOT` and
`D89 EC4800 AUDIT_LIST`, all past the image's OS_PAGE_END 0xEC4800, so
COLD maps none of it; it is virtual only (no file bytes, the map's bss
`loaded at`).  Its symbols:

| map VA | map symbol | size | pages come from |
|---|---|---|---|
| 0xED5000 | AST_PMAPS (= PMAP_$SEGMAP) | 0xFC00 | AST_$ADD_ASTES, on demand: MMU_$VTOP first, WP_$CALLOC + MMU_$INSTALL only if unmapped (0x00E011C4, 0x00E0126C, 0x00E012CA) |
| 0xEE4C00 | AST_PMAPS_END = AREA_$RPMAP_CACHE | 0xC00 (3 pages) | AREA_$INIT on a diskless node: MMU_$INSTALL, unconditional (0x00E2F48A) |
| 0xEE5800 | MST | 0xC00 | MST_$INIT: MMAP_$ALLOC_FREE + MMU_$INSTALL + zero, unconditional (0x00E30AE2) |
| 0xEE6400 | PIT_PAGES | 0x10000 (one page per seg-table pool record, 64) | area_$alloc_seg_table: WP_$CALLOC + MMU_$INSTALL, unconditional (0x00E09D90) |
| 0xEF6400 | MSTE_PAGES | up to 0x166 pages (MST_$INIT clamps MST_PAGES_LIMIT to 0x166, 0x00E30CF8) | MST_$INIT (Global A/B pages) and mst_$init_table_page (0x00E42D1A), unconditional |
| 0xF4FC00 | VM_TABLES_END = IODEFS_GUARD | - | OS_$INIT's ceiling, `move.l #IODEFS_GUARD,D0` / `cmp.l #IODEFS,D0` / `bls` (0x00E3397C) |

AST_AOT (ASTE and AOTE arrays) is filled like AST_PMAPS, on demand behind
an MMU_$VTOP probe (AST_$ADD_AOTES 0x00E0104C, 0x00E010E4).  Every
immediate naming these VAs is a fixup cell (checked in the image's RELOC
table: 0x00E2F450, 0x00E30BAA, 0x00E30BFC, 0x00E30C18, 0x00E09D70,
0x00E09E70, 0x00E3397E, 0x00E339FC, 0x00E42DC2, 0x00E43FAC, 0x00E4417C),
i.e. link-time values, not hardware: the 68020 path moves them with the
kernel.  **[review 2026-10-07]** Exhaustively: every even-aligned longword
of the kernel file (0xE00800 .. RELOC) whose value lies in
[0xEC4800, 0xF4FC00] is either one of 126 RELOC cells (naming AUDIT_LIST,
AST_AOT 0xEC5400, 0xEC7B60, AST_PMAPS 0xED5000, and the five window VAs:
0xEE4C00 x3, 0xEE5800 x23, 0xEE6400 x2, 0xEF6400 x18, 0xF4FC00 x1) or an
instruction-word pair that happens to fall in the range (159, none a page
address); COLD and DUMP hold only the 0xEC4800 OS_PAGE_END cell (itself a
RELOC cell).  No PC-relative or absolute-short form can reach these VAs
from the kernel's code, so moving the symbols is faithful on both CPU
paths.  check-rfc item 8 was also fed three bad layouts (a window off a
page, MSTE_PAGES below OS_PAGE_END, VM_TABLES_END past 0xFA0000) and
failed each.

**The problem.**  Since RFC step 3 our `.bss` held AST_$AOT,
PMAP_$SEGMAP and MST, all COLD-mapped, while AREA's 0xEE4C00 / 0xEE6400
and mst/mst.h's 0xEF6400 were still the image's literals, which our link
gave to MMAP_$MMAPE and AST_$AOT.  An unconditional MMU_$INSTALL over a
COLD-mapped VA would have orphaned the page under it (and on the DN3xx put
two PFT entries with the same VA in one hash chain); MST_$INIT did exactly
that to our MST.

**The rule.**  The VM-table windows the kernel installs pages into
unconditionally are layout, not hardware: they are placed in map order
immediately after our OS_PAGE_END (page aligned), at the map's offsets
from AREA_$RPMAP_CACHE, keeping every size; VM_TABLES_END = IODEFS_GUARD
is the end of the moved region.  Concretely (tools/gen_layout_ld.py,
VM_WINDOWS_START / VM_WINDOW_MARKS, writes build/sau2/layout_vm.ld;
sau2.ld INCLUDEs it in the NOBITS output section `.vm_tables` at
OS_PAGE_END):

```
.vm_tables OS_PAGE_END (NOLOAD) : {
    AREA_$RPMAP_CACHE = .;            /* EE4C00 */
    . = AREA_$RPMAP_CACHE + 0xC00;    KEEP(*(".bss.MST"))   /* EE5800 */
    . = AREA_$RPMAP_CACHE + 0x1800;   PIT_PAGES = .;        /* EE6400 */
    . = AREA_$RPMAP_CACHE + 0x11800;  MSTE_PAGES = .;       /* EF6400 */
    . = AREA_$RPMAP_CACHE + 0x6B000;  VM_TABLES_END = .;    /* F4FC00 */
}
IODEFS_GUARD = VM_TABLES_END;
```

**AST_PMAPS and AST_AOT stay in `.bss`.**  The smallest faithful change:
their only writers map a page only where MMU_$VTOP finds none, so a page
COLD already mapped is simply used (the same code path as a second ASTE on
an already-installed page in the image).  The cost is RAM: COLD maps their
0xF960 + 0xFC00 bytes (125 pages) up front where the image let them grow.
Because PMAP_$SEGMAP (page aligned, 0xFC00 bytes) is the last `.bss` block,
OS_PAGE_END = PMAP_$SEGMAP + 0xFC00 = AREA_$RPMAP_CACHE: the map's
AST_PMAPS_END = AREA_$RPMAP_CACHE adjacency survives, and VM_TABLES is one
contiguous VA range again, split only in what COLD maps.  (Moving AST_AOT
and AST_PMAPS out too, as the image has them, would save that RAM; it
needs a lower VM_WINDOWS_START and those blocks in layout_vm.ld, and is
not needed to boot: source-xmi3.)

**The I/O page stays.**  DISP1_MEM = IODEFS 0xFC0000 and the pages above
it are hardware (handbook 7-1, iodefs.s); the windows must end below it,
and below 0xFA0000 too, where COLD's 68020 fixup pass starts treating a
cell value as an I/O address (cold_start.s IO_WINDOW): a cell naming
VM_TABLES_END there would be moved by +0x3000000 instead of +0x2F00000.

**This build.**  OS_PAGE_END = AREA_$RPMAP_CACHE 0xF12400, MST 0xF13000,
PIT_PAGES 0xF13C00, MSTE_PAGES 0xF23C00, VM_TABLES_END = IODEFS_GUARD
0xF7D400 (0x42C00 below IODEFS, 0x22C00 below 0xFA0000: the COLD-mapped
kernel can grow until OS_PAGE_END reaches 0xF35000).  Moving MST out of
`.bss` took 3 pages off COLD's map: 0x447 kernel pages, RAM needed through
physical 0x23C000.

**Consumers** (all `ARCH_PTR_TO_VA` of the symbol, so their cells are
R_68K_32 fixups as in the image): mst/mst.h `MST_PAGE_TABLE_BASE` =
MSTE_PAGES (mst_$init, alloc_asid, alloc_segs, alloc_table_page (the
folded literal 0xEF6000 is now MSTE_PAGES + index * 0x400 - 0x400, as at
0x00E43FAA..0x00E43FBA), fork, get_private_size, invalidate,
map_canned_at, priv_set_touch_ahead_cnt, set_mstes, touch, unmap_privi,
unwire_asid_pages, va_to_pte, and os/init.c's map 5); area/area.h
`AREA_RPMAP_CACHE_VA` = AREA_$RPMAP_CACHE (area/init.c, rpmap_get.c) and
`AREA_PIT_PAGES_VA` = PIT_PAGES (alloc_seg_table.c, free_seg_table.c);
os/os_internal.h `OS_IODEFS_GUARD` = IODEFS_GUARD (os/init.c 0x00E3397C;
previously computed as MST_PAGE_TABLE_BASE + 0x166 pages, now the single
symbol the image's immediate names).  MST itself was already the C object.

**Checks.**  sau2.ld ASSERTs: AREA_$RPMAP_CACHE = OS_PAGE_END on a page;
PMAP_$SEGMAP + 0xFC00 = AREA_$RPMAP_CACHE; the windows in map order, on
pages, with the map's sizes; VM_TABLES_END = IODEFS_GUARD = MSTE_PAGES +
0x166 pages; VM_TABLES_END < 0xFA0000 and <= DISP1_MEM.  `make check-rfc`
item 8, on our image and on the original with the map's values: the
windows in order, page aligned, from OS_PAGE_END up, no page of them in
COLD's dry-run map, no other allocated section overlapping them, MSTE_PAGES
+ 0x166 pages <= VM_TABLES_END < 0xFA0000, <= IODEFS.  Host test
os/test/test_vm_tables.c: the map's relationships against the C records
(sizeof(pmap_$segmap_t), MST_TABLE_ENTRIES, AREA_DISKLESS_PAGE_COUNT,
AREA_SEG_TABLE_POOL_COUNT, MST_MSTE_PAGES_MAX), the rule on this build's
OS_PAGE_END and its headroom, and every consumer macro = the VA of its
symbol; the mst/area tests that model memory stand in for the symbols with
the map's VAs.

## 9. Open questions (status after the review)

1. **Answered** (section 4): sysboot does not write the probed MOVE_TO_PPN
   back; the two agree because the probe returns 0x600 when RAM exists at
   0x180000.
2. **Partly answered** (section 5): the 0x17F696 path gives d1 = 0,
   d2 = flags word, a1 = 0.  The 0x17F604 path's frame is unresolved.
   d1/d2 are boot_params_t {device.w, ctlr.w} and {unit.w, flags.w}.
3. **Answered** (section 8.7): nobody, as a whole; each module's init
   zeroes its own data (OS_$DATA_ZERO).
4. **Answered**: PFT bit 12 = global (section 6); MMAP w0 =
   mmape_t.wire_count 0x80 (kernel) / 0x40 (free, COLD) (section 6); the
   0x10181C table is decoded (section 6) but its user is unknown; the
   CCR test is a no-op CPU sanity check (section 5 step 6).
   **Still open:** the DN330 meaning of the 0x700000 -> 0x400000 window
   (the 26-bit DN330 address map is not in the bytes of this image; the
   kernel's 9 PTT cells are simply moved there), and why the binder pads
   RELOC with ~8 KB of zeros.

### Answered by the Engineering Handbook (2026-10-06, docs/handbook-dn3xx.md 7-1)

The DN330 runs with a 26-bit address space: physical 0x100000 (where
the kernel's data starts, "mem: md data") is virtual 0x3D00000, the I/O
page is at 0x3FFxxxx (mmu 0x3FFB400, sios 0x3FFB000, timers 0x3FFAC00,
ring 0x3FF9C00, disk/tape/cal 0x3FFA800, pft 0x3FFB800) and the page
translation table is at 0x400000 instead of 0x700000.  That is exactly
what the fixup windows do: +0x2F00000 moves the 0xE00000 kernel to
0x3D00000, +0x3000000 moves the 0xFA0000+ I/O references to 0x3FAxxxx,
and the 0x700000 window moves PTT references down to 0x400000.  The
DN300/DN320 keep the 24-bit map (kernel at 0xE00000, I/O at 0xFFxxxx,
PTT at 0x700000), which is why that path needs no fixups.
