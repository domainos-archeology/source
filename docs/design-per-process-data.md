# Design: portable module data and per-process tables (source-0i3)

Status: proposal, 2026-09-06. No code changed by this document.

## 1. Problem

Domain Pascal compiles each module's globals into one data block and
addresses it A5-relative. The tree currently models that in four
incompatible ways, and on the m68k build two of them can name **different
memory** for the same variable:

- **Absolute-address macro on m68k, object on host** (the common `#if
  defined(ARCH_M68K)` split; 60+ blocks in 25 headers). Examples:
  `netlog/netlog_internal.h:104-107` (`NETLOG_DATA` = `0xE85684` vs
  `extern netlog_data_t netlog_data`), `xns/xns_internal.h:30-34`
  (`XNS_IDP_BASE` = `0xE2B314`), `pmap/pmap_internal.h:98-102`
  (`PMAP_SEGMAP` = `0xED4F80`), `route/route_internal.h:183-263`
  (`ROUTE_$WIRED_PAGES` = `0xE87D80` vs `extern uint32_t ROUTE_$WIRED_PAGES[10]`).
- **Plain C object, no address at all**: `smd/smd_data.c:22`
  `smd_globals_t SMD_GLOBALS;` (original 0xE82B8C), `name/name_data.c:29-40`
  (the four `NAME_$LOCK_*` arrays at A5+0x3C/0x13E/0x1BC/0x2B8 =
  0xE7FD60..), `stop/stop_data.c` (A5 = 0xE81814 block), `proc1/proc1_data.c:67`
  `PROC1_$TYPE[65]` (0xE2612A).
- **Register read**: `arch/m68k/arch.h:62` `__A5_BASE()` with byte-offset
  arithmetic, used by a handful of callers (removed 2026-09-29,
  source-702z).
- **Hand-rolled 1-based indexing**: `proc2/proc2.h:398` `P2_INFO_ENTRY(idx)
  = &P2_INFO_TABLE[(idx)-1]`, `proc2_internal.h:79` `PROC_FORK_EC(idx)`,
  `SOCK_$EVENT_COUNTERS[sock-1]`, `smd_$unit_info(unit) = &SMD_DISPLAY_INFO[unit-1]`,
  but `NAME_$LOCK_SLOT[PROC1_$CURRENT]` (0-based, `pea (0x3c,A5,D6w)`) and
  `PROC1_$TYPE[PROC1_$CURRENT]` whose declared base already carries the
  `-0x2` bias the code uses (`cmpi.w #9,(-0x2,A0,D6w)` at 0xE548FC).

The consequences seen in the audit and the fix waves:

1. Off-by-one drift at use sites (audit items: `SOCK_$EVENT_COUNTERS[sock]`,
   segmap base 0xED5000 vs 0xED4F80, `proc1_$type` bias, `smd asid_to_unit`).
2. On m68k the linker places every C object wherever `.bss` lands
   (`sau2.ld` only fixes `.text` at 0xE00000), so `SMD_GLOBALS.default_unit`
   (the object) and `*(uint16_t *)0xE84924` (the old macro) were two
   different words until source-nuan unified them. Any remaining absolute
   macro that overlaps an object is a latent split-brain.
3. Host tests cannot reach code that dereferences a stored VA unless the
   test builds an arena (`arch/host/arch.h:73-87`, `ARCH_HOST_VA_BASE`), and
   each subsystem invents its own host fallback shape.
4. Layout asserts are written per subsystem against the struct, not against
   the module block, so a field can be right relative to its struct and wrong
   relative to A5.

## 2. Semantic model

A5 is established one of three ways (sampled with gsk):

| Module | Instruction | A5 |
|---|---|---|
| OS_$INIT 0xE337F4 | `lea (0xe351f4).l,A5` at 0xE337FC | 0xE351F4 |
| ROUTE_$PROCESS 0xE873EC | `lea (0xe87d80).l,A5` at 0xE873F4 | 0xE87D80 |
| RING_$RCV_FROM_UNIT_PRIV 0xE76048 | `lea (0xe86400).l,A5` at 0xE76050 | 0xE86400 |
| PMAP_$PURIFIER_L 0xE13A9C | `lea (0xe24d44).l,A5` at 0xE13AA4 | 0xE24D44 |
| STOP_$WATCH 0xE81814 | `lea (-0xa,PC),A5` at 0xE8181C | 0xE81814 (own entry) |
| NAME_$LOCK_DIR 0xE54854 | none: inherits caller's A5 | 0xE7FD24 (NAME) |

So: **every Pascal module owns one data block at a fixed address**; entry
points load A5 with it (absolute or PC-relative), nested and sibling
procedures inherit it, and everything the module keeps between calls lives
at `(off,A5)`. Per-process and per-ASID state is an array inside that block
indexed by `PROC1_$CURRENT` or `PROC1_$AS_ID`, with the Pascal lower bound
folded into the base address by the compiler (`(-0x2,A0,D6w)` for a
`[1..n]` word array is element 1 at `A0`; `(0x3c,A5,D6w)` for the `[1..64]`
longword table whose element 1 is at A5+0x40 - corrected 2026-09-28, step 2:
it is not a `[0..n]` array).

## 3. Proposed C representation

One rule: **a module block is a single struct object, defined once, whose
m68k link position follows the SAU2 map's order (placement paragraph
below), and every access goes through the object.** No absolute-address macros, no `__A5_BASE()` arithmetic, no
per-file `#if ARCH_M68K` splits for data.

```c
/* name/name.h */
typedef struct name_$data_t {            /* A5 = 0xE7FD24 */
    uint8_t   _0000[0x3C];
    uint32_t  lock_slot[NAME_$MAX_LOCK_PROCS];   /* +0x03C, [0..57] */
    uint8_t   _0124[0x1A];
    int16_t   lock_mode[NAME_$MAX_LOCK_PROCS];   /* +0x13E */
    ...
} name_$data_t;
MODULE_DATA_DECLARE(name_$data_t, NAME_$DATA);       /* extern */

/* name/name_data.c */
MODULE_DATA_DEFINE(name_$data_t, NAME_$DATA, 0xE7FD24);
```

(The sketch as first proposed.  Step 2, 2026-09-28, landed it as
`NAME_$OLD_DIR_DATA` / `name_$old_dir_data_t` - `NAME_$DATA` is the NAME
segment at 0xE80264 - with the four tables as Pascal `[1..64]`, each declared
at its bias slot; see the per-process convention below and `name/name.h`.)

`arch/arch.h` provides the family:

```c
/* m68k: object in its own section, linked in the map's order */
#define MODULE_DATA_DEFINE(T, name, addr) \
    T name __attribute__((section(".moddata." #name), aligned(2)))
/* host: plain object */
#define MODULE_DATA_DEFINE(T, name, addr)  T name
#define MODULE_DATA_DECLARE(T, name)       extern T name
#define MODULE_DATA_ADDR(name)             MODULE_DATA_ADDR_##name  /* 0xE7FD24, from the same macro */
```

and the block is linked in the order of the SAU2 link map, not at its
address.

*Placement, revised 2026-09-28 (the owner's decision).* Absolute addresses
do not have to match the image; the relative placement does. The kernel is
built with `-ffunction-sections`, every hand-written routine has its own
`.section ".text.<symbol>"` where that keeps its bytes identical, and
`tools/gen_layout_ld.py` writes `build/sau2/layout.ld`, which `sau2.ld`
INCLUDEs inside its one `.text` output section: an input-section statement
per C function, routine and block, in map order, then a catch-all for code
the map does not name. A block's `addr` is its **ordering key** (and
documentation), so a block that follows its module's code in the image
follows it in our link, and modules come in the image's sequence. It is
**not** the link address: code that needs the block's real address uses
`&NAME_$DATA` / `ARCH_PTR_TO_VA(&NAME_$DATA)`, and a cell that ships a
linked object's VA as image contents uses `ARCH_PTR_TO_VA_STATIC(obj,
image_va)`. Consequently absolute-address references to a block no longer
coincide with the object; they have to go (section 5). `make check` links a
scratch ELF and proves the order (0 inversions), counting what the map
places and what falls into the catch-all. The list is generated from the
map, the objects and the `MODULE_DATA_DEFINE` sites, so it cannot drift.
(Superseded: the first implementation, 3db3e5b, pinned each block at its
original address with one output section per block.) Blocks that contain the module's *code* as well (STOP: A5 = entry
point; the constant cells reached by `pea (d,PC)`) stay as they are: the
constants are file-static `const` objects, the block holds only data.

Per-process arrays follow one convention, enforced by the assert set:

- **Declare the array at the lowest address the code can touch, sized for
  every index the code can produce, and index with the Pascal index the
  assembly computes.** `PROC1_$TYPE` is `uint16_t type[PROC1_MAX+1]` at
  0xE2612A (element 0 at the `-0x2` bias, unused) so `type[pid]` is direct;
  `P2_INFO_TABLE` stays 1-based via `P2_INFO_ENTRY(idx)`;
  `NAME_$OLD_DIR_DATA.lock_slot` is declared at A5+0x3C because
  `(0x3c,A5,D6w*4)` is element 0 of a Pascal `[1..64]` table whose element 1
  is at A5+0x40 (step 2, 2026-09-28: the four lock tables are `[1..64]`, not
  58 entries - element 1 of each follows the previous object and element 64
  ends the 0x4C0-byte OLD_DIR segment).  Where a bias slot overlays the
  previous object inside one block, the block is a union of one arm per
  object so both names keep their A5 offsets (`name/name.h`).
- Never write `[x-1]` or `[x+1]` at a use site. If the assembly has a bias,
  the bias goes into the declaration (base address and bound), once, with the
  citing instruction in a comment.
- Every per-process array carries `_Static_assert(offsetof(...) == A5 offset)`
  and a stride assert, guarded only when the element holds a pointer.

`PER_PROC(block, field)` is deliberately **not** a macro: `NAME_$DATA.lock_mode[pid]`
reads better than any wrapper and the assert set is what prevents drift.

Stored virtual addresses (fields that are `uint32_t` VAs in the image)
keep `ARCH_VA_TO_PTR`/`ARCH_PTR_TO_VA` (`arch/m68k/arch.h:49`,
`arch/host/arch.h:86`). `NAME_$HANDLE_TO_PTR` (`name/name.h:245`) is the same
idea for handles and should be re-expressed on top of it (bead below).
(Done 2026-09-29, source-702z: `NAME_$HANDLE_TO_PTR` / `NAME_$PTR_TO_HANDLE`
are `ARCH_VA_TO_PTR` / `ARCH_PTR_TO_VA` on every build, the host handle
registry `name/handle_map.c` is gone, and the dir and name tests keep their
directories in an `ARCH_HOST_VA_BASE` arena.)

## 4. Host tests

A test that needs a module block gets it for free: the block is an ordinary
object on the host, zero-initialised unless the `_data.c` carries image
contents. Tests that need the target's address space (code that
dereferences stored VAs) set `ARCH_HOST_VA_BASE` to an arena as
`netbuf/test/test_add_pages.c` and `xns/test/test_idp_send.c` already do.
The remaining per-subsystem fallbacks (`XNS_IDP_BASE` pointer,
`pmap_segmap` pointer, `ROUTE_$WIRED_AREA_START_SYM[]`, the
`DISK_VOLUME_BASE` `#undef` idiom in `disk/test/test_io.c`) disappear once
the block is an object.  (2026-09-29: `DISK_VOLUME_BASE` is now `DISK_$DATA`
on every build, so the tests' `#undef` only redirects it to their own
buffer.  A host test that runs code touching a SAU2 register defines the
`SAU2_` name from `arch/m68k/sau2/hw.h` itself, or the `arch_$io_read8` /
`arch_$io_write8` hooks, `arch_$vector_table`, `arch_$prom_machine_id` the
host arch header declares.)

## 5. Migration order

Each step: introduce the block struct with asserts against the A5 offsets
recorded in the file comments and Ghidra, convert the subsystem's accesses,
delete its `#if ARCH_M68K` data macros, add the linker line, rebuild with
`-Werror`, `make test`. Start where the block is already a struct:

1. **arch + Makefile + sau2.ld**: the `MODULE_DATA_*` macros, the section
   generator, one linker section. Prove it on `stop` (block already
   documented field-by-field in `stop/stop_data.c`). Amended 2026-09-28:
   the generator orders instead of pins - `-ffunction-sections`, per-routine
   sections in the hand-written assembly (byte-identical per file), and
   `build/sau2/layout.ld` listing code and blocks in SAU2-map order;
   `make check` verifies the order.  Amended 2026-09-29 (source-6psc): the
   hand-written assembly is no longer byte-identical per file - a data
   cell or routine it reached by its image address names different memory
   once nothing is pinned, so those operands are symbol references
   (`.extern`, or `.set NAME, BLOCK + off` aliases) and `make check`
   (`check-asm`, `tools/asm_compare.py`) compares each file with the image
   bytes in `tools/asm_image_ref.txt` modulo the relocation fields:
   encodings identical, every relocated operand resolved in the image's
   address space against the image operand.  Four `(d16,An)` operands whose
   base register held an image address widened to `(xxx).l`; hardware and
   PROM addresses stay literal. `STOP_$DATA.wire_start` is the link-time
   address of `STOP_$WATCH`; `wire_end` keeps the image literal until the
   FILE_ block exists (source-h5ro).
2. **name** (four lock arrays, A5 0xE7FD24; done as `NAME_$OLD_DIR_DATA`,
   since `NAME_$DATA` already names the NAME segment at 0xE80264) and **smd** (`smd_globals_t`
   at 0xE82B8C is already asserted end to end).
3. **netlog, xns, pmap, route, rip, asknode, ring, sock, pkt, msg**: each
   has a base macro and a host variable today; the struct exists for most.
   Amended 2026-09-29 (netlog/xns/pmap step, source-iq58): blocks
   `NETLOG_$DATA` (0xE85684), `XNS_ERROR_$DATA` (0xE2B29C), `XNS_IDP_$DATA`
   (0xE2B314), `PMAP_$DATA` (0xE24D44, which also holds the map's
   `MOUNT_LOCK` and the exported `PMAP_$*` scalars, now fields) and
   `PMAP_$SEGMAP` (map `AST_PMAPS` 0xED5000, 0xFC00).  The segment map's
   bias row (0xED4F80) lies in the gap before `AST_PMAPS`, outside every
   map segment, so like `P2_INFO_ENTRY` it is declared from row 1 and
   `PMAP_SEGMAP_ROW(seg)` applies the bias once; `ast/ast.h`'s
   `SEGMAP_BASE` is a shim onto the block until the AST step (source-avdg).
   Segments whose cells are exported globals addressed by name and never
   through A5 (`NETLOG_ASM` 0xE248E0) stay individual objects
   (ordering them is source-91vs).
   Amended 2026-09-29 (pkt/msg/asknode/route step, source-r3tc,
   source-3llq, source-esg8, source-ybch): blocks `PKT_$DATA` (0xE24C9C;
   the missing-node table is Pascal `[1..10]` from +0, its bias slot falls
   in PEB_PARITY, so `PKT_MISSING_ENTRY(k)` applies the bias once),
   `MSG_$WIRED_DATA` (0xE242E4: `MSG_$SOCK_LOCK`, `DPAGE_*`) and
   `MSG_$UNWIRED_DATA` (0xE80D84: the send template, and `depth` /
   `ownership` declared from their bias slots as union arms and indexed
   with the socket), `ASKNODE_$DATA` (0xE82408: the packet-info template
   whose last word is the protocol version) and `ROUTE_$WIRED_DATA`
   (0xE26EE4), `ROUTE_$UNWIRED_DATA` (0xE825DC) and `ROUTE_$RTWIRED_DATA`
   (0xE87D80; its `user_stat` is Pascal `[1..4]` whose bias slot is never
   addressed, so it is declared from record 1 and `ROUTE_USER_STAT_ENTRY(n)`
   applies the bias once, as `PKT_MISSING_ENTRY` does).  The route blocks
   are public (route/route.h); users in app, hint, mac, mac_os, msg,
   net_io, network, pkt, rip and xns name the fields directly.  Two image values the old host objects had wrong
   are now the image's: MSG's send template and `DPAGE_LOCK` = -1.
   Amended 2026-09-29 (ring/rip/sock step, source-vulx, source-thww,
   source-gy7x): blocks `SOCK_$DATA` (0xE27510: `socket[]` and
   `socket_ptr[]` both Pascal `[1..0xE0]` declared from their bias slots as
   union arms - `socket[0]` overlays SOCK_LIST, `socket_ptr[0]` is the spin
   lock and `socket_ptr[0xE1]` the user-limit word - so every user indexes
   with the socket number; the free list threads VAs), `RIP_$WIRED_DATA`
   (0xE26258, holding `RIP_$STATS`, `RIP_$INFO`, the recent-change flags and
   the two foreign locks `XNS_ERROR_$CLIENT_MUTEX` / `ROUTE_$SERVICE_MUTEX`,
   now fields), `RIP_$INIT_DATA` (0xE3502C) and `RIP_$RTWIRED_DATA`
   (0xE87D68), `RING_$WIRED_DATA` (0xE261AC: the swdiag counters and
   `RING_$DATA` as `stats[0..1]`), `RING_$CTL` (0xE86400, with the ring
   `net_io_$driver_t` at +0x518 and its procedure variables as image
   contents), `RINGLOG_$CTL` (0xE2C32C; `wired_pages` is `[1..10]` whose
   bias slot lies in PCHIST, so `RINGLOG_WIRED_PAGE(k)` applies it) and
   `RINGLOG_$DATA` (0xEA3E38).  The ring unit record's channel and
   packet-type tables keep element 1 at index 0 behind `RING_UNIT_CHANNEL` /
   `RING_UNIT_PKT_TYPE`, because a bias-slot union arm would sit elsewhere
   on a 64-bit host (pointers precede them).  PC-relative cells formerly
   spelled as absolute macros (`RINGLOG_$ROUTE_FORWARD`, `RTWIRED_$CALLBACK`,
   `RIP_$ANNOUNCE_EXTRA`) are file-scope `const` objects.
4. **proc1/proc2/fim/acl per-process arrays** (`PROC1_$TYPE` bias,
   `FIM_$QUIT_EC/VALUE` 12- and 4-byte strides, `ACL_$SUPER_COUNT`,
   `PROC2_UID[58]`): declaration-side bias, use sites direct.
   Amended 2026-09-29 (proc1/fim half, source-l2yd): blocks `PROC1_$DATA`
   (0xE254E8, 0xCC4: `loadav`, `loadav_elem`, and four pid tables each
   declared from its bias slot as a union arm - `ts_elem[0..64]` over
   `loadav_elem`, `os_stack_base[0..64]` (map OS_STACK_BASE is element 0),
   `stats[0..64]` over `os_stack_base[62..64]`, `type[0..64]` over the low
   word of `stack_low_water`; the stack cells and `os_stack_base` are target
   VAs, so the block is pointer-free and only `time_queue_elem_t`'s host
   padding keeps the late offsets target-only asserts), `FIM_$DATA`
   (0xE2126C, 0x134: `in_fim`, `user_fim_addr`, the frame size table, whose
   image bytes differ from the old host table) and `FIM_$WIRED_DATA`
   (0xE21FE6, 0x796: the data run of the FIM_WIRED code segment - parity
   state, `MISS_STATUS`, `pending_trace_faults` and the per-ASID
   `quit_ec`/`quit_value`/`trace_sts`/`quit_inh`/`deliv_ec`, all `[0..57]`
   from their map symbols, eventcounts self-linked and `quit_inh` 0xFF as in
   the image).  PROC1_ASM cells (`PCBS`, `PROC1_$CURRENT`, ...) and the
   FIM_UNWIRED/FIM_WIRED code-segment cells stay individual or
   assembly-owned objects; `svc/sau2/trap8.s`, `fim/sau2/fim.s` and
   `fim/sau2/bus_err.s` reach the block fields through `.set` aliases with
   their bytes unchanged.  PARITY's absolute state macros became
   `FIM_$WIRED_DATA.parity` fields.
   Amended 2026-09-29 (proc2/acl half, source-l2yd): blocks
   `PROC2_$UNWIRED_DATA` (0xE7BE84, the PROC2 A5 block: the per-ASID
   `uid[0..57]` from its map symbol PROC2_$UID, list heads, boot flags,
   `next_upid` = 0x41 as in the image), `PROC2_$DATA` (0xEA551C: `info[]`
   stays 1-based via `P2_INFO_ENTRY` because entry 0 is the tail of
   XPD_$DATA - PROC2_$DETACH_FROM_PARENT really writes it, source-c6cy -
   while `pid_to_index[0..64]` and `pgroup[0..70]` are union arms, pgroup's
   bias slot over pid_to_index[61..64]), `PROC2_$WIRED_DATA` (0xE2B978, the
   PROC2_WIRED_ASM segment: the fork / creation-record eventcount pairs,
   pair 0 outside the segment, so `PROC_FORK_EC` / `PROC_CR_REC_EC` keep the
   bias), `ACL_$UNWIRED_DATA` (0xE7CF54: workspace, image buffer, cache
   directory, 61 hash buckets - not 64, ACL_$ENTER_SUBS's magic is at
   A5+0xB6C - locksmith state, and `super_count[0..64]` as a union arm
   over the LRU head; public in acl/acl.h for REM_FILE), `ACL_$DATA`
   (0xE88834: the image cache and seven per-process tables reached from
   the shared 0xE97294 base, each `[0..64]` from its bias slot; the bias
   slots chain through the tails of the preceding tables, so the block is
   one union of arms each padded from the block start) and
   `ACL_$WIRED_DATA` (0xE2C014: `ACL_$EXCLUSION_LOCK`).  `PTR_PROC2_$DATA`
   (0xE3238C, read by MST_$WIRE_AREA as a longword VA) is a stored-VA
   cell, `ARCH_PTR_TO_VA_STATIC(&PROC2_$DATA, 0xEA551C)`.  The six 4-byte PROC2/ACL_ A5 anchor segments
   hold nothing C addresses.
5. **Sweep**: remove `__A5_BASE()` callers, then the macro; ban
   `#if defined(ARCH_M68K)` around data declarations by a grep in the
   Makefile's `check` target.
   Amended 2026-09-29 (source-702z): `__A5_BASE()` is gone from both arch
   headers; its one caller (AST_$SET_ATTR_DISPATCH's timestamp mask at
   A5+0x48C) reads `ast_$attr_timestamp_mask`, a plain object with the
   image value 0x0278301C until the AST_ block exists.  The ban is
   `make check-guards` (`tools/check_guards.py`, part of `make check`): it
   scans every header and source outside `arch/` and fails on an
   architecture guard (any spelling, `ARCH_M68K` or `ARCH_HOST`) that holds
   an object declaration or definition, an `extern`, a `#define` casting an
   absolute address to a pointer (a literal or a macro that expands to one)
   or a struct / union definition; asserts, host fallback bodies of
   hand-written routines and section macros stay allowed.
   `tools/arch_m68k_guards.md` classifies all 213 remaining guards.  The
   sweep converted to blocks `APP_$DATA`, `HINT_$DATA`, `NETBUF_$DATA`,
   `PEB_$INFO` (PEB_PARITY), `WIN_$DATA` (with the image's driver entry
   table as code VAs), `XPD_$DATA` (closing source-c6cy), `VOLX_$DATA`,
   `MMAP_$DATA` (the MMAP_ segment; `mmu/sau2/remove_asid.s` and
   `peb/sau2/int.s` now reach it and `PEB_$INFO` through `.set` aliases,
   closing source-alpj and source-i1uu) and `DXM_$SIGNAL_ROUTINES`
   (DXM_WIRED_), bringing the block count to 42; made the ML lock arrays,
   `HINT_$HINTFILE_PTR`, `HINT_$EXCLUSION_LOCK` and `M68881_EXISTS` plain
   objects on both builds; pointed the FILE_ lock-table bases,
   `DISK_$PER_PROC` and `DISK_VOLUME_BASE` at the objects that already
   existed; moved hardware addresses to `arch/m68k/sau2/hw.h` (DMAC, memory
   error status, PAR_BUFF, PEB pages, calendar, timer) and PROM / vector
   cells to the arch headers (`ARCH_PROM_MACHINE_ID`, `ARCH_VECTOR`,
   `ARCH_IO_READ8/WRITE8`); and deleted unused spellings and host-only
   definitions from the kernel's `_data.c` files.  Sixteen guards remain,
   each listed in the script's `EXEMPT` table with its bead (an entry that
   stops matching fails the check): the AST_ block and tables
   (source-gmxj), the DIR block (source-qiby), the MMAP page table
   (source-fyjc), the MMU_ASM cells and registers (source-o56c) and the
   interrupt stack (source-4k71).

   *Closing note, 2026-09-29.*  With source-702z the five steps are done;
   what remains under source-0i3 is follow-up beads, all P3 unless noted:
   blocks not yet converted - source-gmxj (AST_ segment and AST / AOT
   tables), source-qiby (DIR segment), source-fyjc (MMAP page table),
   source-tfcy (NAME segment), source-ppgz (PARITY), source-dn79 (SMD
   0xE2E060), source-bqdf (the `MMAP_$WS_OWNER` bias; the MMAP_ block
   itself is done), source-avdg (AST `SEGMAP_BASE`); ordering of map-named
   plain globals - source-91vs; link-time values of image cells -
   source-82cs, source-2e9k, source-h5ro; hand-written assembly still
   carrying image addresses - source-o56c, source-4k71, source-k79b,
   source-nojc; assembly fidelity - source-kt66 (P2), source-c573,
   source-9jiy; untranslated code the blocks refer to - source-m5y2 (P2),
   source-6co (P2); host-test hygiene - source-ivp9 (P4).  (The PEB boolean
   typing bug the sweep exposed, source-uw36, was fixed in the step's review.)

   Amended 2026-09-29 (AST step, source-gmxj, with source-avdg): blocks
   `AST_$DATA` (0xE1DC80, 0x498: AOTH's 251 chain heads, the AOTE / ASTE
   free heads, scan positions and limits, `dism_seqn`, the three
   self-linked eventcounts, the allocation statistics, `AST_$NOT_FOUND`,
   the update cursor and timestamp, the attribute timestamp mask and the
   clobbered UID, all fields; `vol_indices[0..6]` is a union arm over
   `dism_ec` whose bias slot is the eventcount's last word, the arm's pad
   sized from the eventcount so element 1 follows it on every build) and
   `AST_$AOT` (map AST_AOT 0xEC5400, 0xF960: `aste[504]` from the map's
   AST and `aote[280]` from AOT; the ASTE index is Pascal 1-based and
   element 0 would fall in AUDIT_LIST, so `AST_ASTE_ENTRY(seg)` applies the
   bias once, as `PMAP_SEGMAP_ROW` does).  The list heads, limits and hash
   chains stay pointers (the aste_t / aote_t records they chain already
   are), so the table cells are initialised with the arrays' addresses
   rather than `ARCH_PTR_TO_VA_STATIC` and the block's offsets are
   target-only asserts.  `MMAP_$SEG_ASTE_FOR`, OSINFO_$GET_SEG_TABLE's AST
   base and PMAP's ASTE lookups name the table; every AST segment-map site
   is `PMAP_SEGMAP_ROW(seg)` and `SEGMAP_BASE` is gone.  Eleven exempted
   guards fewer: five remain (DIR, MMAPE, MMU x2, interrupt stack).

Keep the tree green throughout: a subsystem converts in one commit; the
linker line and the object land together; other subsystems keep compiling
because the public header still exports the same names (now fields or
`#define NAME (BLOCK.field)` shims for one wave).

## 6. Risks

- **Placement by map order** (revised 2026-09-28, the owner's decision;
  it replaces pinning at original addresses, whose risks were overlap with
  the linker's `.data/.bss` and a sparse RFC image): blocks now sit inside
  the one `.text` output region among the code, so the image stays dense
  and nothing can overlap.  *Resolved 2026-09-29 (source-702z):* the
  sparse-VMA question is obsolete, because nothing is pinned: `.text`,
  `.rodata` and `.data` each start where the previous one ends (the scratch
  link has `.text` 0xE00000..0xE9FC8C, `.rodata` to 0xEA0AB6, `.data`
  0xEA0AB8..0xEA3BB4, `.bss` after it; the 2026-09-29 review link, and
  the figures move with every build), so the flat RFC image is contiguous
  and needs neither sparse-VMA support nor padding; `sau2.ld` says so.  The
  converse risk remains for the exempted guards (section 5, step 5): an
  image address still used on the target now lies inside our own `.text`
  (0xE7DC00 DIR; the AST_ one, 0xE1DC80, is gone with source-gmxj) or `.bss` (0xEB2800 MMAPE_BASE, 0xEB2BE8
  interrupt stack), which is why those beads matter. The risks that remain: (a) a hand-written file
  whose routines interleave with other code in the image and cannot be
  split without changing its bytes holds some map symbols out of order
  (today `fim/sau2/fim.s`, placed with its largest run at `FIM_$UII`; the
  check lists the 16 symbols it holds and excludes them); (b) code the map
  does not name (renamed or local helpers, a few assembly files) lands in
  the catch-all at the end of `.text`, so a 16-bit PC-relative reference
  into it can overflow - the link reports that as a truncated relocation;
  (c) any remaining absolute-address macro for a block now names different
  memory from the object, so those macros must be converted before, not
  after, their blocks are defined; (d) image cells holding VAs of linked
  objects must use `ARCH_PTR_TO_VA_STATIC`, not the image literal.
- **Image contents in static initialisers**: blocks that ship pre-set
  values (`FP_$EXCLUSION` self-links, `RIP_$DATA` entries, ring
  `open_version`) need `.data` not `.bss`; the macro takes an initialiser.
- **GCC alias attributes** (`ROUTE_$PORT`, `NETWORK_$CAPABLE_FLAGS`) become
  plain field accesses inside a block and the aliases go away.
- **Packed cells and endianness**: byte fields inside word slots (`(0x15,A6)`
  style, `st -(SP)` high-byte booleans) stay explicit `uint8_t` pairs;
  `BE16/BE32_CONST` only for on-disk images.
- **Aliasing through `uint8_t *` views** (`XNS_IDP_BASE + off`): the
  conversion removes them; where an untyped view is unavoidable use `memcpy`.
- **Two names for one address** (`SMD_EC_1` inside `SMD_DISPLAY_UNITS`):
  express as a field of the block, not a second object.

## 7. Proposed beads

| Priority | Title | Size |
|---|---|---|
| P2 | arch: MODULE_DATA_DEFINE/DECLARE macros, section generator, sau2.ld hook; convert `stop` as the pilot | M |
| P2 | name: `name_$data_t` block at 0xE7FD24 with the four lock arrays, delete per-array objects | S |
| P2 | smd: place `SMD_GLOBALS` and `SMD_DISPLAY_UNITS` via MODULE_DATA; drop remaining absolute macros in smd_internal.h | S |
| P2 | netlog/xns/pmap: replace `NETLOG_DATA`, `XNS_IDP_BASE`, `PMAP_SEGMAP` base macros with blocks | M |
| P2 | route/rip/asknode/ring/sock/pkt/msg: same, one bead per subsystem (7) | M each |
| P2 | proc1/fim/acl/proc2 per-process arrays: declaration-side bias, direct indexing, stride asserts | M |
| P3 | Re-express `NAME_$HANDLE_TO_PTR` on `ARCH_VA_TO_PTR` and retire the host registry (done 2026-09-29, source-702z) | S |
| P3 | Remove `__A5_BASE()` and add the Makefile check that forbids `#if ARCH_M68K` around data declarations (done 2026-09-29, source-702z: `make check-guards`) | S |
| P3 | RFC/linker: confirm sparse VMA support or add padding; document in sau2.ld (resolved 2026-09-29 by the map-order decision: the image is contiguous, nothing to confirm or pad; sau2.ld documents it) | S |
