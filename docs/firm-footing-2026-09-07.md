# Firm footing pass, 2026-09-06 to 2026-09-07

Follow-up to `docs/audit-2026-09-06.md`. That audit sampled 20 functions and
found the emitted C mostly unfaithful to the SAU2 image (median fidelity
1.5/5). The user's instruction was: file beads for every issue, fix them with
agents, and get on firm footing before translating more code. This note
records what was done, how it was verified, and what the tree looks like now.

## Method

Each wave ran several owner agents (Opus, `reverse-engineer` type) on disjoint
subsystems. Agents never committed. After every wave the coordinator re-ran the
gates on the combined tree, spot-checked the highest-risk claims against the
image with `gsk`, and only then committed locally (no push, per the user).

Gates, all of which pass at every commit since wave 7:

- `make clean && make -k -j8` with `-Werror`: no compile diagnostics; the link
  fails only on symbols not yet emitted.
- duplicate-global scan over every `build/sau2` object: empty.
- `make test`: every host test passes (150 at the start of wave 7, 182 now);
  every test `#include`s the real `.c` it covers.
- relocation overflows at link: 19 before, 0 now.
- archivist rules on changed files: no `extern` in `.c`, first include is the
  subsystem's own header, no prose ellipses, every TODO cites a bead id and an
  address, no cross-subsystem `_internal.h` includes, no foreign-namespace
  declarations (checked map-driven).

## What changed (waves 7 to 17, 52 local commits)

Representative corrections, each cited in the code with the instruction
address that proves it:

- **Layouts**: VOLX table addressed one entry past a 1-based index with
  negative displacements (was one record off); lock-object table collapsed to
  the proven 0x1C-byte record, 1792 entries plus the sentinel the chain relies
  on; `bat_$volume_t` is 0x234 bytes with 64 partitions at +0x2C; the AOTE's
  0x28/0x30/0x38/0x40 are the DTM/DTU/DTV/DTA clocks and 0x9C..0xBB is an
  embedded object-location record; the FIM per-address-space tables are 58
  long, pinned by a closed chain of adjacent objects; `proc2_info_t` +0x1C is
  the entry's own index; the MMAP_ module block is one 0xAA8-byte object;
  ring_info_t recovered field by field from the responder that fills it.
- **Behaviour**: `PROC2_$DELIVER_FIM` tested a status byte through a cast that
  was always false on every host; `MST_$INIT` had its word and bit indices
  swapped and two loops that would run 65536 times as unsigned; `dismount`
  inverted a Domain boolean; `do_op` called client request builders where the
  image calls the server handlers, with two argument pairs swapped; several
  routines had their success tails missing.
- **Assembly**: syscall trap dispatchers, PEB interrupt handler, FIM trace
  data, SMD cursor thunks and body, ec advance entries are byte-identical to
  the image modulo relocation fields; a shared macro emits Apollo's CMP
  encoding that gas will not produce.
- **Names**: the SR10.2 SAU2 tarball holds the linker map of this exact image
  (`~/src/domainos-archeology/sau2-maps/domain_os.10.2.map`). Its names now
  win everywhere; 13 disagreements renamed, 43 data labels added, 283
  undefined data cells defined from it and from the image bytes, status names
  taken from the SR10.x status database, one hardware register named from the
  Domain Engineering Handbook.
- **Hygiene**: `boolean` is `int8_t` so Pascal-style `< 0` tests are host
  independent; every foreign-namespace declaration moved to its owner's public
  header; every status code has one name in one header.

Figures at the last commit:

| measure | value |
|---|---|
| host tests, failures | 182, 0 |
| undefined link references (all functions now) | 367 link lines (137 unique symbols), from 1212 |
| relocation overflows | 0, from 19 |
| hand-written assembly files under `*/sau2/` | 42 |
| beads closed in this pass | about 50 |

## What is left

Fidelity backlog: `source-4omb` (two unknown bytes in ring_info_t) and
`source-xx4l` (the two 0x0002 head words of MEM_$MEM_REC), both research
questions with no code consequence.

Blocked on user review: `docs/design-per-process-data.md` (bead `source-0i3`,
P1) and its seven step beads. Nothing in this pass pre-empted its
`.moddata.<name>` scheme; image-adjacent code constants use `.text.<name>`
sections listed in `sau2.ld` in image order.

Deferred by instruction: the "Complete/Implement/Emit remaining <subsystem>"
translation beads (about 35). The 367 undefined references (137 unique symbols) are that work.

## How to keep the footing

- Before naming anything, grep the SAU2 map for the address; the map wins.
- Before inventing a status name, query the status database.
- Before a hardware register or on-disk record name, search
  `docs/apollo-docs/` (Engineering Handbook for I/O, AEGIS Internals for disk).
- Every recovered layout gets `_Static_assert`s; every claim gets the
  instruction address in a comment and in Ghidra.
- Never commit an agent's work on its own report: rerun the gates, spot-check
  the riskiest claims with `gsk`, then commit.

## Addendum, 2026-09-08: whole-tree completeness review

The independent review of waves 8-17 passed, but a bytes-per-C-line
ranking (Ghidra function size over non-comment C lines) then exposed a
defect class the audit had not caught: functions silently abbreviated
with no TODO.  The ranking placed 674 of the 1317 source files by the
address in their header comment (the other 643 cite it in a form the
ranking script did not recognise and were NOT reviewed; see below).
Every one of the 656 placed functions was walked block by block against
its disassembly by read-only Fable forks:

| tier | files | defective |
|---|---|---|
| over 150 bytes, ratio 6 and up | 81 | 33 |
| over 150 bytes, ratio under 6 | 257 | 118 |
| under 150 bytes | 318 | 42 |

Recurring defect shapes: dropped control-flow arms; invented early
returns, guards and status mappings; NULL or private copies for
`pea (d,PC)` constant cells; flattened NAME_$UNLOCK_DIR status tails;
mis-laid DO_OP and rem_file request/reply records; callee output records
split into adjacent locals; big-endian-only byte casts; loop counts,
walk direction, bit positions (byte ops on the high byte of a word) and
byte booleans declared as words.  All of it was beaded, fixed by Opus
owner agents, verified by Fable forks against the image and committed in
25 local commits.  Gates on HEAD: 0 diagnostics from a clean build, no
duplicate globals, 271 host tests passing, 381 undefined-reference lines
at link (up from 367 because more callees are now declared).

Quality-gate change: `make clean && make` is now required; incremental
builds hid a prototype mismatch across headers.

**Not reviewed (correction, 2026-09-08):** 647 source files never
entered the ranking, concentrated in proc2 (78), ast (58), disk (43),
proc1 (41), time (40), tty (34), mmap (29), mmu (21), sio (18), term
(17), cal, ec, rem_name, name, mst, kbd, pmap, vtoc and smaller
subsystems.  226 of them hold a function of 150 bytes or more (99 KB of
image code).  Given the defect rates above they should be re-emitted
from the disassembly rather than verified; the list is
scratchpad/unreviewed.txt for this session and can be regenerated from
the header addresses.

Still open: the deferred translation beads, seven research beads
(ring_info_t bytes, MEM record head words, RINGLOG_$CNTL block,
route_$port_t 0x4C..0x57, the 2LONG1 counters, app header direction),
the per-process-data design (source-0i3) awaiting review, and a few P3
convention items (audit_data_t host pointers, PROC1_$TYPE base,
0xE825DC's two names, AST_$TRUNCATE's remote DTM copy-back).

## Addendum, 2026-09-28: re-emission of the unreviewed block

The 647 files the 2026-09-08 correction listed were not verified; they
were re-emitted from the disassembly.  Method, chosen by the owner on
2026-09-08: the files were split into 21 batches by subsystem (645
batch entries, some batches merged at commit time), and each batch was
handled in a single pass by two Fable agents with no ping-pong between
them.  The first agent re-emitted every file from `gsk analyze` under
the same rules as the wave 7-17 fixes (argument order from the callee's
frame, `pea (d,PC)` cells as named statics with the image bytes,
byte-in-word parameters, 0xFF booleans, nested procedures as statics
with explicit uplevel parameters, hand-written routines as byte-compared
`<subsystem>/sau2/*.s`, `_Static_assert`ed layouts, a host test per
non-trivial function).  A second, separate Fable agent then walked the
whole batch block by block against the image, fixed deviations in
place, and ran the gates; the coordinator committed only after that
report.  From 2026-09-22 agents ran strictly one at a time.

Result: 16 local commits between b18c8a9 and d484c88 touching 989 files
under domain_os (88,120 insertions, 34,447 deletions).  66 hand-written
routines now live as `sau2/*.s` files assembled and compared byte for
byte against the image (mmu's whole MMU_ASM segment, the proc1 ready
list, EC wait gates, xns IDP checksum, time's VT timer write, and
others).  Defect shapes found in the old files matched the earlier
waves, plus a few new ones: swapped selector bits and argument pairs
(SIO_$K_SET_PARAM, SIO2681 set_baud_rate/set_line, WIN DISK_INIT), whole
tables wrong from a given slot (SVC TRAP8 from 0x12), invented early
returns and count==0 guards, uninitialised locals stored whole (RIP
table entries), high-byte booleans passed as 0x00FF instead of 0xFF00
(pmap purifier callback), one-based table indexing modelled as
zero-based (MST pages, PROC1_$TYPE), and big-endian-only word slicing.
The reviewers' own fixes on top of the emitters were small and specific
(SIO state bits in the low byte, a 28-byte character set, a
`dbf` label one instruction early in remove_virtual.s, an `andi`
encoding, a flags-word mask, ASTE_FLAG_DIRTY's bit position).

Gates on HEAD: 0 diagnostics from `make clean && make` (only
unresolved-symbol link errors, 370 lines), no duplicate globals, 480
host tests passing.  Two clean-build errors in the already-reviewed tree
(clock_t arguments in proc2/get_cpu_usage.c and time/q_setup_timer.c)
were found and fixed on the way.

Still open: the seven source-0i3 per-process-data design steps (design
approved 2026-09-08), the deferred translation beads, and the follow-up
beads the agents filed during re-emission (mostly P3/P4: source-k3o7
EC_$WAIT/WAITN as assembly, source-w78q mmu_$installi's register ABI,
source-8yhy, source-u4mr, source-2eua, source-w0xm, source-fsw6,
source-ilw0, source-cu7q, source-lryi, source-og4f, source-lu78,
source-t3cp) alongside the research and convention items listed above.

## Addendum, 2026-09-29: per-process data design implemented

The seven steps of docs/design-per-process-data.md (bead source-0i3)
landed in 15 local commits between 3db3e5b and the FIM re-transcription
that follows the last block step, each implemented by an Opus 5.5 agent
and reviewed and fixed in place by a separate Fable 5.1 agent before
commit.  Two decisions by the owner changed the design as it went:
nothing is pinned at an image address, code and data are ordered like
the SAU2 link map (tools/gen_layout_ld.py generates the ordered section
list and `make check` verifies the order); and PROM addresses are shared
m68k defines while hardware register addresses live in the per-SAU
header arch/m68k/sau2/hw.h.

Result on HEAD: 49 module data blocks placed in map order, every
absolute-address macro and every m68k-conditional data declaration gone
(tools/check_guards.py, exemption table empty), all 66 hand-written
assembly objects (102 sections) identical to the image modulo relocation
fields with every relocated operand verified against the image operand
(tools/asm_compare.py against the checked-in reference bytes), 0 build
diagnostics, 133 unresolved link symbols (the untranslated functions),
502 host tests passing.

Beyond the design itself the work exposed and fixed: the name lock
tables are Pascal [1..64] not 58-entry tables; the ACL hash table had
64 buckets where the image has 61 and overran a used cell; several data
blocks carried wrong initial values (pmap scan parameters, msg dpage
lock, fim frame sizes); the old "byte-identical" claims for the hand
assembly were false for 32 of 73 sections, including four real
transcription errors and a fim.s that diverged in 18 of 23 routines;
six hand-assembly files embedded absolute image addresses for data that
map-order placement had moved; NETWORK_\$DO_REQUEST read one socket slot
too far; OS_\$INIT stored the null procedure's first code bytes instead
of its address; the PEB flag bytes were unsigned where the image tests
the sign.

Still open under the design: P3/P4 follow-ups (NAME_\$DATA at 0xE80264
as a block, the PARITY block, ordering of the PROC1_ASM and NET_IO cells,
MMAP_\$WS_OWNER's bias, a few wire-range cells that name untranslated
code, mmu_\$installi's register ABI, PROC1_\$CLR_LOCK's shared tail).
Next phase: the 133 untranslated functions, largest first, with the
same emit-then-review pattern.

## Addendum, 2026-10-06: the SAU2 kernel links

The 133 functions that were still unresolved at link when the
per-process-data design closed (2026-09-29) were translated in nine
batches between 2026-09-29 and 2026-10-06, each emitted by an Opus 5.5
agent from the disassembly and walked block by block against the image
by a separate Fable 5.1 agent before commit (9 commits, 412 files
touched, 42,649 lines added).  Every function the SAU2 link map names
and that something in the tree calls now has a definition (a 2026-10-06
triage found about 25 uncalled map routines still untranslated,
source-5v9m, and a dozen map-named routines whose files carry a silent
'return ok' body rather than a translation: AREA_$REMOVE_SEG,
AREA_$DEACTIVATE_ASTE, PROC2_$DELETE_CLEANUP, two DIR readu helpers and
seven ring driver routines, tracked under source-u4b, ld0, qgq, 6co); the 53 module-local helpers that existing C had
referenced by name without a source file are translated too; the
compiler-support memcpy that gcc emits for struct copies has an
arch/m68k definition.  `make` now produces dist/sau2/domain_os, a
906,876-byte flat image, with no unresolved symbols.

Gates on HEAD: 0 build diagnostics, 0 truncated relocations, 55 module
data blocks and 1356 map symbols placed in map order with 0 inversions,
109 of 109 hand-assembly sections identical to the image modulo
relocation fields (every relocated operand verified against the image
operand), the architecture-guard checker clean, 642 host tests passing.

The reviews kept finding real errors in the old C and in the emitters'
first drafts, which is why the second pass stays: wrong prototypes
derived from callers instead of frames (RING_$SVC_IOCTL, RING_$SEND_OS,
RING_$SVC_CLOSE, MST_$INVALIDATE, MST_$GROW_AREA, network_$send_request,
acl_$prim_create_internal with two arguments swapped), an inverted list
insert in AST_$ACTIVATE_ASTE_CANNED, a socket slot off by one in
NETWORK_$DO_REQUEST, a boolean stored as 1 where the image stores 0xFF,
misnamed record fields (the aote directory pointer, the find-uid result),
data cells with wrong initial values, a phantom ACL_$GET_ACL_ATTRIBUTES
that was really a miscalled AST routine, and a KBD state table whose
image indexing runs past its segment (source-kz1g).

Still open: the pre-existing "complete subsystem" P2 epics, which
predate this work and should be re-read against the now-complete tree;
source-ewh0 (TIME_$CLOCKH as the value field of an eventcount);
source-l2p4 (data cells carried in .text.* sections, the assembler's
section-attribute warnings); source-kz1g; the P3/P4 follow-ups filed
during the design work; and the question of booting the image.
