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
`[1..n]` word array is element 1 at `A0`; `(0x3c,A5,D6w)` for a `[0..n]`
longword array).

## 3. Proposed C representation

One rule: **a module block is a single struct object, defined once, whose
m68k placement is the original address, and every access goes through the
object.** No absolute-address macros, no `__A5_BASE()` arithmetic, no
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

`arch/arch.h` provides the family:

```c
/* m68k: object placed by the linker at the original address */
#define MODULE_DATA_DEFINE(T, name, addr) \
    T name __attribute__((section(".moddata." #name), aligned(2)))
/* host: plain object */
#define MODULE_DATA_DEFINE(T, name, addr)  T name
#define MODULE_DATA_DECLARE(T, name)       extern T name
#define MODULE_DATA_ADDR(name)             MODULE_DATA_ADDR_##name  /* 0xE7FD24, from the same macro */
```

and `sau2.ld` gains one output section per module placed at its address
(`.moddata.NAME_$DATA 0xE7FD24 : { *(.moddata.NAME_$DATA) }`), so on m68k
`&NAME_$DATA == (void *)0xE7FD24` and any leftover absolute reference and
the object coincide. The section list is generated from the
`MODULE_DATA_DEFINE` sites (a small script, checked in) so the two cannot
drift. Blocks that contain the module's *code* as well (STOP: A5 = entry
point; the constant cells reached by `pea (d,PC)`) stay as they are: the
constants are file-static `const` objects, the block holds only data.

Per-process arrays follow one convention, enforced by the assert set:

- **Declare the array at the lowest address the code can touch, sized for
  every index the code can produce, and index with the Pascal index the
  assembly computes.** `PROC1_$TYPE` is `uint16_t type[PROC1_MAX+1]` at
  0xE2612A (element 0 at the `-0x2` bias, unused) so `type[pid]` is direct;
  `P2_INFO_TABLE` stays 1-based via `P2_INFO_ENTRY(idx)`; `NAME_$DATA.lock_slot`
  is 0-based because `(0x3c,A5,D6w*4)` starts at A5+0x3C with pid 0.
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
   documented field-by-field in `stop/stop_data.c`).
2. **name** (four lock arrays, A5 0xE7FD24) and **smd** (`smd_globals_t`
   at 0xE82B8C is already asserted end to end).
3. **netlog, xns, pmap, route, rip, asknode, ring, sock, pkt, msg**: each
   has a base macro and a host variable today; the struct exists for most.
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

- **Section placement vs. `.bss`**: placed sections must not overlap the
  linker's own `.data/.bss`; the generator checks addresses against the
  0xE00000+ text range and each other. RFC images become a set of sparse
  ranges; `sau2.ld` already emits `AT()` file offsets, so add the sections
  with `AT()` after `.text` and let the loader map them (verify the RFC
  format supports non-contiguous VMA or fall back to padding).
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
