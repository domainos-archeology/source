# Agent Instructions

This project's purpose is to analyze and reverse engineer compiled Apollo Workstation code using Ghidra and additional tools. The code that ghidra generates isn't perfect, so we need to do further analysis on it to identify data structures, memory layout, and function purposes.  Ghidra also uses a number of constructs that don't map directly to C/C++ constructs, so we need to identify and convertthose as well.

## DOMAIN_OS KERNEL WORKFLOW

The kernel (domain_os) work should proceed as follows:
1. Function names begin with `<NAMESPACE>_$`.  Functions beginning with `FUN_` are as yet unidentified.  When you identify a function's purpose, rename it to `<NAMESPACE>_$<FUNCTION_NAME>`.
2. When saving functions locally, create a directory structure that matches the namespace.  For example, `CACHE_$CLEAR` would be saved to `domain_os/cache/clear.c`.
3. Identify data structures as you go.  Create header files in the appropriate namespace directory to hold those structures.  For example, if you identify a structure used by `CACHE_$CLEAR`, you might create `domain_os/cache/cache.h` to hold that structure definition.

### File Naming Conventions
- Source files are named after the function, **without** the `<NAMESPACE>_$` prefix, in lowercase: `CAL_$APPLY_LOCAL_OFFSET` → `cal/apply_local_offset.c`.  for functions that don't have a `$` in their name, put them in a subdirectory named `misc`.  so `CRASH_SYSTEM` would go in `misc/crash_system.c`.
- Keep `$` in function names in the C code (e.g., `M$OIU$WLW`, not `M_OIU_WLW`)
- Use relative includes to reference other modules: `#include "../math/math.h"`
- Unit tests go in `<module>/test/test_<function_name>.c`

### Decompilation Guidelines
- The Ghidra decompiler output often needs cleanup - compare against the assembly to verify correctness
- Watch for endianness assumptions: use bit operations (`(*status >> 16) & 0xFF`) instead of byte pointer casts (`((char*)status)[1]`) so code runs correctly on little-endian hosts
- Add TODO comments for known issues (e.g., missing bounds checking) and create corresponding `bd` issues for tracking

IMPORTANT: part of the goal of this work is to make the kernel code retargetable to non-m68k architectures.  We will start with other 32-bit architectures (x86), but we'll need to address that a lot of the code is written with m68k-specific assumptions (such as byte ordering, but also things like syscall conventions).  As you work through the code, please keep that in mind and use #defines, typedefs, and other constructs to isolate architecture-specific code.

ALSO IMPORTANT: the original source for much of the kernel (perhaps all of it except for obvious assembly language portions) was written in Pascal, not C.  You will likely find functions that map to nested pascal subprocedures.  Those will require additional analysis to flatten them into C functions.  Include those as static C functions within the C function they're used within.  Please add comments both to the disassembly and to the generated C files.

MORE IMPORTANT INFO: things may be represented in ghidra as functions but that don't behave like C functions, and quite possibly aren't _called_ like C functions.  My expectation there is that they were actually written in assembly language.  I had converted those labels to functions so that ghidra had some better analysis it could perform, but I they should be generated assembly when emitted to fines.  Given their platform-specific nature, please use the suffix for the SAU type (in this current version of domain_os, that will be sau2) in the filename.  For example, if you identify a function that is actually an assembly language routine for the sau2, name the file `<namespace>/sau2/<function_name>.s`.

AND LASTLY, ALSO IMPORTANT: please add tests you to exercise the code you work on.  I realize that this is difficult without a full OS, but please do your best to create unit tests wherever possible for the non-hairy bits.


## Additional tools at your disposal

This project uses **gsk** (ghidra-skill) to analyze compiled apollo code.

## Quick Reference

```bash
gsk search <SUBSTR>                           # Finds functions with <SUBSTR> in their name
gsk analyze <ADDRESS>                         # Prints information, xrefs, disassembly, and decompilation of the function at <ADDRESS>
gsk rename <ADDRESS> '<NAME>'                 # Renames function at <ADDRESS> to <NAME>
gsk xrefs to <ADDRESS>                        # Lists all callers of the function at <ADDRESS>
gsk xrefs from <ADDRESS>                      # Lists all functions called by <ADDRESS>
gsk comment decompiler <ADDRESS> "<COMMENT>"  # Adds <COMMENT> as a PRE comment at <ADDRESS>
gsk comment disassembly <ADDRESS> "<COMMENT>" # Adds <COMMENT> as a EOL comment at <ADDRESS>
```

For more commands use `gsk --help`


This project uses **bd** (beads) for issue tracking. Run `bd onboard` to get started.

## Quick Reference

```bash
bd ready              # Find available work
bd show <id>          # View issue details
bd update <id> --status in_progress  # Claim work
bd close <id>         # Complete work
bd sync               # Sync with git
```

## Landing the Plane (Session Completion)

**When ending a work session**, you MUST complete ALL steps below. Work is NOT complete until `git push` succeeds.

**MANDATORY WORKFLOW:**

1. **File issues for remaining work** - Create issues for anything that needs follow-up
2. **Run quality gates** (if code changed) - Tests, linters, builds
3. **Update issue status** - Close finished work, update in-progress items
4. **PUSH TO REMOTE** - This is MANDATORY:
   ```bash
   git pull --rebase
   bd sync
   git push
   git status  # MUST show "up to date with origin"
   ```
5. **Clean up** - Clear stashes, prune remote branches
6. **Verify** - All changes committed AND pushed
7. **Hand off** - Provide context for next session

**CRITICAL RULES:**
- Work is NOT complete until `git push` succeeds
- NEVER stop before pushing - that leaves work stranded locally
- NEVER say "ready to push when you are" - YOU must push
- If push fails, resolve and retry until it succeeds
