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

/* A5+0x000 (0xE7FD24): the "any byte of a leaf" set, see name/name.h. */
uint8_t NAME_$LEAF_CHAR_SET[NAME_$LEAF_SET_SIZE] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x7f, 0xff, 0xff, 0xff, 0xef, 0xff, 0xff, 0xff,
    0xff, 0xff, 0x7f, 0xfe, 0x00, 0x00, 0x00, 0x00
};

/* A5+0x020 (0xE7FD44): the "first byte of a leaf" set, 28 bytes (a
 * `set of chr(32)..chr(255)`).  NAME_$LOCK_SLOT starts right after it at
 * A5+0x3C, so name_$validate_leaf's bound-0xFF index reaches set bytes
 * 28..31 (first byte below 0x20) inside NAME_$LOCK_SLOT[0]. */
uint8_t NAME_$LEAF_FIRST_CHAR_SET[NAME_$LEAF_FIRST_SET_SIZE] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x3f, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0x3f, 0xfe
};

/* A5+0x13E (0xE7FE62): lock mode currently requested/held (0xE54894). */
int16_t NAME_$LOCK_MODE[NAME_$MAX_LOCK_PROCS];

/* A5+0x1BC (0xE7FEE0): mapped base address of the locked directory
 * (cleared at 0xE548A0, stored at 0xE54B02). */
uint32_t NAME_$LOCK_HANDLE[NAME_$MAX_LOCK_PROCS];

/* A5+0x2B8 (0xE7FFDC): UID of the directory this process has locked;
 * zero means "no directory locked" (0xE548EA, cleared by DIR_$OLD_INIT). */
uid_t NAME_$LOCK_UID[NAME_$MAX_LOCK_PROCS];

/*
 * ============================================================================
 * UID_LIST entry owned by NAME
 * ============================================================================
 *
 * NAME_$CANNED_REP_ROOT_UID lives in the shared read-only UID table the map
 * calls "I E1737C UID_LIST size = 210"; its slot is 0x00E173FC, between
 * DISKLESS_$UID (0x00E173F4) and DISPLAY3_$UID (0x00E17404), so the object is
 * 8 bytes.  Image bytes: 00 00 04 04 00 00 00 00.  (NAME_$CANNED_ROOT_UID,
 * the neighbouring 0x00E173E4 slot, is defined in name/init.c.)
 */
uid_t NAME_$CANNED_REP_ROOT_UID = UID_CONST(0x00000404, 0);

/*
 * ============================================================================
 * Literal cells in the NAME / OLD_DIR code regions
 * ============================================================================
 *
 * Domain Pascal passes VAR and const parameters by address, so each literal
 * argument becomes a cell in the code region whose address is pushed.  All
 * three below sit in "I E53EF8 OLD_DIR size = 4308" or "I E58488 NAME
 * size = 5B0"; the map exports no symbol for them, so the Ghidra labels are
 * kept.
 */

/* 0x00E5472E: the word 0, immediately after an `rts` at 0x00E5472C.
 * Image bytes: 00 00. */
int16_t NAME_$CONST_ZERO_W = 0;

/* 0x00E54730: the longword 0, immediately after NAME_$CONST_ZERO_W and
 * immediately before NAME_$UNLOCK_DIR (0x00E54734).  Image bytes:
 * 00 00 00 00. */
uint32_t NAME_$CONST_ZERO_L = 0;


/*
 * Naming_Internal_Err - 0x00E5855C, the status cell NAME_$MAP_DIR pea's to
 * CRASH_SYSTEM at 0x00E5852A.  Image bytes: 00 0E 00 25.
 */
status_$t Naming_Internal_Err = 0x000E0025;

