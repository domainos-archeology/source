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
  arithmetic, used by a handful of callers.
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

## 4. Host tests

A test that needs a module block gets it for free: the block is an ordinary
object on the host, zero-initialised unless the `_data.c` carries image
contents. Tests that need the target's address space (code that
dereferences stored VAs) set `ARCH_HOST_VA_BASE` to an arena as
`netbuf/test/test_add_pages.c` and `xns/test/test_idp_send.c` already do.
The remaining per-subsystem fallbacks (`XNS_IDP_BASE` pointer,
`pmap_segmap` pointer, `ROUTE_$WIRED_AREA_START_SYM[]`, the
`DISK_VOLUME_BASE` `#undef` idiom in `disk/test/test_io.c`) disappear once
the block is an object.

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
   `make check` verifies the order. `STOP_$DATA.wire_start` is the link-time
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
4. **proc1/proc2/fim/acl per-process arrays** (`PROC1_$TYPE` bias,
   `FIM_$QUIT_EC/VALUE` 12- and 4-byte strides, `ACL_$SUPER_COUNT`,
   `PROC2_UID[58]`): declaration-side bias, use sites direct.
5. **Sweep**: remove `__A5_BASE()` callers, then the macro; ban
   `#if defined(ARCH_M68K)` around data declarations by a grep in the
   Makefile's `check` target.

Keep the tree green throughout: a subsystem converts in one commit; the
linker line and the object land together; other subsystems keep compiling
because the public header still exports the same names (now fields or
`#define NAME (BLOCK.field)` shims for one wave).

## 6. Risks

- **Placement by map order** (revised 2026-09-28, the owner's decision;
  it replaces pinning at original addresses, whose risks were overlap with
  the linker's `.data/.bss` and a sparse RFC image): blocks now sit inside
  the one `.text` output region among the code, so the image stays dense
  and nothing can overlap. The risks that remain: (a) a hand-written file
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
| P3 | Re-express `NAME_$HANDLE_TO_PTR` on `ARCH_VA_TO_PTR` and retire the host registry | S |
| P3 | Remove `__A5_BASE()` and add the Makefile check that forbids `#if ARCH_M68K` around data declarations | S |
| P3 | RFC/linker: confirm sparse VMA support or add padding; document in sau2.ld | S |
