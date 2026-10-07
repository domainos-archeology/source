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
