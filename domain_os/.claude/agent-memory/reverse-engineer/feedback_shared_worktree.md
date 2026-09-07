---
name: shared-worktree-no-stash
description: Never `git stash` in this repo - other agents edit the same working tree concurrently, and build errors from their in-flight files are not yours
metadata:
  type: feedback
---

Do **not** run `git stash` (or any tree-wide checkout/reset) in
`~/src/domainos-archeology/source`.

**Why:** several reverse-engineering agents work on different subsystems in the
*same* working tree at the same time.  A stash yanks their half-written files
out from under them; a pop can silently conflict.  I did this once to test
whether some `smd/` build errors were pre-existing and it stashed ~115 files
belonging to four other agents.

**How to apply:** to decide whether a `make` error is yours, look at the file
path, not at a clean-tree comparison.  A *path-limited* `git stash push -- <my
subsystems>` does not touch other agents' files, but the before/after error
counts it produces are still worthless: other agents commit and edit between
the two builds, so the two error sets differ for reasons that have nothing to
do with your change (seen 2026-09-06: `file/`, `rem_file/`, `rip/`, `vtoc/` and
`smd/` errors appearing and disappearing across three consecutive full builds).
Judge by file path only.  Build only your own subsystems
(`make -k -j8 tty cal time misc ml ...`) and report errors in other subsystems
as belonging to whoever owns them.  For the test gate, `make -k test` then run
every binary under `build/host/test/` yourself and total the counts, since one
other agent's broken test file stops `make test` before its summary line.

Concurrency also reaches *inside* a subsystem you were told you own: while I
was emitting `fim/init_pid.c` (2026-09-07) the agent working on `proc2/` added
its own `void FIM_$INIT_PID(int16_t *pid);` prototype to `fim/fim.h` to make
its call site compile, so my freshly added prototype collided
("conflicting types") in every host test that includes the header.  **Before
adding a prototype for a function another subsystem already calls, grep the
tree for the name and re-read the header you are about to edit** - and when
there is a conflict, converge on the existing signature and call site instead
of changing theirs.
