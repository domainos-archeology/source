---
name: ghidra-name-sync-workflow
description: How to diff the whole C tree against the Ghidra symbol table in one pass, and the gsk quirks (no list-all, label vs function symbols, shell-escaped names) that bite when doing it
metadata:
  type: project
---

Recovered while closing source-dia2 (bring Ghidra names in line with the tree).

**Why:** the audit's failure mode #14 was "Ghidra out of sync"; doing that check
one function at a time is hopeless at 1800+ symbols, and three gsk quirks silently
produce wrong answers.

**How to apply:**

## Getting the full symbol table

`gsk search` has no list-all and requires a non-empty query. Union of single-character
searches covers everything (every symbol contains at least one of these):

```sh
for c in a b c ... z 0 ... 9 '$' '_'; do gsk search "$c" -l 100000 >> all.txt; done
sort -u all.txt      # ~1870 rows of "<addr>\t<name>"
```

Then diff against the tree's own names:

```sh
grep -rhoE '^[a-zA-Z_][a-zA-Z_0-9 \*]*[ \*]([A-Za-z_0-9]+\$[A-Za-z_0-9\$]*)[ ]*\(' \
     --include='*.c' --include='*.h' . | grep -oE '[A-Za-z_0-9]+\$[A-Za-z_0-9\$]*'
```
plus `.globl` lines from `*/sau2/*.s`. `comm -23` the two sorted lists. Names that
come back are either (a) statics/inlines with no binary counterpart, (b) flattening
artifacts, or (c) the real work. Separate them by grepping each name's declaration
for a nearby "Original address:" line, then look that address up.

For the reverse direction, grep the tree for every `FUN_00e…` / `DAT_00e…` /
`thunk_` token and re-resolve each address after your renames — that finds the
stale comments.

## gsk quirks

- `gsk label list --address <a>` returns **nothing** for a function entry; the
  function symbol is separate. Use the search dump (or `gsk analyze | sed -n 2p`)
  to read a function's name, and `label list` only for data.
- `gsk label add` on an address whose only label is the auto-generated `DAT_…`
  **replaces** it. On an address that already has a *function*, it adds a second
  symbol — and `gsk rename` then fails with "A symbol named X already exists at
  this address". Fix: `gsk label delete <addr> <name>` first, then `gsk rename`.
- Shell-quoting bugs leak into the database: 0x00E27036 was literally named
  `smd_\$disp1_setup_blt`. After any bulk rename session, `grep '\\' all.txt`.
- `gsk xrefs from <function>` returned nothing for OS_$INIT; to enumerate callees,
  pull the assembly and regex the `jsr`/`bsr` targets instead.

## Judging a rename

Argument order is the cheap check: the FIRST declared parameter sits at `(0x8,A6)`
and offsets grow with declaration order (bytes still occupy an even 2-byte slot).
Confirm arity against the tree's prototype before renaming; when the arity does not
match, the address identity may still be right — rename it and file a bead for the
prototype rather than leaving `FUN_`.

Do **not** churn case-only differences (Ghidra `STOP_$HOOK` vs C `stop_$hook`): the
tree deliberately uses lowercase `ns_$name` for internal helpers and documents the
uppercase original in the comment. Related: [[feedback-shared-worktree]],
[[project-audit-2026-09-06]].
