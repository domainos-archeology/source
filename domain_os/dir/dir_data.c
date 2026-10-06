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
 * DIR_$ONE_PAGE_L - 0x00E52040, the longword 0x00000400 (one page) in the
 * DIR code region.  Four `pea (d,PC)` sites read it (0x00E52026,
 * 0x00E52532, 0x00E52F3A and 0x00E53D2E), handing it over as
 * FILE_$FW_PARTIAL's byte count and FILE_$TRUNCATE's length.  Image bytes:
 * 00 00 04 00.  (Ghidra labelled the cell by its address, 0x00E52040.)
 */
const int32_t DIR_$ONE_PAGE_L = 0x00000400;

/*
 * DIR_$OLD_LINK_TEXT_MAX - the word 0x0100 at 0xE577F2, shared by
 * DIR_$OLD_ADD_LINKU (MAP_CASE) and DIR_$OLD_READ_LINKU (UNMAP_CASE) as the
 * output-buffer size.  See dir/dir_internal.h.
 */
int16_t DIR_$OLD_LINK_TEXT_MAX = 0x0100;

/*
 * ============================================================================
 * DIR_$DATA - the DIR segment (map: "D E7DBF8 DIR size = 212C",
 * 0x00E7DBF8..0x00E7FD23)
 * ============================================================================
 *
 * Module data block DIR_$DATA: Claude Opus 5.5 (source-qiby).  Layout,
 * biases and asserts in dir/dir_internal.h; DIR routines load A5 =
 * 0x00E7DC00, the block + 8.  The image's non-zero bytes
 * (`gsk read 0xE7DBF8 0x212C`) are exactly the cells initialised below:
 *
 *   00e7dbf8  00 e4 b3 3c 00 e4 b2 30                          A5-0x8 / A5-0x4
 *   00e7fc00  00 00 00 04 00 10 00 14  00 0c 00 00 00 00 00 00  NAME_OFFSET_TABLE
 *   00e7fc42  .. DIR_$OP_TAB, 26 records (read with `gsk read 0xE7FC42 0xD0`)
 *   00e7fd14  00 02 00 01 00 01 00 01  00 00 00 01 2e 62 61 6b  cookies, ".bak"
 *
 * Everything else is zero: the link buffer (A5+0x000), the entry cache
 * (A5+0x400), DIR_$MTTAB and its tables (A5+0x1558), the per-process lock
 * flags (A5+0x15FE), the lock and handle tables (A5+0x1680 / A5+0x1880;
 * DIR_$INIT builds them), the DIR_$ENTRY_CACHE_* and DIR_$HNDL_WAITS /
 * DIR_$LK_* counters, the free-list heads and in-use bitmaps and the link
 * buffer owner (A5+0x2010..0x2041), and the pad word at A5+0x2112.
 *
 * The three cells the tree used to call 0x00E7F470 / 0x00E7F4B0 /
 * 0x00E7FBF4 are interior `next' fields of the two tables, which is why
 * DIR_$INIT clears them one by one after building the chains:
 *
 *   0x00E7F470 = A5+0x1870 = lock_tab[31].u.next    (0x00E3149E)
 *   0x00E7F4B0 = A5+0x18B0 = handle_tab[0].next     (0x00E314A6)
 *   0x00E7FBF4 = A5+0x1FF4 = handle_tab[31].next    (0x00E314A2)
 *
 * and 0x00E7F4BC = A5+0x18BC = &handle_tab[1], the value DIR_$INIT stores
 * as the handle free-list head, not a pool of its own.
 *
 * DIR_$OP_TAB (0x00E7FC42, A5+0x2042) is the per-operation parameter table:
 * 26 records of 8 bytes, records 21..46 = opcodes 0x2A..0x5C, the exact
 * range DIR_$DO_OP's jump table admits and every record the DIR_$<op>U
 * wrappers reach (the highest is DIR_$DROP_MOUNT's at 0x00E7FD0A); the
 * 21-record bias is DIR_$OP_REC's.  The two pointer cells hold the VAs of
 * code-region constants, so they are link-time addresses on the target
 * (ARCH_PTR_TO_VA_STATIC) and the image's values on the host.
 */
MODULE_DATA_DEFINE_INIT(dir_$data_t, DIR_$DATA, 0x00E7DBF8, {
    /* 0x00E7DBF8 (A5-0x8): AST_$PURIFY's segment-list argument */
    .purify_seg_list = ARCH_PTR_TO_VA_STATIC(&DIR_$CONST_ZERO_L, 0x00E4B33C),
    /* 0x00E7DBFC (A5-0x4): what every DIR CRASH_SYSTEM site pushes */
    .crash_status = ARCH_PTR_TO_VA_STATIC(&Naming_bad_request_header_ver_err,
                                          0x00E4B230),
    /* 0x00E7FC00: DIR_$NAME_OFFSET_TABLE, 0000 0004 0010 0014 000C 0 0 0 */
    .name_offset_table = { 0, 4, 16, 20, 12, 0, 0, 0 },
    .op_tab = {
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
    },
    /*
     * 0x00E7FD14 / 0x00E7FD18 / 0x00E7FD1C: the directory-read continuation
     * cookies dir_$do_op_dir_readu compares and stores as whole longwords
     * (`cmp.l (0x211c,A5),D0` at 0x00E4DA00, 0x00E4DA50 and 0x00E4DA60;
     * `move.l (0x2114,A5),(A3)` at 0x00E4DA06, 0x00E4DA3E, 0x00E4DB50;
     * `cmp.l`/`move.l (0x2118,A5)` at 0x00E4DA22, 0x00E4DA56, 0x00E4DAE0):
     * the first real entry, ".." and ".".
     */
    .readu_cookie_first  = 0x00020001,
    .readu_cookie_dotdot = 0x00010001,
    .readu_cookie_dot    = 0x00000001,
    /* 0x00E7FD20: ".bak", copied 1-based by dir_$do_op_add_bak */
    .bak_suffix = { '.', 'b', 'a', 'k' },
});

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

/* 0x00E515BA, after the `rts' at 0x00E515B8.  Bytes 00 04.  Shared by
 * dir_$do_op_delete, dir_$do_op_set_acl and dir_$write_def_prot. */
const uint16_t DIR_$CONST_FOUR_W = 4;

/* 0x00E4BC24, byte 0xFF (followed by a 0x00 filler byte).  ACL_$RIGHTS'
 * `ignore_super` argument - TRUE, i.e. the super-user bypass is suppressed.
 * Every DIR call site reaches this ONE cell with `pea (d,PC)`:
 * dir_$open_dir 0x00E4BA88, dir_$do_op_add_bak 0x00E509E2 / 0x00E50A9E,
 * dir_$do_op_cname, dir_$do_op_drop_dir, dir_$get_entry_cached. */
boolean DIR_$CONST_TRUE_B = true;

/* 0x00E4B448 (bytes 00 00 80 00, one cache group's size) follows
 * DIR_$CONST_ONE_W.  dir_$map_page is its only reader, so it lives there as
 * a file static (source-ka0m). */

/* 0x00E4DFFA / 0x00E4DFFC, after the `rts` at 0x00E4DFF8.
 * Bytes 00 90 | 00. */
int16_t DIR_$READU_ATTR_SIZE = 0x0090;
uint8_t DIR_$READU_NUL_NAME = 0x00;

/* 0x00E50830 (bytes 00 05) is FILE_$SET_PROT's prot_type for
 * dir_$add_bak_default_prot and 0x00E50C5A (bytes 00 00) is
 * DIR_$DROP_HARD_LINKU's flags word for DIR_$DROPU.  Both are single-reader
 * code-region cells and now live as file statics next to their call sites,
 * dir/add_bak_default_prot.c and dir/dropu.c (source-p25p, source-ka0m). */

/* 0x00E54B26, after the `rts` at 0x00E54B24.  Bytes 00 01. */
int16_t ACL_TYPE_DIR = 1;

/*
 * 0x00E56094..0x00E560A5, after the `rts` at 0x00E56092.  Image bytes:
 *   00 90 | 00 28 | 00 04 | 00 00 01 16 | 00 01 00 00 | 00 00 00 01
 * Only the first two words have more than one reader, so only they are
 * module globals; 0x00E56098, 0x00E5609A, 0x00E5609E and 0x00E560A2 are
 * file statics in dir/old_fix_dir.c, its sole reader (source-ka0m).
 */
int16_t  DIR_$ATTR_REC_SIZE_W = 0x0090;
int16_t  DIR_$INFOBLK_MAX_LEN = 0x0028;

/* 0x00E564E2, a longword in DIR_$OLD_SET_DEFAULT_ACL's pool: the 0x400-byte
 * length handed to FILE_$FW_PARTIAL.  Bytes 00 00 04 00. */
uint32_t DIR_$SET_DEF_ACL_FLUSH_LEN = 0x00000400;

/* 0x00E5716A, after the `rts` at 0x00E57168.  Bytes 00 06. */
uint16_t DIR_$PROT_TYPE_ACL = 6;

/* 0x00E57CDC, after the `rts` at 0x00E57CDA.  Bytes 00 0E 00 25 - the same
 * status value as Naming_bad_request_header_ver_err, in OLD_DIR's own pool. */
status_$t Bad_request_header_version_err = 0x000E0025;


/*
 * DIR_SERVER (0x00E801E4, map size 0x80): the remote directory server's
 * saved identity.  Image: the default project record 0x0000000C x 3 at
 * +0x00 and first_time = 0xFF at +0x7C; everything else zero.
 */
MODULE_DATA_DEFINE_INIT(dir_$server_data_t, DIR_SERVER, 0x00E801E4, {
    .default_proj = { 0x0000000C, 0x0000000C, 0x0000000C },
    .first_time = (int8_t)0xFF,
});
