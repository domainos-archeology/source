---
name: dir-old-unlock-tails
description: The two distinct NAME_$UNLOCK_DIR tail shapes in the DIR_$OLD_*/NAME_$OLD_* routines, plus the per-process lock-state save/restore and the dir_$find_entry path buffer
metadata:
  type: project
---

## NAME_$UNLOCK_DIR (0x00E54734) has TWO tail shapes, not one

**Why:** an audit bead described only one, and blindly applying it inverts the
other.  **How to apply:** read the four instructions after the `bsr` before
writing the C.

- **Tail A - the unlock status wins when it is NONZERO**
  `pea (-d,A6) / bsr / addq #4 / move.l (-d,A6),D1 / beq / move.l D1,(An)`
  Sites: DIR_$OLD_CREATE_DIRU 0x00E57334, DIR_$OLD_ADD_LINKU 0x00E577DA,
  name_$old_add_entry 0x00E56734, name_$old_add_link_local 0x00E5666A.
- **Tail B - the unlock status fills in only a ZERO status low word**
  `pea (-d,A6) / bsr / addq #4 / tst.w (0x2,An) / bne / move.l (-d,A6),(An)`
  Sites: DIR_$OLD_DROP_LINKU 0x00E579A6, DIR_$OLD_CNAMEU 0x00E576D0,
  DIR_$OLD_ADD_BAKU 0x00E57150, name_$old_drop_entry 0x00E56A80,
  DIR_$OLD_READ_LINKU 0x00E5790A.
- **No tail at all** - the unlock reports into a local nobody reads:
  DIR_$OLD_READ_INFOBLK 0x00E56126, DIR_$OLD_WRITE_INFOBLK 0x00E561B6,
  DIR_$OLD_FIX_DIR 0x00E5607E, DIR_$OLD_CLEANUP.
- **Straight into status_ret** (`pea (An)`): DIR_$OLD_DROP_DIRU 0x00E57440,
  DIR_$OLD_FIX_DIR 0x00E55EB0, DIR_$OLD_ADD_BAKU 0x00E56F18 / 0x00E5708A.

Swept 2026-09-08 across every dir/old_*.c and name/old_*.c; only CREATE_DIRU,
DROP_LINKU and CNAMEU were wrong.

## DIR_$OLD_CREATE_DIRU saves the per-process lock state

0x00E5720E-0x00E57246 copies NAME_$LOCK_UID/_HANDLE/_MODE/_SLOT
[PROC1_$CURRENT] into the frame and 0x00E572EE-0x00E57326 puts them back,
because dir_$old_create_obj locks and maps the NEW directory through the same
slots.  Any DIR_$OLD_* routine that creates an object while holding a lock is
worth checking for the same bracket.

## dir_$find_entry's `flags` argument is the path buffer's CAPACITY

0x00E4CAE0 compares (0x12,A6) against the level about to be recorded and calls
CRASH_SYSTEM past it.  The path is 1-BASED: level N is written at
extra+(N-1)*4 as {page_no, entry_idx} (dir_$page_path_t).  dir_$remove_entry
passes 8 and indexes it as `(-0x34,A6,level*4)`, i.e. the cell one element
below the buffer is a SEPARATE local (the AST_$PURIFY segment list), not
level 0.

## Host-testing 32-bit entry/handle words

One mechanism (source-702z retired the handle registry, 2026-09-29):
- a directory handle -> `NAME_$HANDLE_TO_PTR`, which is `ARCH_VA_TO_PTR`;
- a raw 32-bit entry address -> `ARCH_VA_TO_PTR`;
  in both cases the test keeps the target in an arena `ARCH_HOST_VA_BASE`
  (arch/host/arch.h) points at.
A plain `(char *)(uintptr_t)` cast segfaults every host test that dereferences
one.
