/*
 * DIR data storage - Global variables for the directory subsystem
 *
 * These are runtime-initialized data areas used by DIR_$INIT
 * and various directory operations.
 */

#include "dir/dir_internal.h"

/*
 * DIR_$WAIT_ECS - Array of 32 event counters for directory slot waiting
 *
 * Each slot has an associated event counter used to signal
 * completion of pending directory operations.
 */
ec_$eventcount_t DIR_$WAIT_ECS[32];

/*
 * DIR_$WT_FOR_HDNL_EC - Event counter for wait-for-handle
 *
 * Used when all directory handles are in use and a caller
 * must wait for one to become available.
 */
ec_$eventcount_t DIR_$WT_FOR_HDNL_EC;

/*
 * DIR_$MUTEX - Main directory exclusion mutex
 *
 * Protects access to the shared directory slot table and
 * associated data structures.
 */
ml_$exclusion_t DIR_$MUTEX;

/*
 * DIR_$LINK_BUF_MUTEX - Link buffer exclusion mutex
 *
 * Protects access to the shared link name buffer used
 * during directory link operations.
 */
ml_$exclusion_t DIR_$LINK_BUF_MUTEX;

/*
 * DAT_00e52040 - one-page length constant (0x00000400) embedded in the
 * code segment at 0xE52040 and passed by reference to FILE_$FW_PARTIAL and
 * FILE_$TRUNCATE.
 */
const int32_t DAT_00e52040 = 0x00000400;

/*
 * ============================================================================
 * The DIR module block (map: "D E7DBF8 DIR size = 212C", 0x00E7DBF8..0x00E7FD24)
 * ============================================================================
 *
 * DIR_$DO_OP establishes it with `lea (0xe7dc00).l,A5` at 0x00E4C030, so A5 is
 * the block base + 8 and the two tables the code reaches through A5 land at
 * A5+0x1F9C and A5+0x2000.
 */

/*
 * DIR_$OP_PARAMS - 0x00E7FB9C (A5+0x1F9C), the server-side per-operation
 * records DIR_$DO_OP indexes by (opcode >> 1) with an 8-byte stride:
 *   0x00E4C0B6  lea (0x00,A5,D1.l),A0 / move.w (0x1f9c,A0),(0x12,A2)
 *   0x00E4C254  add.w (0x1fa0,A0),D1  / move.w (0x1f9c,A0),(0xa,A3)
 * i.e. +0x00 is the protocol version and +0x04 the reply body size.  The
 * region runs up to DIR_$NAME_OFFSET_TABLE at 0x00E7FC00, which is 0x64 bytes,
 * and the image holds zeroes throughout.
 *
 * TODO(source-wk2f, 0x00E7FB9C): 0x64 bytes is 12.5 records, so either the
 * index is biased or the table shares its tail with the neighbouring counters;
 * the opcode range DIR_$DO_OP admits has not been narrowed far enough to say.
 */
uint16_t DIR_$OP_PARAMS[DIR_$OP_PARAMS_WORDS] = { 0 };

/*
 * DIR_$NAME_OFFSET_TABLE - 0x00E7FC00 (A5+0x2000), eight words, one per
 * directory entry type (the index is always masked with 7).  It ends exactly
 * where the map's next symbol, DIR_$ENTRY_CACHE_TOO_LONG_NAME, begins
 * (0x00E7FC10).  Image words: 0000 0004 0010 0014 000C 0000 0000 0000.
 */
int16_t DIR_$NAME_OFFSET_TABLE[8] = { 0, 4, 16, 20, 12, 0, 0, 0 };

/*
 * The five module globals between DIR_$LK_WAITS (0x00E7FC2C) and DIR_$OP_TAB
 * (0x00E7FC42).  DIR_$INIT clears all of them.  Zero in the image.
 */
void    *DAT_00e7fc30 = NULL;   /* 0x00E7FC30 handle-entry free list head   */
uint32_t DAT_00e7fc34 = 0;      /* 0x00E7FC34                               */
void    *DAT_00e7fc38 = NULL;   /* 0x00E7FC38 request-buffer free list head */
uint32_t DAT_00e7fc3c = 0;      /* 0x00E7FC3C active-slot bitmap, 32 slots  */
uint16_t DAT_00e7fc40 = 0;      /* 0x00E7FC40 link-buffer mutex owner       */

/*
 * DIR_$OP_TAB - 0x00E7FC42, the client-side per-operation table (see the
 * record type in dir/dir_internal.h).  26 records of 8 bytes cover
 * 0x00E7FC42..0x00E7FD12, which is every record the DIR_$<op>U wrappers reach
 * (the highest is DIR_$DROP_MOUNT's at 0x00E7FD0A).  Read with
 * `gsk read 0x00E7FC42 0xE2`.
 *
 * TODO(source-wk2f, 0x00E7FD12): the remaining 0x12 bytes of the DIR segment
 * hold three more words (0001 0000 0001) and the four characters ".bak" at
 * 0x00E7FD20; neither has a tree symbol yet.
 */
dir_$op_tab_entry_t DIR_$OP_TAB[DIR_$OP_TAB_ENTRIES] = {
    { 0x0000, 0x0000, 0x000e, 0x0000 },  /* [ 0] 0x00E7FC42 */
    { 0x0000, 0x0000, 0x000a, 0x0000 },  /* [ 1] 0x00E7FC4A */
    { 0x0000, 0x0000, 0x0004, 0x0008 },  /* [ 2] 0x00E7FC52 */
    { 0x0000, 0x0000, 0x0002, 0x0008 },  /* [ 3] 0x00E7FC5A */
    { 0x0001, 0x0000, 0x0004, 0x0000 },  /* [ 4] 0x00E7FC62 */
    { 0x0001, 0x0000, 0x000a, 0x0008 },  /* [ 5] 0x00E7FC6A */
    { 0x0000, 0x0000, 0x0004, 0x0008 },  /* [ 6] 0x00E7FC72 */
    { 0x0000, 0x0000, 0x0002, 0x0008 },  /* [ 7] 0x00E7FC7A */
    { 0x0000, 0x0000, 0x0002, 0x0000 },  /* [ 8] 0x00E7FC82 */
    { 0x0000, 0x0000, 0x0008, 0x0000 },  /* [ 9] 0x00E7FC8A */
    { 0x0000, 0x0000, 0x0008, 0x000a },  /* [10] 0x00E7FC92 */
    { 0x0000, 0x0000, 0x0002, 0x0008 },  /* [11] 0x00E7FC9A */
    { 0x0000, 0x0001, 0x0012, 0x0010 },  /* [12] 0x00E7FCA2 */
    { 0x0000, 0x0000, 0x0002, 0x000e },  /* [13] 0x00E7FCAA */
    { 0x0000, 0x0000, 0x000a, 0x0006 },  /* [14] 0x00E7FCB2 */
    { 0x0000, 0x0000, 0x0000, 0x0000 },  /* [15] 0x00E7FCBA */
    { 0x0000, 0x0000, 0x0008, 0x0000 },  /* [16] 0x00E7FCC2 */
    { 0x0000, 0x0000, 0x0010, 0x0000 },  /* [17] 0x00E7FCCA */
    { 0x0000, 0x0000, 0x0008, 0x0008 },  /* [18] 0x00E7FCD2 */
    { 0x0000, 0x0000, 0x0002, 0x0000 },  /* [19] 0x00E7FCDA */
    { 0x0000, 0x0000, 0x0036, 0x0000 },  /* [20] 0x00E7FCE2 */
    { 0x0000, 0x0000, 0x003c, 0x0000 },  /* [21] 0x00E7FCEA */
    { 0x0000, 0x0000, 0x003c, 0x0034 },  /* [22] 0x00E7FCF2 */
    { 0x0000, 0x0001, 0x0022, 0x0020 },  /* [23] 0x00E7FCFA */
    { 0x0000, 0x0000, 0x000c, 0x0000 },  /* [24] 0x00E7FD02 */
    { 0x0000, 0x0000, 0x000c, 0x0000 },  /* [25] 0x00E7FD0A */
};
#if defined(ARCH_M68K)
_Static_assert(sizeof(DIR_$OP_TAB) == 0xD0,
               "DIR_$OP_TAB: 0x00E7FC42..0x00E7FD12");
#endif

/*
 * ============================================================================
 * Objects inside the DIR_$MTTAB region (0x00E7F158..0x00E7FC10)
 * ============================================================================
 *
 * The map exports only DIR_$MTTAB at 0x00E7F158; the four objects below are
 * module-local cells inside that span, and their extents come from the
 * addresses the code uses.  All are zero in the image.
 */

/* 0x00E7F280: the directory handle pool, 0x30 bytes per entry.  DIR_$CLEANUP
 * walks it as `&DAT_00e7f280 + i * 0x30` for the 32 slots DAT_00e7fc3c's
 * bitmap covers; the pool ends where DAT_00e7f470 begins. */
uint8_t DAT_00e7f280[0x00E7F470 - 0x00E7F280];

uint32_t DAT_00e7f470 = 0;   /* 0x00E7F470, cleared by DIR_$INIT */
uint32_t DAT_00e7f4b0 = 0;   /* 0x00E7F4B0, cleared by DIR_$INIT */
uint32_t DAT_00e7fbf4 = 0;   /* 0x00E7FBF4, cleared by DIR_$INIT */

/*
 * ============================================================================
 * Literal cells in the DIR / OLD_DIR code regions
 * ============================================================================
 *
 * Domain Pascal passes VAR and const parameters by address, so each literal
 * argument becomes a cell in the code region whose address is pushed with
 * `pea (d,PC)`.  Every one below sits immediately after an `rts`; the image
 * bytes were read with `gsk read`.
 */

/* 0x00E4B230, after the `rts` at 0x00E4B22E.  Bytes 00 0E 00 25. */
status_$t Naming_bad_request_header_ver_err = 0x000E0025;

/* 0x00E4B33C, after the `rts` at 0x00E4B33A.  Bytes 00 00 00 00. */
uint32_t DAT_00e4b33c = 0;

/* 0x00E4B444, after the `rts` at 0x00E4B442.  Bytes 00 01. */
uint16_t DAT_00e4b444 = 1;

/* 0x00E4B448, two filler bytes (20 48) after DAT_00e4b444.
 * Bytes 00 00 80 00. */
uint32_t DAT_00e4b448 = 0x00008000;

/* 0x00E4DFFA / 0x00E4DFFC, after the `rts` at 0x00E4DFF8.
 * Bytes 00 90 | 00. */
int16_t DAT_00e4dffa = 0x0090;
uint8_t DAT_00e4dffc = 0x00;

/* 0x00E50830, after the `rts` at 0x00E5082E.  Bytes 00 05. */
uint16_t DAT_00e50830 = 5;

/* 0x00E50C5A, after the `rts` at 0x00E50C58.  Bytes 00 00. */
uint16_t DAT_00e50c5a = 0;

/* 0x00E54B26, after the `rts` at 0x00E54B24.  Bytes 00 01. */
int16_t ACL_TYPE_DIR = 1;

/* 0x00E56094..0x00E560A4, after the `rts` at 0x00E56092.  Bytes
 * 00 90 | 00 28 | 00 04 | 00 | 00 01 16 00 | 01 | 00 00 00 | 00 | 00 01. */
int16_t  DAT_00e56094 = 0x0090;
int16_t  DAT_00e56096 = 0x0028;
uint16_t DAT_00e56098 = 0x0004;
uint8_t  DAT_00e5609a = 0x00;
uint32_t DAT_00e5609e = 0x00010000;
uint8_t  DAT_00e560a2 = 0x00;

/* 0x00E564E2, a longword in DIR_$OLD_SET_DEFAULT_ACL's pool: the 0x400-byte
 * length handed to FILE_$FW_PARTIAL.  Bytes 00 00 04 00. */
uint32_t DAT_00e564e2 = 0x00000400;

/* 0x00E5716A, after the `rts` at 0x00E57168.  Bytes 00 06. */
uint16_t DAT_00e5716a = 6;

/* 0x00E57CDC, after the `rts` at 0x00E57CDA.  Bytes 00 0E 00 25 - the same
 * status value as Naming_bad_request_header_ver_err, in OLD_DIR's own pool. */
status_$t Bad_request_header_version_err = 0x000E0025;

