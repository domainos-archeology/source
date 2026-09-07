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
- `make test`: every host test passes (150 at the start of wave 7, 180 now);
  every test `#include`s the real `.c` it covers.
- relocation overflows at link: 19 before, 0 now.
- archivist rules on changed files: no `extern` in `.c`, first include is the
  subsystem's own header, no prose ellipses, every TODO cites a bead id and an
  address, no cross-subsystem `_internal.h` includes, no foreign-namespace
  declarations (checked map-driven).

## What changed (waves 7 to 15, 53 local commits)

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
| host tests, failures | 180, 0 |
| undefined link references (all functions now) | 366, from 1212 |
| relocation overflows | 0, from 19 |
| hand-written assembly files under `*/sau2/` | 41 |
| beads closed in this pass | about 50 |

## What is left

Fidelity backlog: only `source-4omb` (two unknown bytes in ring_info_t) and
the four small follow-ups of the final wave if any stay open.

Blocked on user review: `docs/design-per-process-data.md` (bead `source-0i3`,
P1) and its seven step beads. Nothing in this pass pre-empted its
`.moddata.<name>` scheme; image-adjacent code constants use `.text.<name>`
sections listed in `sau2.ld` in image order.

Deferred by instruction: the "Complete/Implement/Emit remaining <subsystem>"
translation beads (about 35). The 366 undefined references are that work.

## How to keep the footing

- Before naming anything, grep the SAU2 map for the address; the map wins.
- Before inventing a status name, query the status database.
- Before a hardware register or on-disk record name, search
  `docs/apollo-docs/` (Engineering Handbook for I/O, AEGIS Internals for disk).
- Every recovered layout gets `_Static_assert`s; every claim gets the
  instruction address in a comment and in Ghidra.
- Never commit an agent's work on its own report: rerun the gates, spot-check
  the riskiest claims with `gsk`, then commit.
