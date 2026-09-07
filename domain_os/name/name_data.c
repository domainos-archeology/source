/*
 * NAME module data - per-process directory-lock state
 *
 * NAME_$LOCK_DIR (0xE54854), NAME_$UNLOCK_DIR (0xE54734) and the DIR_$OLD_*
 * entry points all execute with A5 = 0xE7FD24; every one of those gates
 * begins with `lea (0xe7fd24).l,A5`.  The four tables below are the parts of
 * that module data area that the directory-lock code touches.  They are
 * indexed directly by PROC1_$CURRENT (the original applies no 1-based
 * adjustment) and hold NAME_$MAX_LOCK_PROCS == 58 entries each, which is the
 * count DIR_$OLD_INIT (0xE314F4) clears with `moveq #0x39` + `dbf`.
 *
 * Original m68k addresses (A5 = 0xE7FD24):
 *   NAME_$LOCK_SLOT:   A5+0x03C = 0xE7FD60  [58 x 4 bytes]
 *   NAME_$LOCK_MODE:   A5+0x13E = 0xE7FE62  [58 x 2 bytes]
 *   NAME_$LOCK_HANDLE: A5+0x1BC = 0xE7FEE0  [58 x 4 bytes]
 *   NAME_$LOCK_UID:    A5+0x2B8 = 0xE7FFDC  [58 x 8 bytes]
 *
 * Each table ends before the next one begins, which is how the 58-entry
 * extent was recovered:
 *   0x03C + 58*4 = 0x124 <= 0x13E
 *   0x13E + 58*2 = 0x1B2 <= 0x1BC
 *   0x1BC + 58*4 = 0x2A4 <= 0x2B8
 */

#include "name/name_internal.h"

/* A5+0x03C (0xE7FD60): FILE_$PRIV_LOCK lock-context slot, passed by
 * reference as its lock_ptr_out argument (pea (0x3c,A5,D6w*1) at 0xE548AC). */
uint32_t NAME_$LOCK_SLOT[NAME_$MAX_LOCK_PROCS];

/* A5+0x13E (0xE7FE62): lock mode currently requested/held (0xE54894). */
int16_t NAME_$LOCK_MODE[NAME_$MAX_LOCK_PROCS];

/* A5+0x1BC (0xE7FEE0): mapped base address of the locked directory
 * (cleared at 0xE548A0, stored at 0xE54B02). */
uint32_t NAME_$LOCK_HANDLE[NAME_$MAX_LOCK_PROCS];

/* A5+0x2B8 (0xE7FFDC): UID of the directory this process has locked;
 * zero means "no directory locked" (0xE548EA, cleared by DIR_$OLD_INIT). */
uid_t NAME_$LOCK_UID[NAME_$MAX_LOCK_PROCS];
