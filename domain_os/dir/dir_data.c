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
 * 0x00E7FB9C (A5+0x1F9C) is not an object: it is the +0x02 word of the
 * DIR_$OP_TAB record for (opcode >> 1), reached through a virtual table base
 * of 0x00E7FB9A that sits 21 records below DIR_$OP_TAB (see the record
 * comment in dir/dir_internal.h).  For the opcodes DIR_$DO_OP admits,
 * 0x2A..0x5C, the addresses it forms run 0x00E7FC44..0x00E7FD10, all inside
 * DIR_$OP_TAB; the bytes 0x00E7FB9A..0x00E7FC42 that records 0..20 would
 * occupy hold DIR_$NAME_OFFSET_TABLE and the DIR globals below.
 */

/*
 * DIR_$NAME_OFFSET_TABLE - 0x00E7FC00 (A5+0x2000), eight words, one per
 * directory entry type (the index is always masked with 7).  It ends exactly
 * where the map's next symbol, DIR_$ENTRY_CACHE_TOO_LONG_NAME, begins
 * (0x00E7FC10).  Image words: 0000 0004 0010 0014 000C 0000 0000 0000.
 */
int16_t DIR_$NAME_OFFSET_TABLE[8] = { 0, 4, 16, 20, 12, 0, 0, 0 };

/*
 * The five module globals between DIR_$LK_WAITS (0x00E7FC2C) and DIR_$OP_TAB
 * (0x00E7FC42) are fields of the A5 block, not separate objects, and are
 * reached through the DIR_$LOCK_FREE / DIR_$LOCK_IN_USE / DIR_$HANDLE_FREE /
 * DIR_$HANDLE_IN_USE / DIR_$LINK_BUF_OWNER accessors in dir/dir_internal.h:
 *
 *   0x00E7FC30 (A5+0x2030)  head of the dir_$lock_entry_t free list
 *   0x00E7FC34 (A5+0x2034)  lock-entry in-use bitmap  (DIR_$LOCK_OBJ)
 *   0x00E7FC38 (A5+0x2038)  head of the dir_$handle_t free list
 *   0x00E7FC3C (A5+0x203C)  handle in-use bitmap      (DIR_$ALLOC_HANDLE)
 *   0x00E7FC40 (A5+0x2040)  DIR_$LINK_BUF_MUTEX owner (dir_$do_op_cname)
 *
 * All are zero in the image; DIR_$INIT (0x00E3140C) fills the first four in.
 */

/*
 * DIR_$OP_TAB - 0x00E7FC42, the per-operation parameter table (see the record
 * type and the 21-record bias in dir/dir_internal.h).  26 records of 8 bytes
 * cover 0x00E7FC42..0x00E7FD12, i.e. records 21..46 = opcodes 0x2A..0x5C, the
 * exact range DIR_$DO_OP's jump table admits and every record the DIR_$<op>U
 * wrappers reach (the highest is DIR_$DROP_MOUNT's at 0x00E7FD0A).  Read with
 * `gsk read 0x00E7FC42 0xD0`.
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
 * The last 0x12 bytes of the DIR segment, 0x00E7FD12..0x00E7FD24 (the map has
 * "D E7DBF8 DIR size = 212C", and OLD_DIR's A5 base is the very next byte:
 * `lea (0xe7fd24).l,A5` in every OLD_DIR routine, e.g. 0x00E54B30).
 */

/*
 * 0x00E7FD12: two bytes of zero fill between DIR_$OP_TAB's last record and
 * the longword constants below, which the compiler placed on a longword
 * boundary.  Nothing reads it.
 */
uint16_t DAT_00e7fd12 = 0x0000;

/*
 * 0x00E7FD14 / 0x00E7FD18 / 0x00E7FD1C: the three directory-read continuation
 * cookies dir_$do_op_dir_readu compares and stores as whole longwords through
 * A5 = 0x00E7DC00 (`cmp.l (0x211c,A5),D0` at 0x00E4DA00, 0x00E4DA50 and
 * 0x00E4DA60; `move.l (0x2114,A5),(A3)` at 0x00E4DA06, 0x00E4DA3E, 0x00E4DB50;
 * `cmp.l`/`move.l (0x2118,A5)` at 0x00E4DA22, 0x00E4DA56, 0x00E4DAE0).
 *
 *   0x00E7FD1C  the "." pseudo-entry     (a 1-character name of dots)
 *   0x00E7FD18  the ".." pseudo-entry    (2 characters; 0x00E4DA1C compares
 *                                         the name byte against '.' = 0x2E)
 *   0x00E7FD14  the first real entry, the value the cursor moves to once both
 *               pseudo-entries have been emitted (0x00E4DA26 loop exit)
 */
uint32_t DAT_00e7fd14 = 0x00020001;
uint32_t DAT_00e7fd18 = 0x00010001;
uint32_t DAT_00e7fd1c = 0x00000001;

/*
 * 0x00E7FD20: the four characters ".bak".  No instruction in the image reads
 * it -- there is no absolute reference to 0x00E7FD20 anywhere, and no DIR or
 * OLD_DIR routine forms (0x2120,A5) or (-0x4,A5) -- so it is a constant the
 * Pascal source declared whose only use was compiled away.
 */
char DAT_00e7fd20[4] = { '.', 'b', 'a', 'k' };

/*
 * ============================================================================
 * Objects inside the DIR_$MTTAB region (0x00E7F158..0x00E7FC10)
 * ============================================================================
 *
 * The map exports only DIR_$MTTAB at 0x00E7F158; the four objects below are
 * module-local cells inside that span, and their extents come from the
 * addresses the code uses.  All are zero in the image.
 */

/*
 * 0x00E7F280 (A5+0x1680) is dir_$lock_entry_t[32] and 0x00E7F480 (A5+0x1880)
 * is dir_$handle_t[32]; both are declared in dir/dir_internal.h and reached
 * as DIR_$LOCK_TAB / DIR_$HANDLE_TAB.  The three cells the tree used to call
 * DAT_00e7f470 / DAT_00e7f4b0 / DAT_00e7fbf4 are interior `next` fields of
 * those arrays, which is why DIR_$INIT clears them one by one after building
 * the chains:
 *
 *   0x00E7F470 = A5+0x1870 = DIR_$LOCK_TAB[31].u.next    (0x00E3149E)
 *   0x00E7F4B0 = A5+0x18B0 = DIR_$HANDLE_TAB[0].next     (0x00E314A6)
 *   0x00E7FBF4 = A5+0x1FF4 = DIR_$HANDLE_TAB[31].next    (0x00E314A2)
 *
 * and 0x00E7F4BC = A5+0x18BC = &DIR_$HANDLE_TAB[1], the value DIR_$INIT
 * stores as the handle free-list head, not a pool of its own.
 */

/*
 * 0x00E7DBFC (A5-0x4): the module block's pointer to the status constant the
 * DIR CRASH_SYSTEM sites push (`move.l (-0x4,A5),-(SP)`).  Image bytes at
 * 0x00E7DBF8: `00 e4 b3 3c 00 e4 b2 30`.
 */
status_$t *const DIR_$CRASH_STATUS = &Naming_bad_request_header_ver_err;

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
uint32_t DIR_$CONST_ZERO_L = 0;

/* 0x00E4B444, after the `rts` at 0x00E4B442.  Bytes 00 01. */
uint16_t DIR_$CONST_ONE_W = 1;

/* 0x00E4BC24, byte 0xFF (followed by a 0x00 filler byte).  ACL_$RIGHTS'
 * `ignore_super` argument - TRUE, i.e. the super-user bypass is suppressed.
 * Every DIR call site reaches this ONE cell with `pea (d,PC)`:
 * dir_$open_dir 0x00E4BA88, dir_$do_op_add_bak 0x00E509E2 / 0x00E50A9E,
 * dir_$do_op_cname, dir_$do_op_drop_dir, dir_$get_entry_cached. */
boolean DIR_$CONST_TRUE_B = true;

/* 0x00E4B448, two filler bytes (20 48) after DIR_$CONST_ONE_W.
 * Bytes 00 00 80 00. */
uint32_t DAT_00e4b448 = 0x00008000;

/* 0x00E4DFFA / 0x00E4DFFC, after the `rts` at 0x00E4DFF8.
 * Bytes 00 90 | 00. */
int16_t DIR_$READU_ATTR_SIZE = 0x0090;
uint8_t DIR_$READU_NUL_NAME = 0x00;

/* 0x00E50830, after the `rts` at 0x00E5082E.  Bytes 00 05. */
uint16_t DAT_00e50830 = 5;

/* 0x00E50C5A, after the `rts` at 0x00E50C58.  Bytes 00 00. */
uint16_t DAT_00e50c5a = 0;

/* 0x00E54B26, after the `rts` at 0x00E54B24.  Bytes 00 01. */
int16_t ACL_TYPE_DIR = 1;

/* 0x00E56094..0x00E560A4, after the `rts` at 0x00E56092.  Bytes
 * 00 90 | 00 28 | 00 04 | 00 | 00 01 16 00 | 01 | 00 00 00 | 00 | 00 01. */
int16_t  DIR_$ATTR_REC_SIZE_W = 0x0090;
int16_t  DIR_$INFOBLK_MAX_LEN = 0x0028;
uint16_t DAT_00e56098 = 0x0004;
uint8_t  DAT_00e5609a = 0x00;
uint32_t DAT_00e5609e = 0x00010000;
uint8_t  DAT_00e560a2 = 0x00;

/* 0x00E564E2, a longword in DIR_$OLD_SET_DEFAULT_ACL's pool: the 0x400-byte
 * length handed to FILE_$FW_PARTIAL.  Bytes 00 00 04 00. */
uint32_t DAT_00e564e2 = 0x00000400;

/* 0x00E5716A, after the `rts` at 0x00E57168.  Bytes 00 06. */
uint16_t DIR_$PROT_TYPE_ACL = 6;

/* 0x00E57CDC, after the `rts` at 0x00E57CDA.  Bytes 00 0E 00 25 - the same
 * status value as Naming_bad_request_header_ver_err, in OLD_DIR's own pool. */
status_$t Bad_request_header_version_err = 0x000E0025;

