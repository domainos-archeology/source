# Agent Instructions

This project's purpose is to analyze and reverse engineer obsolete compiled Apollo Workstation code, for purposes of digital preservation and education, using Ghidra and additional tools. The code that ghidra generates isn't perfect, so we need to do further analysis on it to identify data structures, memory layout, and function purposes.  Ghidra also uses a number of constructs that don't map directly to C/C++ constructs, so we need to identify and convertthose as well.

The generated C code should map as closely to the semantics in the machine code as possible.  The data structures and their layout should be identical.  Do NOT get creative or think of optimizations while doing your work.  Your work is that of an archivist.  You should be as faithful as you can to the original.  Whenever you have a question or think of a way to fix the code, you MUST MAKE SURE the resulting code maps to the same behavior.

## DOMAIN_OS KERNEL WORKFLOW

The kernel (domain_os) work should proceed as follows:
1. Function names begin with `<NAMESPACE>_$`.  Functions beginning with `FUN_` are as yet unidentified.  When you identify a function's purpose, rename it to `<NAMESPACE>_$<FUNCTION_NAME>`.
2. When saving functions locally, create a directory structure that matches the namespace.  For example, `CACHE_$CLEAR` would be saved to `domain_os/cache/clear.c`.
3. Identify data structures as you go.  Create header files in the appropriate namespace directory to hold those structures.  For example, if you identify a structure used by `CACHE_$CLEAR`, you might create `domain_os/cache/cache.h` to hold that structure definition.

### File Naming Conventions
- Source files are named after the function, **without** the `<NAMESPACE>_$` prefix, in lowercase: `CAL_$APPLY_LOCAL_OFFSET` → `cal/apply_local_offset.c`.  for functions that don't have a `$` in their name, put them in a subdirectory named `misc`.  so `CRASH_SYSTEM` would go in `misc/crash_system.c`.
- Keep `$` in function names in the C code (e.g., `M$OIU$WLW`, not `M_OIU_WLW`)
- Use relative includes to reference other modules, rooted at the domain_os/ dir: `#include "math/math.h"`
- Unit tests go in `<module>/test/test_<function_name>.c`.  Each test is a self-contained host program: it `#include`s the `.c` under test directly (e.g. `#include "../read_cal.c"`), carries its own tiny TEST/RUN_TEST/ASSERT_EQ helpers, and is built and run by `make test` with `-DARCH_HOST`.
- Add files and include paths to the Makefile as needed.

### Ghidra Synchronization
**When you determine the actual name of a function**, you MUST update Ghidra immediately:
1. Use `gsk rename <ADDRESS> '<NAME>'` to rename the function in Ghidra
2. Update any parameter names/types as needed
3. Then add the function to the appropriate subsystem directory

Do NOT use `#define FUN_XXXXXXXX actual_name` patterns. These are temporary workarounds that should be corrected by renaming in Ghidra directly.

### TODOs in the code

When at all possible, do NOT leave work undone.  The unit of work is the function - finish the function.  If something is unclear or if you need further analysis to clean up the code, leave a TODO: comment to make them easy to see.  Also add a bead task (low priority) to reference the TODO.

### Decompilation Guidelines
- The Ghidra decompiler output often needs cleanup - compare against the assembly to verify correctness
- **Always verify that emitted C code mirrors the behavior shown in Ghidra assembly.** Check parameter counts by examining stack offsets after register saves, and verify how parameters are accessed (full values vs individual bytes/words). Ghidra's decompiler sometimes gets parameter counts wrong.
- Watch for endianness assumptions: use bit operations (`(*status >> 16) & 0xFF`) instead of byte pointer casts (`((char*)status)[1]`) so code runs correctly on little-endian hosts
- Add TODO comments for known issues (e.g., missing bounds checking) and create corresponding `bd` issues for tracking

### Header File Organization
**NEVER use `extern` declarations in .c files.** All external references must come from proper header files.

**Public headers (`<subsystem>/<subsystem>.h`):**
- Contains types, constants, and function prototypes needed by OTHER subsystems
- Example: `time/time.h` exports `TIME_$CLOCK()`, `clock_t`, etc.
- Include with: `#include "time/time.h"`

**Internal headers (`<subsystem>/<subsystem>_internal.h`):**
- Contains internal types, constants, and static helper prototypes used ONLY within the subsystem
- Should `#include "<subsystem>/<subsystem>.h"` at the top
- All .c files in the subsystem should `#include "<subsystem>/<subsystem>_internal.h"`
- Example: `time/time_internal.h` contains `time_$q_insert_sorted()`, internal data structures

**Cross-subsystem dependencies:**
- If subsystem A needs a function from subsystem B, and B's header doesn't exist yet, CREATE IT
- Example: `time/` needs `ML_$SPIN_LOCK()` → create `ml/ml.h` with the prototype
- Don't worry about the implementation; add stubs or leave unimplemented
- This ensures all dependencies are explicit and traceable

**Example structure:**
```
time/
  time.h              # Public API - exported to other subsystems
  time_internal.h     # Internal API - includes time.h
  time_data.c         # #include "time_internal.h"
  clock.c             # #include "time_internal.h"
  ...
```

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
gsk xrefs to <ADDRESS>                        # Lists all callers of the function at <ADDRESS>/accessors of data at <ADDRESS>.
gsk xrefs from <ADDRESS>                      # Lists all functions called by <ADDRESS>
gsk comment decompiler <ADDRESS> "<COMMENT>"  # Adds <COMMENT> as a PRE comment at <ADDRESS>
gsk comment disassembly <ADDRESS> "<COMMENT>" # Adds <COMMENT> as a EOL comment at <ADDRESS>
gsk read <ADDRESS>                            # Prints hex output for data at ADDRESS (default length = 256)
gsk read <ADDRESS> <LENGTH>                   # Prints hex output for data at ADDRESS (length LENGTH)
gsk data get <ADDRESS>                        # returns the type of data at <ADDRESS>
gsk data set <ADDRESS> <TYPE>                 # sets the type of data found at <ADDRESS> to <TYPE>
gsk label list                                # List all labels
gsk label list --address <ADDRESS>            # Labels at specific address <ADDRESS>
gsk label add <ADDRESS> <LABEL>               # Add global label <LABEL> at address <ADDRESS>
gsk label add <ADDRESS> <LABEL --local        # Add function-scoped label <LABEL> at address <ADDRESS>
gsk label delete <ADDRESS> <LABEL>            # Remove label <LABEL> at address <ADDRESS>

```

For more commands use `gsk --help`

### The SAU2 link map (names win over guesses)

`~/src/domainos-archeology/sau2-maps/domain_os.10.2.map` is the linker map of
the **exact** image loaded in Ghidra (SR10.2, SAU2 - anchors: `FILE_$LOCK_INIT`
at `E32744`, `FIM_$GET_USER_PC` at `E0AAA6`).  It lists code *and* data symbols
in address order, with per-segment sizes:

```
I    E0A458  FIM_               size = 664
     E0A458  FIM_$BUILD_DF
D71  E935CC  FILE_$LOT_DATA     loaded at 1A18E6, size = 1086C
```

**Its names are authoritative.**  Before inventing a descriptive name for a
function or data cell, grep this map for the address; if it has a symbol there,
use that name in Ghidra *and* in the C tree.  Where the SR10.4 maps
(`~/src/domainos-archeology/sr10.4-install/sau*/domain_os.map`) disagree with
this one, the SAU2 map wins - they are different builds and are only good for
ordering.  A segment size is also authoritative for the size of the object it
holds, so quote it (as a comment or `_Static_assert`) next to the C definition.

Two things the map does *not* settle: symbols with no `$` (e.g. `CHKSUM`,
`TESTPAGE`, `NULL_LOOP`) are module-local and may keep a more descriptive tree
name, and a segment with no interior symbols is a real negative rather than a
gap - it means the object genuinely exports nothing.


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

**When ending a work session**, complete ALL steps below. Work is committed locally; do NOT push (the owner pushes manually).

**MANDATORY WORKFLOW:**

1. **File issues for remaining work** - Create issues for anything that needs follow-up
2. **Run quality gates** (if code changed) - in `domain_os/`: `make clean && make` (m68k build from a clean tree, `-Werror`; only unresolved-symbol link errors are expected - incremental builds have hidden prototype mismatches across headers) and `make test` (host build + run of every `<subsystem>/test/test_*.c`)
3. **Update issue status** - Close finished work, update in-progress items
4. **COMMIT LOCALLY** - This is MANDATORY:
   ```bash
   bd sync
   git add <files>
   git commit
   git status  # MUST show a clean tree
   ```
5. **Clean up** - Clear stashes
6. **Verify** - All changes committed
7. **Hand off** - Provide context for next session

**CRITICAL RULES:**
- Work is NOT complete until it is committed
- Do NOT `git push`; local commits are fine
- NEVER leave work uncommitted in the working tree
