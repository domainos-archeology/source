---
name: gate-scripts-header-hygiene
description: The two greps that police header hygiene in domain_os, and the traps that make naive versions of them lie.
metadata:
  type: project
---

Two checks keep the header layout honest after a move pass:

**1. Foreign declarations.** Every `<sub>/*.h` line that declares a
`NAME$SYM` whose namespace is not `<sub>`.  A pure-grep version is unusable:
it drowns in `status_$t` / `ec_$eventcount_t` parameter types and in
legitimate sub-namespaces (`EC2_$` in `ec/`, `VTOCE_$` in `vtoc/`,
`XNS_IDP_$` in `xns/`, `PTR_FOO_$` aliases).  Match only *declaration* lines
(leading `extern` or a type token), strip a `PTR_` prefix, accept prefix ==
dir or either being a prefix of the other, then settle the rest with the SAU2
map segment - see [[header-ownership-rule]].

**2. Cross-subsystem `_internal.h` includes.** `grep -rn '#include
".*/.*_internal.h"'` filtered to lines where the including file's directory
differs from the include's.  Each hit means a declaration is in the wrong
header; find what the borrower actually uses by diffing the identifiers
declared in `<owner>_internal.h` against those in `<owner>.h`.

**Traps:**
- `ugrep` (what `grep` resolves to here) mishandles `\$` inside an `-E`
  alternation, so shell one-liners silently report false positives.  Do the
  scan in Python.
- Removing a cross-subsystem internal include also removes everything that
  header pulled in transitively.  `make` alone will not show it - the objects
  are stale.  Always `make clean && make -k -j8` after this kind of edit;
  audit/ lost `uid/uid.h`, dir/ lost `hint/hint.h` and proc2/ lost
  `time/time.h` this way, all via `file/file_internal.h`.
- Host tests that `#include "../foo.c"` inherit the same transitive loss but
  only fail under `make test`.
