/*
 * DIR - Directory Operations Module (Internal Header)
 *
 * Internal structures, constants, and function prototypes for the
 * directory subsystem. This header should only be included by
 * implementation files within the dir/ module.
 */

#ifndef DIR_INTERNAL_H
#define DIR_INTERNAL_H

#include "dir/dir.h"
#include "acl/acl.h"
#include "ast/ast.h"
#include "audit/audit.h"
#include "rem_file/rem_file.h"
#include "file/file.h" // we call FILE_$PRIV_UNLOCK
#include "fim/fim.h"
#include "hint/hint.h"    /* HINT_$ADDI, HINT_$GET_HINTS */
#include "name/name.h"
#include "rem_name/rem_name.h"   /* REM_NAME_$GET_ENTRY, REM_NAME_$DIR_READU, ... */
#include "mst/mst.h"
#include "network/network.h"
#include "proc1/proc1.h"
#include "misc/crash_system.h"
#include "wp/wp.h"
#include "os/os.h"
#include "route/route.h"

/*
 * ============================================================================
 * Internal Data Structures
 * ============================================================================
 */

/*
 * Dir_$OpResponse - Common response structure for DIR_$DO_OP
 *
 * This structure holds the response from directory operations.
 * The exact fields used depend on the operation type.
 */
/*
 * Layout recovered from DIR_$DO_OP (0xE4C02C), which addresses the reply
 * through A3 (its 4th parameter), and from the three callers that read it
 * back: DIR_$READ_LINKU (0xE4D6C0), DIR_$DROP_LINKU (0xE517F6) and
 * DIR_$RESOLVE (0xE4D356).
 *
 *   0x00  f12/f13/f14/f15   reply header bytes
 *   0x04  status            tst.l (0x4,A3) / move.l ...,(0x4,A3)
 *   0x08  f18[0..1]         reply version word   (tst.w (0x8,A3))
 *   0x0A  f18[2..3]         accepted version     (move.w ...,(0xa,A3))
 *   0x13  f18[11]           bit 0 = "reply came from a remote node"
 *                           (bset.b #0x0,(0x13,A3) at 0xE4C1B6)
 *   0x14  payload           Pascal variant record, see the union below
 *
 * The payload is a genuine variant record: the delete/create family returns a
 * bare UID at 0x14, READ_LINKU/FIND_UID return a length word at 0x14 followed
 * by a UID at 0x16, and RESOLVE uses the whole 0x14..0x30 range.  DIR_$DO_OP
 * addresses as far as (0x40,A3) for the get-default-protection operation, so
 * the raw view spans 0x14..0x43.
 */
typedef struct __attribute__((packed, aligned(2))) Dir_$OpResponse {
    uint8_t  f12;           /* 0x00: Response flags byte 1 */
    uint8_t  f13;           /* 0x01: Response flags byte 2 */
    uint8_t  f14;           /* 0x02: Response flags byte 3 */
    uint8_t  f15;           /* 0x03: Response flags byte 4 */
    status_$t status;       /* 0x04: Operation status */
    uint8_t  f18[12];       /* 0x08..0x13: reply header (versions + flag byte) */
    union {
        uid_t    uid;       /* 0x14: UID result (delete / create_dir / add_bak / ...) */
        uint32_t cookie;    /* 0x14: continuation cookie (DIR_READU) */
        struct __attribute__((packed, aligned(2))) {
            uint16_t _20_2_;    /* 0x14: length word (READ_LINKU, FIND_UID) */
            uint32_t _22_4_;    /* 0x16: UID high */
            uint32_t f1a;       /* 0x1A: UID low */
            uint32_t _24_4_;    /* 0x1E */
        };
        struct __attribute__((packed, aligned(2))) {    /* DIR_$RESOLVE (0xE4D356 epilogue) */
            int8_t   more;          /* 0x14: negative => resolution incomplete */
            int8_t   loop;          /* 0x15: negative => repeat the request */
            uid_t    start_uid;     /* 0x16 */
            uid_t    resolved_uid;  /* 0x1E */
            uint16_t param5;        /* 0x26 */
            uint16_t param6;        /* 0x28 */
            uint16_t param7;        /* 0x2A */
            uint16_t param8;        /* 0x2C */
            uint16_t link_count;    /* 0x2E: clr.w at 0x00E4D10A */
            uint32_t redirect;      /* 0x30: dir_$do_op_resolve's extra_ret
                                     * (clr.l at 0x00E4D118); DIR_$DO_OP
                                     * forwards it to DIR_$UPDATE_HINT for a
                                     * cross-node RESOLVE (0x00E4C1E2) */
        } resolve;
        uint8_t  raw[0x30];     /* 0x14..0x43 raw view */
    };
} Dir_$OpResponse;

/* f18[11] (response offset 0x13), bit 0: the reply came from a remote node. */
#define DIR_RESP_REMOTE_FLAG_BYTE   11
#define DIR_RESP_REMOTE_FLAG        0x01

#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(Dir_$OpResponse, status) == 0x04, "Dir_$OpResponse.status");
_Static_assert(__builtin_offsetof(Dir_$OpResponse, f18) == 0x08, "Dir_$OpResponse.f18");
_Static_assert(__builtin_offsetof(Dir_$OpResponse, uid) == 0x14, "Dir_$OpResponse.uid");
_Static_assert(__builtin_offsetof(Dir_$OpResponse, cookie) == 0x14, "Dir_$OpResponse.cookie");
_Static_assert(__builtin_offsetof(Dir_$OpResponse, _20_2_) == 0x14, "Dir_$OpResponse._20_2_");
_Static_assert(__builtin_offsetof(Dir_$OpResponse, _22_4_) == 0x16, "Dir_$OpResponse._22_4_");
_Static_assert(__builtin_offsetof(Dir_$OpResponse, f1a) == 0x1A, "Dir_$OpResponse.f1a");
_Static_assert(__builtin_offsetof(Dir_$OpResponse, _24_4_) == 0x1E, "Dir_$OpResponse._24_4_");
_Static_assert(__builtin_offsetof(Dir_$OpResponse, resolve.start_uid) == 0x16, "resolve.start_uid");
_Static_assert(__builtin_offsetof(Dir_$OpResponse, resolve.resolved_uid) == 0x1E, "resolve.resolved_uid");
_Static_assert(__builtin_offsetof(Dir_$OpResponse, resolve.param5) == 0x26, "resolve.param5");
_Static_assert(__builtin_offsetof(Dir_$OpResponse, resolve.link_count) == 0x2E, "resolve.link_count");
_Static_assert(__builtin_offsetof(Dir_$OpResponse, resolve.redirect) == 0x30, "resolve.redirect");
_Static_assert(sizeof(Dir_$OpResponse) == 0x44, "Dir_$OpResponse spans 0x14..0x43 of payload");
#endif

/*
 * Dir_$OpRequest - Base request structure for directory operations
 *
 * All directory operations share a common header format.
 * Operation-specific data follows this header.
 */
typedef struct Dir_$OpRequest {
    uint8_t  op;            /* 0x00: Operation code (DIR_OP_*) */
    uint8_t  padding[3];    /* 0x01-0x03: Padding */
    uid_t    uid;           /* 0x04-0x0B: Directory UID */
    uint16_t reserved;      /* 0x0C-0x0D: Reserved/type field */
    /* Operation-specific data follows */
} Dir_$OpRequest;

/*
 * Dir_$OpAddHardLinkuRequest - Request structure for ADD_HARD_LINKU
 */
typedef struct Dir_$OpAddHardLinkuRequest {
    uint8_t  op;                /* Operation code: DIR_OP_ADD_HARD_LINKU */
    uint8_t  padding[3];
    uid_t    uid1;              /* Directory UID */
    uint16_t padding2;
    uint8_t  field8_0x98[134];  /* Variable length name data */
    uid_t    uid2;              /* Target file UID */
    uint16_t path_len;          /* Name length */
} Dir_$OpAddHardLinkuRequest;

/*
 * Maximum B-tree depth for directory pages.
 * Directories can have up to 8 levels of B-tree nesting.
 */
#define DIR_MAX_BTREE_DEPTH 9

/*
 * dir_page_hdr_t - On-disk header of a directory B-tree page
 *
 * Every directory page (0x400 bytes) starts with this 0x12-byte header; the
 * two-byte index table follows at +0x12 on an interior/leaf page, and on the
 * ROOT page a variable-length root area whose length is the word at +0x14
 * sits between the header and the index table (dir_$insert_entry computes
 * base_offset = 0x12 + root_area_len at 0xE4F416).
 *
 * Offsets recovered from dir_$insert_entry (0xE4F3BA):
 *   +0x00 tst.b (A3) / btst #13     flags word
 *   +0x02 move.l (A3)+,(0x2,A4)     directory UID high
 *   +0x06                           directory UID low
 *   +0x0A tst.w (0xa,A3)            page number; 0 marks the root page
 *   +0x0C move.w ...,(0xc,A3)       next-page link (0xFFFF = last)
 *   +0x0E move.w (0xe,A3)           end offset of the index table
 *   +0x10 move.w (0x10,A3)          base of the entry heap (grows downwards)
 *
 * The flags word is written as a word and then patched with byte operations,
 * so its two halves are named separately:
 *   flags high byte: 0xC0 = page kind (0 = leaf, 1 = interior)
 *   flags word bit 13 (0x2000) = page has reclaimable dead entries
 *   flags low byte low 6 bits = page format version
 */
typedef struct __attribute__((packed, aligned(2))) dir_page_hdr_t {
    uint8_t     kind;           /* +0x00: high byte of the flags word */
    uint8_t     version;        /* +0x01: low byte of the flags word */
    uint32_t    dir_uid_high;   /* +0x02 */
    uint32_t    dir_uid_low;    /* +0x06 */
    uint16_t    page_no;        /* +0x0A: 0 == root page */
    uint16_t    next_page;      /* +0x0C */
    uint16_t    index_end;      /* +0x0E */
    uint16_t    heap_base;      /* +0x10 */
} dir_page_hdr_t;

/* Byte 0 and byte 1 together form the flags word tested with btst #13. */
#define DIR_PAGE_KIND_MASK      0xC0    /* in dir_page_hdr_t.kind */
#define DIR_PAGE_KIND_SHIFT     6
#define DIR_PAGE_VERSION_MASK   0x3F    /* in dir_page_hdr_t.version */
#define DIR_PAGE_RECLAIMABLE    0x2000  /* bit 13 of the flags word */
#define DIR_PAGE_HDR_SIZE       0x12
#define DIR_PAGE_SIZE           0x400
#define DIR_PAGE_ROOT_AREA_LEN  0x14    /* root pages only: word holding the root area length */

#if defined(ARCH_M68K)
_Static_assert(sizeof(dir_page_hdr_t) == DIR_PAGE_HDR_SIZE, "dir_page_hdr_t must be 0x12 bytes");
_Static_assert(__builtin_offsetof(dir_page_hdr_t, kind) == 0x00, "dir_page_hdr_t.kind");
_Static_assert(__builtin_offsetof(dir_page_hdr_t, version) == 0x01, "dir_page_hdr_t.version");
_Static_assert(__builtin_offsetof(dir_page_hdr_t, dir_uid_high) == 0x02, "dir_page_hdr_t.dir_uid_high");
_Static_assert(__builtin_offsetof(dir_page_hdr_t, dir_uid_low) == 0x06, "dir_page_hdr_t.dir_uid_low");
_Static_assert(__builtin_offsetof(dir_page_hdr_t, page_no) == 0x0A, "dir_page_hdr_t.page_no");
_Static_assert(__builtin_offsetof(dir_page_hdr_t, next_page) == 0x0C, "dir_page_hdr_t.next_page");
_Static_assert(__builtin_offsetof(dir_page_hdr_t, index_end) == 0x0E, "dir_page_hdr_t.index_end");
_Static_assert(__builtin_offsetof(dir_page_hdr_t, heap_base) == 0x10, "dir_page_hdr_t.heap_base");
#endif

/*
 * dir_insert_ctx_t - Shared context for dir_$insert_entry and helper functions
 *
 * In the original M68K code, this data lived in dir_$add_entry's stack frame
 * (link.w A6,-0xB0) and was accessed by nested Pascal subprocedures via the
 * parent frame pointer (A1 = parent A6). For the C flattening, all shared
 * state is bundled into this struct and passed explicitly.
 *
 * Frame offset mappings (A1 = parent A6):
 *   Parameters: handle(+0x08), name(+0x0C), entry_type(+0x12),
 *     extra(+0x14), uid(+0x18), link_len(+0x1C), link_data(+0x1E)
 *   Locals: max_depth(-0xAE), overflow_page(-0xAA), page_count(-0xA6),
 *     free_space(-0x9E), page_data(-0x94), idx_base(-0x90),
 *     temp_entry(-0x8C), inter_page(-0x78), new_page(-0x74),
 *     dir_uid(-0x50/-0x4C), fim_data(-0x18), remove_uid(-0x20)
 *   B-tree path: page_num at -0x74+N*4, entry_idx at -0x72+N*4
 *   Split pages: at -0x4A+i*2 (find_extra offset 0x26)
 */
typedef struct dir_insert_ctx {
    /* === Parameters from dir_$add_entry === */
    uint32_t    handle;             /* Directory handle (A1+0x08).  A kernel
                                     * virtual address in a 32-bit word; every
                                     * dereference goes through
                                     * NAME_$HANDLE_TO_PTR (name/name.h) so
                                     * the code is exercisable on a host whose
                                     * pointers are wider (source-qu3v). */
    void       *name;              /* Entry name (A1+0x0C) */
    uint16_t    name_len;          /* Entry name length (A1+0x10) */
    uint16_t    entry_type;        /* Entry type 2/3/4 (A1+0x12) */
    uint32_t    extra_val;         /* Extra data (A1+0x14) */
    uid_t      *uid;               /* Target UID pointer (A1+0x18) */
    uint16_t    link_len;          /* Link data length, type 4 (A1+0x1C) */
    void       *link_data;         /* Link data pointer, type 4 (A1+0x1E) */
    int16_t     overflow_page;     /* Overflow page or -1 (A1-0xAA) */
    int16_t     max_depth;         /* B-tree depth / initial slot_idx (A1-0xAE) */
    int16_t     current_slot;      /* Current B-tree level being processed (A1-0xA8),
                                    * set by alloc_split_page, read by finalize_split */

    /* === B-tree traversal path from dir_$find_entry === */
    /* Level N: path_page[N] = page number, path_entry[N] = entry index.
     * Level 0 = root, level max_depth = leaf. In the original M68K frame,
     * these were interleaved as (page_num, entry_idx) pairs at A6-0x74+N*4.
     * Level 0 was stored just below find_entry's extra buffer, and levels 1+
     * were within the extra buffer. */
    int16_t     path_page[DIR_MAX_BTREE_DEPTH];
    int16_t     path_entry[DIR_MAX_BTREE_DEPTH];

    /* === Directory UID from root page header === */
    /* Stored in find_entry's extra buffer at offset 0x20/0x24. */
    uint32_t    dir_uid_high;      /* (A1-0x50) */
    uint32_t    dir_uid_low;       /* (A1-0x4C as uint32) */

    /* === Split page tracking === */
    /* Page numbers allocated during B-tree splits. In the original code,
     * stored in find_entry's extra buffer at offset 0x26+i*2 (A6-0x4A+i*2).
     * Populated by dir_$alloc_split_page. */
    int16_t     split_pages[16];
    int16_t     page_count;        /* Count of allocated split pages (A1-0xA6) */

    /* === Working state === */
    uint8_t    *page_data;         /* Current mapped page (A1-0x94) */
    uint8_t    *idx_base;          /* Index table base in page (A1-0x90) */
    uint8_t    *new_page;          /* New page during splits (A1-0x74) */
    uint8_t    *inter_page;        /* Root/internal page ptr (A1-0x78) */
    uint8_t    *temp_entry;        /* Temporary entry pointer (A1-0x8C) */
    int16_t     free_space;        /* Free space on current page (A1-0x9E) */

    /* === FIM cleanup data (for type 4 soft link entries) === */
    uint8_t     fim_data[16];      /* FIM cleanup structure (A1-0x18) */
    uint8_t     remove_uid[8];     /* UID buffer for FIM error recovery (A1-0x20) */
} dir_insert_ctx_t;

/* NOTE: dir_insert_ctx_t is NOT a recovered memory record -- it is a C
 * gathering of dir_$add_entry's m68k stack-frame locals, and the (A1+/-N)
 * annotations above are frame displacements, not struct offsets.  It
 * therefore carries no layout _Static_asserts. */

/*
 * ============================================================================
 * Internal Global Data References
 * ============================================================================
 */

/*
 * Directory operation parameter tables
 *
 * These tables contain operation-specific parameters indexed by operation type.
 * Located at 0xE7FC42 on M68K.
 */
/*
 * DIR_$OP_TAB - the per-operation parameter table, 0x00E7FC42 (the SAU2 map
 * names the symbol).  Records are 8 bytes and every reader indexes the same
 * family by (opcode >> 1) with an 8-byte stride, from a *virtual* base of
 * 0x00E7FB9A -- 21 records below DIR_$OP_TAB, i.e. the table is biased and
 * only records 21..46 are physically present:
 *
 *   DIR_$SERVER  0x00E58248  move.w D0w,D1w (D0 = opcode >> 1)
 *                0x00E5824A  movea.l #0xe7fc42,A1
 *                0x00E58252  lsl.l #0x3,D1 / lea (0x0,A1,D1),A0
 *                0x00E58258  move.w (-0xa8,A0),D1w      -> record + 0x00
 *   DIR_$DO_OP   0x00E4C034  lea (0xe7dc00).l,A5
 *                0x00E4C0B4  lsl.l #0x3,D1 / lea (0x0,A5,D1),A0
 *                0x00E4C0BA  move.w (0x1f9c,A0),(0x12,A2)  -> record + 0x02
 *                0x00E4C188  cmp.w  (0x1f9c,A0),D2w        -> record + 0x02
 *                0x00E4C254  add.w  (0x1fa0,A0),D1w        -> record + 0x06
 *                0x00E4C25A  move.w (0x1f9c,A0),(0xa,A3)   -> record + 0x02
 *   the DIR_$<op>U client wrappers read their own record's + 0x00 and + 0x04
 *   by absolute address (e.g. DIR_$ADD_MOUNT 0x00E534D6 / 0x00E534FC read
 *   0x00E7FD02 and 0x00E7FD06, which is record 45).
 *
 * 0x00E7FC42 - 0x00E7FB9A = 0xA8 = 21 * 8, and DIR_$DO_OP's jump table admits
 * opcodes 0x2A..0x5C (`subi.w #0x2a,D0w` / `cmpi.w #0x33,D0w` / `bcc` at
 * 0x00E4C266), i.e. records 21..46 -- exactly 26 records running
 * 0x00E7FC42..0x00E7FD12.  The bytes a record 0..20 would occupy are other
 * DIR globals (DIR_$NAME_OFFSET_TABLE, DIR_$LK_WAITS, the free-list heads),
 * so the array below starts at record 21 and DIR_$OP_REC applies the bias.
 */
typedef struct dir_$op_tab_entry_t {
  uint16_t version;        /* +0x00 request body version */
  uint16_t reply_version;  /* +0x02 reply body version */
  uint16_t base_size;      /* +0x04 fixed part of the request size */
  uint16_t reply_size;     /* +0x06 fixed part of the reply body size */
} dir_$op_tab_entry_t;

#if defined(ARCH_M68K)
_Static_assert(sizeof(dir_$op_tab_entry_t) == 8, "dir_$op_tab_entry_t");
#endif

/* The first record present, opcode 0x2A >> 1. */
#define DIR_$OP_TAB_BASE_INDEX 21

#define DIR_$OP_TAB_ENTRIES 26
extern dir_$op_tab_entry_t DIR_$OP_TAB[DIR_$OP_TAB_ENTRIES];

/* The record for op_half = opcode >> 1, with the table's 21-record bias. */
#define DIR_$OP_REC(half) (DIR_$OP_TAB[(half) - DIR_$OP_TAB_BASE_INDEX])

/* The two words of each record, by the address Ghidra labels them with. */
#define DAT_00e7fc4a     (DIR_$OP_TAB[ 1].version)  /* 0x00E7FC4A */
#define DAT_00e7fc4e     (DIR_$OP_TAB[ 1].base_size)  /* 0x00E7FC4E */
#define DAT_00e7fc52     (DIR_$OP_TAB[ 2].version)  /* 0x00E7FC52 */
#define DAT_00e7fc56     (DIR_$OP_TAB[ 2].base_size)  /* 0x00E7FC56 */
#define DAT_00e7fc62     (DIR_$OP_TAB[ 4].version)  /* 0x00E7FC62 */
#define DAT_00e7fc66     (DIR_$OP_TAB[ 4].base_size)  /* 0x00E7FC66 */
#define DAT_00e7fc6a     (DIR_$OP_TAB[ 5].version)  /* 0x00E7FC6A */
#define DAT_00e7fc6e     (DIR_$OP_TAB[ 5].base_size)  /* 0x00E7FC6E */
#define DAT_00e7fc72     (DIR_$OP_TAB[ 6].version)  /* 0x00E7FC72 */
#define DAT_00e7fc76     (DIR_$OP_TAB[ 6].base_size)  /* 0x00E7FC76 */
#define DAT_00e7fc7a     (DIR_$OP_TAB[ 7].version)  /* 0x00E7FC7A */
#define DAT_00e7fc7e     (DIR_$OP_TAB[ 7].base_size)  /* 0x00E7FC7E */
#define DAT_00e7fc82     (DIR_$OP_TAB[ 8].version)  /* 0x00E7FC82 */
#define DAT_00e7fc86     (DIR_$OP_TAB[ 8].base_size)  /* 0x00E7FC86 */
#define DAT_00e7fc8a     (DIR_$OP_TAB[ 9].version)  /* 0x00E7FC8A */
#define DAT_00e7fc8e     (DIR_$OP_TAB[ 9].base_size)  /* 0x00E7FC8E */
#define DAT_00e7fc92     (DIR_$OP_TAB[10].version)  /* 0x00E7FC92 */
#define DAT_00e7fc96     (DIR_$OP_TAB[10].base_size)  /* 0x00E7FC96 */
#define DAT_00e7fc9a     (DIR_$OP_TAB[11].version)  /* 0x00E7FC9A */
#define DAT_00e7fc9e     (DIR_$OP_TAB[11].base_size)  /* 0x00E7FC9E */

/* 0x00E7FC42 is DIR_$OP_TAB itself, record 0's version word. */
#define DAT_00e7fc42     (DIR_$OP_TAB[ 0].version)   /* 0x00E7FC42 */
/* GET_ENTRYU (op 0x44) params.  Same record+0 / record+4 pair as the DAT_
 * cells above, but reached through A5 = 0xE7DC00 rather than by absolute
 * address: DIR_$GET_ENTRYU_FUN_00e4d460 reads (0x20aa,A5) at 0x00E4D4A6 and
 * (0x20ae,A5) at 0x00E4D4B8.  Named for what they are used for. */
extern uint16_t DIR_$GET_ENTRYU_REQ_PARM;  /* word at 0xE7FCAA -> request+0x0E */
extern uint16_t DIR_$GET_ENTRYU_REQ_LEN;   /* word at 0xE7FCAE, added to name_len */
#define DAT_00e7fcba     (DIR_$OP_TAB[15].version)   /* FIX_DIR params */  /* 0x00E7FCBA */
#define DAT_00e7fcbe     (DIR_$OP_TAB[15].base_size)  /* 0x00E7FCBE */
#define DAT_00e7fcca     (DIR_$OP_TAB[17].version)   /* SET_DEFAULT_ACL params */  /* 0x00E7FCCA */
#define DAT_00e7fcce     (DIR_$OP_TAB[17].base_size)  /* 0x00E7FCCE */
#define DAT_00e7fcd2     (DIR_$OP_TAB[18].version)   /* GET_DEFAULT_ACL params */  /* 0x00E7FCD2 */
#define DAT_00e7fcd6     (DIR_$OP_TAB[18].base_size)  /* 0x00E7FCD6 */
#define DAT_00e7fcda     (DIR_$OP_TAB[19].version)   /* VALIDATE_ROOT_ENTRY params */  /* 0x00E7FCDA */
#define DAT_00e7fcde     (DIR_$OP_TAB[19].base_size)  /* 0x00E7FCDE */
#define DAT_00e7fce2     (DIR_$OP_TAB[20].version)   /* SET_PROTECTION params */  /* 0x00E7FCE2 */
#define DAT_00e7fce6     (DIR_$OP_TAB[20].base_size)  /* 0x00E7FCE6 */
#define DAT_00e7fcea     (DIR_$OP_TAB[21].version)   /* SET_DEF_PROTECTION params */  /* 0x00E7FCEA */
#define DAT_00e7fcee     (DIR_$OP_TAB[21].base_size)  /* 0x00E7FCEE */
#define DAT_00e7fcf2     (DIR_$OP_TAB[22].version)   /* GET_DEF_PROTECTION params */  /* 0x00E7FCF2 */
#define DAT_00e7fcf6     (DIR_$OP_TAB[22].base_size)  /* 0x00E7FCF6 */
#define DAT_00e7fcfa     (DIR_$OP_TAB[23].version)   /* RESOLVE params */  /* 0x00E7FCFA */
#define DAT_00e7fcfe     (DIR_$OP_TAB[23].base_size)  /* 0x00E7FCFE */
#define DAT_00e7fd02     (DIR_$OP_TAB[24].version)   /* ADD_MOUNT params - my_host_id word */  /* 0x00E7FD02 */
#define DAT_00e7fd06     (DIR_$OP_TAB[24].base_size)   /* ADD_MOUNT request size */  /* 0x00E7FD06 */

#define DAT_00e7fd0a     (DIR_$OP_TAB[25].version)   /* DROP_MOUNT params - my_host_id word */  /* 0x00E7FD0A */
#define DAT_00e7fd0e     (DIR_$OP_TAB[25].base_size)   /* DROP_MOUNT request size */  /* 0x00E7FD0E */
/*
 * The last 0x12 bytes of the DIR segment, 0x00E7FD12..0x00E7FD24, defined in
 * dir/dir_data.c.  dir_$do_op_dir_readu reads the three longwords through
 * A5 = 0x00E7DC00 as (0x2114,A5) / (0x2118,A5) / (0x211c,A5); the pad word
 * and the ".bak" string have no reader in the image.
 */
extern uint16_t DAT_00e7fd12;   /* 0x00E7FD12 alignment fill */
extern uint32_t DAT_00e7fd14;   /* 0x00E7FD14 first-real-entry cookie */
extern uint32_t DAT_00e7fd18;   /* 0x00E7FD18 ".." pseudo-entry cookie */
extern uint32_t DAT_00e7fd1c;   /* 0x00E7FD1C "." pseudo-entry cookie */
extern char     DAT_00e7fd20[4];/* 0x00E7FD20 ".bak" */

/*
 * ============================================================================
 * OLD_* Fallback Functions (for compatibility with older servers)
 * ============================================================================
 */

/* These are the legacy implementations called when the new protocol fails */

void DIR_$OLD_ADDU(uid_t *dir_uid, char *name, int16_t *name_len,
                   uid_t *file_uid, status_$t *status_ret);

void DIR_$OLD_ROOT_ADDU(uid_t *dir_uid, char *name, int16_t *name_len,
                        uid_t *file_uid, uint32_t *flags, status_$t *status_ret);

/* DIR_$OLD_ADD_HARD_LINKU: declared in dir/dir.h -- REM_FILE_$SERVER calls it. */

void DIR_$OLD_ADD_LINKU(uid_t *dir_uid, char *name, int16_t *name_len,
                        void *target, uint16_t *target_len, status_$t *status_ret);

void DIR_$OLD_ADD_BAKU(uid_t *dir_uid, char *name, uint16_t *name_len,
                       uid_t *backup_uid, status_$t *status_ret);

/* A6+0x14 is the STATUS OUTPUT and A6+0x18/+0x1C are two pointers to Domain
 * booleans - see the argument order at 0x00E5717C-0x00E5719C. */
void DIR_$OLD_DELETE_FILEU(uid_t *dir_uid, char *name, uint16_t *name_len,
                           status_$t *status_ret, boolean *check_del_right,
                           boolean *no_lock);

void DIR_$OLD_DROPU(uid_t *dir_uid, char *name, uint16_t *name_len,
                    uid_t *file_uid, status_$t *status_ret);

/* DIR_$OLD_DROP_HARD_LINKU: declared in dir/dir.h -- REM_FILE_$SERVER calls it. */

void DIR_$OLD_DROP_LINKU(uid_t *dir_uid, char *name, uint16_t *name_len,
                         uid_t *target_uid, status_$t *status_ret);

void DIR_$OLD_CNAMEU(uid_t *dir_uid, char *old_name, uint16_t *old_name_len,
                     char *new_name, uint16_t *new_name_len, status_$t *status_ret);

void DIR_$OLD_CREATE_DIRU(uid_t *parent_uid, char *name, uint16_t *name_len,
                          uid_t *new_dir_uid, status_$t *status_ret);

void DIR_$OLD_DROP_DIRU(uid_t *parent_uid, char *name, uint16_t *name_high,
                        uint16_t *name_low, status_$t *status_ret);

void DIR_$OLD_FIX_DIR(uid_t *dir_uid, status_$t *status_ret);

void DIR_$OLD_GET_DEFAULT_ACL(uid_t *dir_uid, uid_t *acl_type, uid_t *acl_ret,
                              status_$t *status_ret);

/* DIR_$OLD_SET_DEFAULT_ACL: declared in dir/dir.h -- REM_FILE_$SERVER calls it. */

void DIR_$OLD_VALIDATE_ROOT_ENTRY(char *name, uint16_t *name_len,
                                  status_$t *status_ret);

void DIR_$OLD_FIND_UID(uid_t *dir_uid, uid_t *target_uid, char *name_buf,
                       int16_t *name_len_ret, status_$t *status_ret);

uint32_t DIR_$OLD_FIND_NET(uid_t *dir_uid, uint32_t *index);

/* dir_$old_entry_t and DIR_$OLD_GET_ENTRYU: declared in dir/dir.h --
 * NAME_$OLD_DELETE_ENTRYU (name/old_delete_entryu.c) uses both. */

void DIR_$OLD_READ_LINKU(int16_t dir_uid_low, int16_t name_low, uint16_t *name_len,
                         int16_t target_low, int16_t *target_len,
                         uid_t *target_uid, status_$t *status_ret);

/*
 * ============================================================================
 * Internal Helper Functions
 * ============================================================================
 */

/*
 * DIR_$GET_ENTRYU_FUN_00e4d460 - Internal entry retrieval helper
 *
 * Called by DIR_$GET_ENTRYU to perform the actual lookup.
 *
 * Original address: 0x00E4D460
 */
void DIR_$GET_ENTRYU_FUN_00e4d460(uid_t *local_uid, char *name,
                                   uint16_t name_len, void *entry_ret,
                                   status_$t *status_ret);

/*
 * DIR_$DIR_READU_FUN_00e4e1a8 - Internal directory read helper
 *
 * Originally a nested Pascal subprocedure of DIR_$DIR_READU.
 * Flattened to take explicit parameters from the parent function.
 *
 * Original address: 0x00E4E1A8
 */
void DIR_$DIR_READU_FUN_00e4e1a8(uid_t *dir_uid, int32_t *continuation,
                                  uint16_t *max_entries, int32_t *count_ret,
                                  void *flags, int32_t *eof_ret,
                                  status_$t *status_ret);

/*
 * dir_$dir_readu_via_do_op - Internal directory read helper
 *
 * Called by DIR_$DIR_READU for normal directory reads.
 *
 * Original address: 0x00E4E1FE
 */
void dir_$dir_readu_via_do_op(status_$t *status_ret);

/*
 * DIR_$ADD_ENTRY_INTERNAL - Internal add entry helper
 *
 * Shared implementation for DIR_$ADDU and DIR_$ROOT_ADDU.
 *
 * Original address: 0x00E500B8
 */
void DIR_$ADD_ENTRY_INTERNAL(uid_t *dir_uid, char *name, int16_t name_len,
                  uid_t *file_uid, uint32_t flags, status_$t *status_ret);

/* dir_$find_uid_internal - Internal find UID helper
 *
 * Shared implementation for DIR_$FIND_UID and DIR_$FIND_NET.
 * Sends DO_OP request 0x46 (opcode 0x11a), falls back to
 * DIR_$OLD_FIND_NET (flag<0) or DIR_$OLD_FIND_UID on error.
 *
 * Original address: 0x00E4E786
 * Size: 246 bytes
 */
void dir_$find_uid_internal(uid_t *dir_uid, uid_t *target_uid, int8_t flag,
                            int16_t name_buf_len, char *name_buf,
                            int16_t *name_len_ret, uint32_t *net_ret,
                            status_$t *status_ret);

/*
 * ============================================================================
 * Additional Internal Helper Functions
 * ============================================================================
 */

/*
 * name_$old_add_link, name_$old_get_root_entry, name_$old_get_entry_nonroot,
 * name_$old_add_entry, name_$old_drop_entry, name_$validate_leaf,
 * NAME_$LOCK_DIR and NAME_$UNLOCK_DIR are name-subsystem routines; they are
 * declared in name/name.h (included above).
 */

/* NAME_$OLD_DELETE_ENTRYU carries the NAME_$ prefix, so its prototype lives
 * in name/name.h (bead source-3uo); the body is dir/old_delete_entryu.c. */

/* dir_$old_unlink_entry: declared in dir/dir.h -- NAME_$OLD_DROP_ENTRY
 * (name/old_drop_entry.c) calls it. */

/* dir_$old_find_entry - Find entry in directory by name
 *
 * Searches a directory for a named entry. First checks inline entries
 * (slots 1..N at 0x30-byte intervals), then uses a hash lookup to
 * search overflow chains. Returns the entry pointer, slot index,
 * and chain level.
 *
 * Returns: 0xFF (true) if found, 0 if not found
 *
 * Original address: 0x00E54B9E
 */
int8_t dir_$old_find_entry(uint32_t handle, uint8_t *name, uint16_t name_len,
                           int32_t *entry_ret, uint16_t *slot_idx,
                           uint16_t *chain_level);

/* dir_$old_hash_name - Compute hash for directory entry name
 *
 * Computes a hash value for a directory entry name. Returns
 * the hash modulo the number of hash buckets in the low 16 bits,
 * and the quotient in the high 16 bits.
 *
 * Original address: 0x00E54B58
 */
uint16_t dir_$old_hash_name(uint8_t *name, uint16_t name_len, uint16_t num_buckets);

/* dir_$old_delete_entry - Delete/clear a directory entry
 *
 * Removes an entry from the directory buffer. Handles both inline
 * entries (stride 0x30, chain_level=0) and overflow entries
 * (bucket stride 0x96, chain_level>0). Clears the entry type and
 * active flag, decrements the total entry count, and frees any
 * overflow link data blocks (for type 3/soft link entries).
 *
 * Original address: 0x00E555DC
 * Size: 192 bytes
 */
void dir_$old_delete_entry(uint32_t handle, uint16_t slot_idx,
                           uint16_t chain_level, uint16_t hash);

/* dir_$old_add_entry: declared in dir/dir.h -- NAME_$OLD_ADD_LINK_LOCAL
 * (name/old_add_link_local.c) calls it. */

/* dir_$old_add_entry_ext: declared in dir/dir.h -- NAME_$OLD_ADD_ENTRY
 * (name/old_add_entry.c) calls it. */

/* dir_$old_add_link_entry - Add symbolic link entry to directory
 *
 * Allocates overflow blocks for the link target data (up to 0x90
 * bytes per block, 2 blocks max for targets > 0x90 bytes), copies
 * the target text into the overflow blocks, then adds the entry
 * via dir_$old_add_entry with type 3. On failure, frees allocated blocks.
 *
 * Original address: 0x00E5545C
 * Size: 384 bytes
 */
void dir_$old_add_link_entry(uid_t *dir_uid, uint32_t handle, uint8_t *name,
                             uint16_t name_len, void *target, uint16_t target_len,
                             uint8_t flags, uint8_t *result, status_$t *status_ret);

/* dir_$old_read_link_data - Read link target data from overflow blocks
 *
 * Reads symbolic link target data from overflow blocks referenced
 * by the link descriptor. The descriptor contains:
 *   - uint16_t total_len (total link data length)
 *   - uint16_t block_idx[3] (overflow block indices)
 * Copies up to 0x90 bytes per block into the output buffer.
 * The 5th parameter (status_ret) is passed by callers but unused.
 *
 * Original address: 0x00E55764
 * Size: 112 bytes
 */
void dir_$old_read_link_data(uint32_t handle, void *link_desc,
                             uint8_t *buf, uint16_t *buf_len);

/* dir_$old_create_obj - Create directory storage object
 *
 * Creates the underlying file for a new directory:
 * 1. FILE_$PRIV_CREATE - create the file
 * 2. FILE_$PRIV_LOCK - lock the new file
 * 3. MST_$MAPS - map the file into memory
 * 4. dir_$old_init_buf - initialize the directory buffer
 * 5. Set up default ACLs (from parent or global defaults)
 * 6. AST_$COND_FLUSH - flush changes
 * On failure: truncates/deletes the created file, sets error bit.
 *
 * Original address: 0x00E54546
 * Size: 488 bytes
 */
void dir_$old_create_obj(uid_t *parent_uid, uint32_t handle, uint16_t type,
                         uid_t *new_dir_uid, status_$t *status_ret);

/* dir_$old_free_slot - Release/free overflow slot in directory buffer
 *
 * Manages overflow slot lifecycle. Called to:
 * - Remove entries from hash chains (with hash param)
 * - Free overflow data blocks (with param2=0)
 * Unlinks slot from its chain if active with zero chain count,
 * then prepends it to the free list at handle+0x0C.
 *
 * Original address: 0x00E5518C
 */
void dir_$old_free_slot(uint32_t handle, uint16_t hash, uint16_t slot_idx);

/* dir_$old_find_free_inline_slot - Find empty inline slot in directory buffer
 *
 * Scans inline entry slots (1..inline_count) looking for one with a zero
 * type byte (entry + 0x11 == 0), indicating an empty slot. Returns the
 * slot index via *slot_out.
 *
 * Returns: 0xFF (true/negative) if free slot found, 0 if all slots occupied
 *
 * Original address: 0x00E54DCC
 * Size: 68 bytes
 */
int8_t dir_$old_find_free_inline_slot(uint32_t handle, uint16_t inline_count,
                                       uint16_t *slot_out);

/* dir_$old_find_overflow_slot - Find or allocate overflow slot for entry
 *
 * Searches hash chain for an overflow bucket with a free sub-entry slot.
 * If no existing bucket has space, allocates a new one from the free list.
 * In replace mode (flags < 0), may evict single-ref entries to make room.
 *
 * Returns: 0xFF (true/negative) if slot found, 0 if directory full
 *
 * Original address: 0x00E54F8A
 * Size: 512 bytes
 */
int8_t dir_$old_find_overflow_slot(uint32_t handle, uint16_t hash,
                                    int8_t flags, uint16_t *bucket_out,
                                    uint16_t *sub_slot_out);

/* FUN_00e54e10 - Allocate overflow slot from free list
 * Original address: 0x00E54E10
 */
uint16_t FUN_00e54e10(uint32_t handle);

/* FUN_00e54e62 - Allocate overflow slot with hash hint
 * Original address: 0x00E54E62
 */
uint16_t FUN_00e54e62(uint32_t handle, uint16_t hash_hint);

/* dir_$old_init_buf - Initialize directory buffer
 *
 * Initializes a directory buffer structure. Copies 10-byte template from
 * DAT_00e5453c, sets UID_$NIL, initializes entry arrays (18 entries at
 * stride 0x30, 43 entries at offset 0x3AA), and clears flag fields.
 *
 * Original address: 0x00E544B0
 * Size: 140 bytes
 */
void dir_$old_init_buf(void *buffer);

/* dir_$read_canned_root - Read entries from canned root directory
 * Original address: 0x00E4DFFE
 */
void dir_$read_canned_root(void);

/* DIR_$IS_RETRYABLE_STATUS - Check if status code is retryable
 * Original address: 0x00E4BC26
 */
int8_t DIR_$IS_RETRYABLE_STATUS(status_$t status);

/* DIR_$UPDATE_HINT - Update hint after redirect
 * Original address: 0x00E4BC76
 */
void DIR_$UPDATE_HINT(uid_t *uid, uint32_t hint1, uint32_t hint2,
                      uid_t *redirect, uint32_t param5);

/*
 * AUDIT_$LOG_CNAME_OP, AUDIT_$LOG_LINK_OP, AUDIT_$LOG_DIR_OP,
 * audit_$log_mount_op and audit_$log_prot_op are audit-subsystem routines
 * (audit/log_*_op.c); they are declared in audit/audit.h (included above).
 */

/* audit_$log_resolve_op carries the audit_$ prefix, so its prototype lives in
 * audit/audit.h (bead source-3uo); the body is dir/audit_log_resolve_op.c. */

/* DIR_$VALIDATE_PAGES - Validate and compact directory pages
 *
 * Validates structural integrity of directory pages by walking them
 * backward from the last page. Checks UID consistency, removes orphan
 * pages, and truncates the directory to the correct size.
 *
 * Called from dir_$open_dir (directory open) and DIR_$CLEANUP.
 *
 * Original address: 0x00E53728
 * Size: 752 bytes
 */
uint32_t DIR_$VALIDATE_PAGES(void *handle, char crash_flag, status_$t *status_ret);

/* dir_$open_dir - Open/lock directory handle
 *
 * Opens a directory by UID and returns a handle. The mode and rights
 * parameters control the access level. Allocates a handle slot, copies
 * the UID, locks the object, validates directory format (type 5),
 * checks ACL rights, and validates pages.
 *
 *   mode 0: read-only (auto-promoted to 2 internally, with recovery)
 *   mode 1: read with ACL check
 *   mode 2: write access
 *   rights: access rights bitmask (e.g., 3 = create, 8 = write ACL)
 *
 * On failure, the handle is automatically released via dir_$release_handle.
 *
 * Original address: 0x00E4BA02
 * Size: 546 bytes
 */
void dir_$open_dir(void *uid, int16_t mode, int16_t rights,
                   void *handle_ret, status_$t *status_ret);

/* dir_$map_page - Map directory data page with 2-slot LRU cache
 *
 * Maps a specific page of a directory's data into memory. Uses a
 * 2-slot cache keyed by page group (page_idx >> 5). Each group
 * contains 32 pages of 1024 bytes each (32KB). On cache miss,
 * calls MST_$REMAP_PRIVI to map the group.
 *
 * Returns pointer to page data (page_idx & 0x1F) * 1024 bytes
 * into the mapped group.
 *
 * Original address: 0x00E4B340
 * Size: 260 bytes
 */
void *dir_$map_page(void *handle, int16_t page_idx);

/* DIR_$UNLOCK_OBJ - Unlock/release lock on directory handle
 * Original address: 0x00E4B234
 */
void DIR_$UNLOCK_OBJ(void *handle);

/* DIR_$UNMAP_PAGES - Unmap directory pages from memory
 * Original address: 0x00E4B6BA
 */
void DIR_$UNMAP_PAGES(void *handle);

/* DIR_$WIRE_PAGE - Wire (pin) a directory page in memory
 * Original address: 0x00E4B7B6
 */
void DIR_$WIRE_PAGE(void *handle, void *page_data);

/* DIR_$ALLOC_HANDLE - Allocate directory handle slot
 * Original address: 0x00E4B86E
 */
void *DIR_$ALLOC_HANDLE(void);

/* DIR_$FREE_HANDLE - Free directory handle slot
 * Original address: 0x00E4B980
 */
void DIR_$FREE_HANDLE(void *handle);

/* DIR_$LOCK_OBJ - Lock/open directory object
 * Original address: 0x00E4AFA8
 */
void DIR_$LOCK_OBJ(void *handle, int16_t mode, status_$t *status_ret);

/* DIR_$VALIDATE_HANDLE - Validate directory handle
 * Original address: 0x00E4B44C
 */
void DIR_$VALIDATE_HANDLE(void *handle, int16_t mode, status_$t *status_ret);

/* dir_$remove_entry - Remove entry from directory by name
 * Original address: 0x00E50FC8
 */
void dir_$remove_entry(void *handle, void *name, int16_t name_len,
                       int16_t op_type, void *uid_ret, status_$t *status_ret);

/*
 * Directory handle field offsets used across dir/.
 *
 * 0x3A holds the volume word DIR_$VALIDATE_HANDLE copies out of the object's
 * location descriptor (`move.w (-0x56,A6),(0x3a,A2)` at 0x00E4B566);
 * dir_$do_op_delete compares an entry's own file_$obj_loc_t.volume against it
 * at 0x00E5138C to decide whether the entry still names a live local object.
 */
#define DIR_HANDLE_VOLUME_OFF   0x3A

/*
 * DIR_$DO_OP's mount table, in the module data area A5 = 0xE7DC00 addresses.
 * dir_$do_op_add_mount, dir_$do_op_drop_mount, dir_$do_op_find_uid and
 * dir_$do_op_delete all walk it; dir_$do_op_delete reads the 16-bit count at
 * `(0x155a,A5)` (0x00E51422) and the source UIDs at `A5 + 8 + 0x1554 + i*8`
 * (0x00E5142A-0x00E5144C).
 */
#define DIR_MOUNT_COUNT_OFF     0x1558  /* Mount count (32-bit) */
#define DIR_MOUNT_COUNT16_OFF   0x155A  /* Mount count (16-bit, low half) */
#define DIR_MOUNT_SRC_BASE      0x155C  /* Source UIDs: +idx*8 */
#define DIR_MOUNT_TGT_BASE      0x159C  /* Target UIDs: +idx*8 */
#define DIR_MOUNT_NODE_BASE     0x15DC  /* Node IDs:    +idx*4 */
#define DIR_MOUNT_MAX           8       /* Maximum mount entries */

/* dir_$release_wire - Release wired page and reset cache state
 *
 * Releases a wired directory page (offset 0x14 in handle) via
 * WP_$UNWIRE and resets the max_slots field (offset 0x1C) to 2.
 * Crashes if max_slots is already 2 (double-release).
 *
 * Original address: 0x00E4B838
 * Size: 54 bytes
 */
void dir_$release_wire(void *handle);

/* dir_$release_handle - Release directory handle completely
 *
 * Full cleanup of a directory handle: unlocks (DIR_$UNLOCK_OBJ),
 * unmaps pages (DIR_$UNMAP_PAGES), frees the handle slot (DIR_$FREE_HANDLE),
 * and clears the handle pointer to NULL. No-op if handle is already NULL.
 *
 * Takes a pointer to the handle variable (not the handle itself),
 * so it can clear it after release.
 *
 * Original address: 0x00E4B9D6
 * Size: 44 bytes
 */
void dir_$release_handle(void *handle_ptr);

/* dir_$alloc_overflow_page - Allocate overflow page for link data
 * Original address: 0x00E4E960
 */
void dir_$alloc_overflow_page(status_$t *status_ret);

/* dir_$next_page - Advance to the next page in B-tree traversal
 * Original address: 0x00E4D7B0
 */
void dir_$next_page(void *handle, int16_t depth, void *extra, uint16_t *page_ret);

/* dir_$map_link_page - Map a link overflow page
 * Original address: 0x00E4D572
 */
void *dir_$map_link_page(void *handle, uint16_t page_idx);

/* dir_$get_parent_uid - Resolve parent UID of a directory
 * Original address: 0x00E4D060
 */
void dir_$get_parent_uid(uid_t *uid, status_$t *status_ret);

/* dir_$read_def_prot - Read default protection from directory page
 * Original address: 0x00E51C6A
 */
void dir_$read_def_prot(uint32_t handle, void *acl_type,
                        void *prot_buf, void *acl_uid, status_$t *status_ret);

/* dir_$write_def_prot - Write default protection to directory page
 *
 * Argument roles read off the prologue at 0x00E51E18-0x00E51E30:
 *   (0x08,A6) -> D4  handle
 *   (0x0C,A6) -> D5  acl_type, compared against ACL_$DIR_ACL (0x00E1744C) at
 *                    0x00E51F2A and ACL_$FILE_ACL (0x00E17444) at 0x00E51F7C
 *   (0x10,A6)        prot_data, copied as 11 longwords = 44 bytes by the loop
 *                    at 0x00E51E7C-0x00E51E88
 *   (0x14,A6) -> A2  src_acl_uid; funky test and.w (0x4,A2),D0w at 0x00E51E48,
 *                    8 bytes copied out of it at 0x00E51E8C on the plain path
 *   (0x18,A6) -> D3b flush_flag; FILE_$FW_PARTIAL only when negative
 *   (0x1A,A6) -> A3  status_ret
 *
 * Original address: 0x00E51E18
 */
void dir_$write_def_prot(uint32_t handle, void *acl_type,
                         void *prot_data, void *src_acl_uid, char flush_flag,
                         status_$t *status_ret);

/* dir_$remove_entry_from_page - Remove entry from its directory page
 * Original address: 0x00E50D5E
 */
void dir_$remove_entry_from_page(int16_t slot_idx, status_$t *status_ret);

/* dir_$set_default_acl_internal - Set default ACL on directory page
 *
 * Performs ACL conversion and storage on a directory's page data.
 * Called by dir_$do_op_set_default_acl. Converts the incoming ACL
 * to internal 10-ACL or funky-ACL format, validates that the ACL
 * object resides on the same volume, then copies ACL data into the
 * directory page at the appropriate offset (0x1A for DIR_ACL,
 * 0x4E for FILE_ACL). Updates the ACL UID at offset 0x46 or 0x7A.
 *
 * Handles both ACL_$DIR_ACL and ACL_$FILE_ACL types.
 *
 * Argument roles read off the prologue at 0x00E52D70-0x00E52D88:
 *   (0x08,A6) -> D4  handle
 *   (0x0C,A6) -> D5  acl_type, compared against ACL_$DIR_ACL (0x00E1744C) at
 *                    0x00E52E64 and ACL_$FILE_ACL (0x00E17444) at 0x00E52EB4
 *   (0x10,A6) -> A2  src_acl_uid; funky test and.w (0x4,A2),D0w at 0x00E52DA0
 *                    selects ACL_$CONVERT_FUNKY_ACL over ACL_$CONVERT_TO_10ACL
 *   (0x14,A6) -> D2b flush_flag; FILE_$FW_PARTIAL only when negative (0x00E52F34)
 *   (0x16,A6) -> A3  status_ret
 *
 * Original address: 0x00E52D70
 * Size: 566 bytes
 */
void dir_$set_default_acl_internal(uint32_t handle, void *acl_type,
                                   void *src_acl_uid, char flush_flag,
                                   status_$t *status_ret);

/* dir_$create_dir_obj - Create new directory file
 *
 * Creates a new directory file using FILE_$PRIV_CREATE, initializes
 * its structure, and returns the new directory's UID.
 *
 * Original address: 0x00E52394
 * Size: 482 bytes
 */
void dir_$create_dir_obj(uid_t *parent_uid, void *page0_data, uid_t *dir_acl_uid,
                         uid_t *file_acl_uid, uid_t *new_uid_ret, status_$t *status_ret);

/* dir_$add_entry - Core internal add entry to directory
 *
 * Validates entry name, checks for duplicates, handles overflow pages,
 * and inserts the entry into the directory pages.
 *
 * Original address: 0x00E4FE0A
 * Size: 232 bytes
 */
void dir_$add_entry(uint32_t handle, void *name, uint16_t name_len,
                    uint16_t entry_type, uint32_t extra, uid_t *uid,
                    uint16_t link_len, void *link_data, status_$t *status_ret);

/* dir_$truncate_pages - Truncate/resize directory pages
 * Original address: 0x00E4E90A
 * Size: 86 bytes
 */
uint32_t dir_$truncate_pages(void *handle, uint16_t new_page_count,
                             status_$t *status_ret);

/* dir_$find_entry - Directory entry lookup
 *
 * Looks up a directory entry by name using B-tree binary search.
 * Returns negative (char < 0) if found, non-negative if not found.
 *
 * Original address: 0x00E4C9E4
 * Size: 390 bytes
 */
char dir_$find_entry(void *handle, void *name, int16_t name_len,
                     int16_t flags, void **entry_ret,
                     void *extra, int16_t *depth_ret);

/* dir_$do_op_read_linku - DO_OP handler for read link
 * Original address: 0x00E4D5B4
 */
void dir_$do_op_read_linku(uid_t *uid, void *name, uint16_t name_len,
                           uint16_t buf_len, uint32_t extra, void *link_type_ret,
                           uid_t *uid_ret, status_$t *status_ret);

/* DIR_$OLD_DIR_READU - Legacy directory read
 * Original address: 0x00E57C80
 */
/* DIR_$OLD_DIR_READU - Legacy directory read
 * Checks for canned root, crashes if so, then calls dir_$old_read_entries (0x00e579c0).
 * Dereferences param_3 and param_4 before passing to helper.
 * Original address: 0x00E57C80
 */
void DIR_$OLD_DIR_READU(uid_t *uid, void *param_2, void *param_3,
                        void *param_4, void *param_5, void *param_6,
                        status_$t *status_ret);

/* DIR_$OLD_READ_INFOBLK - Read directory info block
 * Reads info block data from a directory, up to max_len bytes.
 * Original address: 0x00E560A6
 */
void DIR_$OLD_READ_INFOBLK(uid_t *dir_uid, void *info_data,
                            int16_t *max_len, int16_t *actual_len,
                            status_$t *status_ret);

/* DIR_$OLD_WRITE_INFOBLK - Write directory info block
 * Writes info block data to a directory.
 * Original address: 0x00E5613C
 */
void DIR_$OLD_WRITE_INFOBLK(uid_t *dir_uid, void *info_data,
                             int16_t *len, status_$t *status_ret);

/* DIR_$OLD_INIT - Legacy directory init
 * Original address: 0x00E314F4
 */
void DIR_$OLD_INIT(void);

/* DIR_$OLD_CLEANUP - Legacy directory cleanup
 * Original address: 0x00E54B2A
 */
void DIR_$OLD_CLEANUP(void);

/* dir_$old_read_entries - Internal directory read implementation
 *
 * Reads directory entries into a caller-provided buffer.
 * Iterates through inline entries and overflow chain entries,
 * unmaps case on names, and copies entry data (type, UID, name).
 * Called by DIR_$OLD_DIR_READU after root UID check.
 *
 * Original address: 0x00E579C0
 */
void dir_$old_read_entries(uid_t *uid, void *param_2, uint32_t param_3,
                           uint32_t param_4, void *param_5, void *param_6,
                           status_$t *status_ret);

/*
 * ============================================================================
 * Additional Internal Data
 * ============================================================================
 */

/* Directory operation parameter tables for SET_ACL */
#define DAT_00e7fcc2     (DIR_$OP_TAB[16].version)   /* SET_ACL type field */  /* 0x00E7FCC2 */
#define DAT_00e7fcc6     (DIR_$OP_TAB[16].base_size)   /* SET_ACL request size */  /* 0x00E7FCC6 */

/* Directory handle slot data base address: 0xE7DC00 */
extern uint32_t DAT_00e7fc3c;   /* Active slots bitmap */
extern uint32_t DAT_00e7fc34;   /* Additional bitmap */
extern uint32_t DAT_00e7f470;   /* Counter/flag */
extern uint32_t DAT_00e7fbf4;   /* Counter/flag */
extern uint32_t DAT_00e7f4b0;   /* Counter/flag */
extern void    *DAT_00e7fc30;   /* Free list head (handle entries) */
extern void    *DAT_00e7fc38;   /* Free list head (request buffers) */
extern uint8_t  DAT_00e7f280[]; /* Start of handle entry pool, 0x30 bytes per
                                 * entry, 0x00E7F280..0x00E7F470 */
extern uint8_t  DAT_00e7f4bc;   /* Start of request buffer pool */
extern uint16_t DAT_00e7fc40;   /* Link buffer mutex owner */

/* Event counters and mutexes */
extern ec_$eventcount_t DIR_$WAIT_ECS[];    /* Array of 32 event counters (base) */
extern ml_$exclusion_t  DIR_$MUTEX;         /* Directory exclusion mutex */
extern ml_$exclusion_t  DIR_$LINK_BUF_MUTEX;/* Link buffer mutex */
extern ec_$eventcount_t DIR_$WT_FOR_HDNL_EC;/* Wait-for-handle event counter */

/* Hint subsystem data */
/*
 * The two DIR_$OP_TAB fields DIR_$DO_OP reaches through A5 = 0x00E7DC00.
 * (0x1f9c,A0) is record + 0x02 and (0x1fa0,A0) record + 0x06, because the
 * record base is 0x00E7FB9A + (opcode >> 1) * 8 -- see DIR_$OP_REC above.
 *   DIR_$OP_REPLY_VERSION  0x00E4C0BA -> request + 0x12
 *                          0x00E4C188  reply + 0x0A must be <= it
 *                          0x00E4C25A -> reply + 0x0A on the local path
 *   DIR_$OP_REPLY_SIZE     0x00E4C254  added to the 0x14-byte reply header
 */
#define DIR_$OP_VERSION(half)    (DIR_$OP_REC(half).reply_version)
#define DIR_$OP_REPLY_SIZE(half) (DIR_$OP_REC(half).reply_size)

/*
 * The record + 0x00 word DIR_$SERVER checks the incoming request body
 * version against (`move.w (-0xa8,A0),D1w` / `cmp.w (0xe,A2),D1w` at
 * 0x00E58258, 0x00E5825C).  It is the same word the DIR_$<op>U client
 * wrappers store into request + 0x0E.
 */
#define DIR_$OP_REQ_VERSION(half) (DIR_$OP_REC(half).version)

/* 0x00E4B33C, longword 0x00000000.  Passed by reference as
 * FILE_$SET_REFCNT's refcnt (`move.l (A0),D0` at 0x00E5E40E) and as
 * AST_$PURIFY's segment_list (0x00E4B2A4). */
extern uint32_t DAT_00e4b33c;

/* 0x00E4B444, word 0x0001 - MST remap / ACL check parameter, read as a
 * word (`btst.b #0,(1,A0)` in FILE_$GET_ATTRIBUTES at 0x00E5D99E). */
extern uint16_t DAT_00e4b444;

/*
 * Constant status cells CRASH_SYSTEM is handed by `pea (d,PC)`.
 * 0x00e4b230 holds the longword 0x000E0025 (status_$naming, subsys 0x0E,
 * mod 0x00, code 0x25), so these are status_$t cells in the code region,
 * not strings.  0x00e7dbfc is a pointer cell holding 0x00e4b230.
 */
extern status_$t *PTR_Naming_bad_request_header_ver_err_00e7dbfc;
extern status_$t  Naming_bad_request_header_ver_err;
/* Alias for crash in OLD_DIR_READU */
extern status_$t Bad_request_header_version_err;

/*
 * OLD directory subsystem data area
 *
 * Base address: 0xE7FD24 (runtime, A5-relative in OLD functions).  The
 * DIR_$OLD_* entry points share this module data area with NAME_$LOCK_DIR /
 * NAME_$UNLOCK_DIR, so the per-process lock tables are declared once in
 * name/name.h (NAME_$LOCK_SLOT / _MODE / _HANDLE / _UID) and defined in
 * name/name_data.c.  What the OLD directory code calls "the slot handle
 * pointer" at 0x2B8 + i*8 is the high longword of NAME_$LOCK_UID[i]: a
 * non-zero value means process i holds a directory lock.
 */
#define DIR_OLD_NUM_SLOTS NAME_$MAX_LOCK_PROCS  /* dbf 0x39 = 58 iterations */
#define DIR_OLD_HANDLE_OFFSET 0x2B8

/*
 * Status codes used by OLD functions.
 *
 * Every 0x000Exxxx (naming server) code now lives in name/name.h, which this
 * header includes; the guarded copies that used to sit here disagreed with
 * name/name.h about status_$naming_object_is_not_an_acl_object and were
 * silently overridden by it (source-pp31).
 */
/* status_$directory_is_full now lives in dir/dir.h */
/* status_$no_right_to_perform_operation / 
 * status_$insufficient_rights_to_perform_operation are module-0x23 (ACL)
 * codes and are defined once in acl/acl.h, included above (bead source-3uo). */
#ifndef file_$objects_on_different_volumes
#define file_$objects_on_different_volumes           0x000F0013
#endif

/* DIR_OP_SET_ACL and DIR_OP_GET_ENTRYU operation codes */
#ifndef DIR_OP_SET_ACL
#define DIR_OP_SET_ACL       0x4A
#endif
#ifndef DIR_OP_GET_ENTRYU_OP
#define DIR_OP_GET_ENTRYU_OP 0x44
#endif

/*
 * ACL_$DIRIN_ACL, ACL_$DEFAULT_ACL, ACL_$DNDCAL, ACL_$FNDWRX and
 * ACL_$DEF_ACLDATA come from acl/acl.h; ACL_TYPE_DIR / ACL_TYPE_FILE (object
 * type words in the NAME code region) come from name/name.h.
 */

/*
 * Externs for functions/data already declared in included headers
 * (file.h, file_internal.h, ml.h, ast.h, proc1.h, etc.)
 * are NOT re-declared here to avoid conflicts.
 *
 * These are accessible via the #include chain:
 *   FILE_$SET_ACL, FILE_$SET_PROT, FILE_$GET_ATTRIBUTES,
 *   FILE_$SET_REFCNT, FILE_$FW_FILE, FILE_$FW_PARTIAL,
 *   FILE_$DELETE_OBJ, FILE_$TRUNCATE, FILE_$PRIV_CREATE,
 *   FILE_$PRIV_LOCK, FILE_$PRIV_UNLOCK,
 *   ML_$EXCLUSION_INIT, ML_$EXCLUSION_STOP,
 *   MST_$UNMAP, MST_$UNMAP_PRIVI, MST_$MAPS,
 *   AST_$GET_LOCATION, AST_$GET_COMMON_ATTRIBUTES, AST_$SET_ATTRIBUTE,
 *   AUDIT_$ENABLED, PROC1_$AS_ID, PROC1_$CURRENT, PROC1_$TYPE,
 *   NODE_$ME, HINT_$GET_HINTS, HINT_$ADDI,
 *   REM_FILE_$RN_DO_OP, MAP_CASE, UNMAP_CASE, CRASH_SYSTEM
 */

/* NAME_CONVERT_ACL_STATUS is declared in name/name.h, included above. */

/*
 * ACL_$RIGHTS, REM_FILE_$DROP_HARD_LINKU, etc. are already
 * declared in acl/acl.h, rem_file/rem_file.h (included via
 * file/file_internal.h). dir_$find_uid_internal is declared earlier
 * in this file. No need to re-declare.
 */

/* REM_FILE_$SET_DEF_ACL is declared in rem_file/rem_file.h and
 * REM_NAME_$GET_ENTRY in name/name.h (both included above). */

/* dir_$do_op_add_link - DO_OP handler for add entry/hard link
 *
 * Server-side handler for remote add (op 0x2A) and add hard link (op 0x2C).
 * Validates ACL rights, checks link count (max 0xFFF5), increments link
 * count attribute. On failure, calls dir_$do_op_drop_entry to undo.
 *
 * Original address: 0x00E5044A
 * Size: 378 bytes
 */
void dir_$do_op_add_link(uid_t *uid, void *name, uint16_t name_len, uid_t *file_uid,
                         uint16_t flags, status_$t *status_ret);
/* dir_$do_op_add_entry - DO_OP add entry with idempotent handling
 *
 * General add entry handler for remote directory operations. Enters super
 * mode, looks up directory, attempts to add entry. If name already exists
 * and current process type is 9, compares existing entry to verify match
 * (idempotent add). Updates root hints when adding to NAME_$ROOT_UID.
 *
 * Original address: 0x00E4FEF2
 * Size: 454 bytes
 */
void dir_$do_op_add_entry(uid_t *uid, uint16_t type, void *name, uint16_t name_len,
                          uint16_t entry_type, uint32_t extra, void *uid_data,
                          uint16_t target_len, uint32_t target_data,
                          void *result, status_$t *status_ret);
/* dir_$do_op_delete - DO_OP handler for delete/drop operations
 *
 * Server-side handler for delete file (ops 0x2E, 0x36) and drop hard link
 * (op 0x30). Validates rights, checks entry type (not a file error),
 * locks file via FILE_$PRIV_LOCK, calls FILE_$DELETE_OBJ, sets attributes.
 *
 * Original address: 0x00E5125E
 * Size: 860 bytes
 */
/* The three flags are Domain BOOLEANS occupying the even byte of a 2-byte
 * slot each (A6+0x12, +0x14, +0x16); every test on them is `tst.b` + `bmi` /
 * `bpl` (0x00E51310, 0x00E51346, 0x00E514F2), a SIGNED test. */
void dir_$do_op_delete(uid_t *dir_uid, void *name, uint16_t name_len,
                       boolean check_del_right, boolean entry_only,
                       boolean allow_link, uid_t *entry_uid_ret,
                       uid_t *deleted_uid_ret, status_$t *status_ret);
/* Frame at 0x00E518BC: 0x08 uid, 0x0C word (pushed by DIR_$DO_OP from
 * request+0x0E at 0x00E4C466 but never read by the callee), 0x0E old_name,
 * 0x12 old_name_len, 0x14 new_name, 0x18 new_name_len, 0x1A status_ret. */
void dir_$do_op_cname(uid_t *uid, uint16_t req_version,
                      void *old_name, uint16_t old_name_len,
                      void *new_name, uint16_t new_name_len,
                      status_$t *status_ret);
void dir_$do_op_add_bak(uid_t *uid, uint16_t type, void *name_ptr, uint16_t name_len,
                        void *uid_data, uid_t *result_uid, status_$t *status_ret);
/* dir_$do_op_create_dir - DO_OP handler: create subdirectory
 *
 * Creates a new subdirectory within a parent directory. Opens the
 * parent, reads its ACL UIDs, calls dir_$create_dir_obj to create and
 * initialize the new directory object, then adds the name entry.
 * On name_already_exists for server processes (type 9), performs
 * idempotent lookup of the existing entry.
 *
 * Called by DIR_$DO_OP case 0x38. Audit code 0x16.
 *
 * Original address: 0x00E52576
 * Size: 462 bytes
 */
void dir_$do_op_create_dir(uid_t *uid, void *name, uint16_t name_len,
                           void *result_uid, status_$t *status_ret);

/* dir_$do_op_drop_dir - DO_OP handler: drop/delete directory entry
 *
 * Removes a directory entry. Looks up the entry by name, rejects
 * link entries (type 4), checks the directory lock list, verifies
 * ACL rights. If the target is a directory on the same volume,
 * checks it's empty (1 entry only), truncates ACL objects, and
 * deletes the directory object. Falls back to DIR_$OLD_DROP_DIRU
 * on bad_directory errors.
 *
 * Called by DIR_$DO_OP case 0x3A. Audit code 0x17.
 *
 * Original address: 0x00E52744
 * Size: 682 bytes
 */
void dir_$do_op_drop_dir(uid_t *uid, void *name, uint16_t name_len,
                         status_$t *status_ret);
void dir_$do_op_drop_entry(uid_t *uid, uint16_t rights, void *name,
                           uint16_t name_len, uint16_t entry_type,
                           void *result_uid, status_$t *status_ret);
void dir_$do_op_dir_readu(uid_t *uid, int16_t version, char *name,
                          uint16_t name_flags, void *cont, uint16_t max_entries,
                          uint32_t max_size, uint32_t buf_size,
                          void *size_ret, void *offset_ret,
                          void *count_ret, status_$t *status_ret);
void dir_$do_op_get_entryu(uid_t *uid, void *name, uint16_t name_len,
                           uint16_t *type_ret, uid_t *uid_ret,
                           uint32_t *extra_ret, status_$t *status_ret);
void dir_$do_op_find_uid(uid_t *uid, uid_t *target_uid, int8_t flag,
                         void *name_ret, void *len_ret, void *uid_ret,
                         status_$t *status_ret);
void dir_$do_op_fix_dir(uid_t *uid, status_$t *status_ret);
/* dir_$do_op_set_acl - DO_OP handler: set ACL (opcode 0x4A)
 *
 * 0x00E52BC2 is the SERVER-side handler, not DIR_$SET_ACL.  DIR_$SET_ACL is
 * the client-side request builder at 0x00E52C86 (dir/set_acl.c, declared in
 * dir.h).  Sole caller 0x00E4C78E passes the resolved object UID, the funky
 * ACL UID at request+0x8E and response+0x04 as the status.
 *
 * Original address: 0x00E52BC2
 * Size: 196 bytes
 */
void dir_$do_op_set_acl(uid_t *uid, uid_t *acl_uid, status_$t *status_ret);
/* dir_$do_op_set_default_acl - DO_OP handler: set default ACL (opcode 0x4C)
 *
 * Call site 0x00E4C794 pushes, right to left, pea (0x4,A3) / pea (0x96,A2) /
 * pea (0x8e,A2) / pea (-0x10,A6), so acl_type = request+0x8E (8-byte ACL type
 * UID) and src_acl_uid = request+0x96 (8-byte source ACL UID).  Both are
 * forwarded in that order to dir_$set_default_acl_internal at 0x00E52FDC and
 * 0x00E52FD8, which is where the two roles are pinned.
 *
 * Original address: 0x00E52FA6
 * Size: 94 bytes
 */
void dir_$do_op_set_default_acl(uid_t *dir_uid, void *acl_type, void *src_acl_uid,
                                status_$t *status_ret);
void dir_$do_op_get_default_acl(uid_t *uid, uid_t *type, uid_t *acl_ret, status_$t *status_ret);
void dir_$do_op_validate_root_entry(void *name, uint16_t name_len, status_$t *status_ret);
void dir_$do_op_set_prot(uid_t *uid, void *prot_data, void *acl_uid,
                         int16_t prot_type, status_$t *status_ret);
/* dir_$do_op_set_def_prot - DO_OP handler: set default protection (opcode 0x54)
 *
 * Call site 0x00E4C81C pushes, right to left, pea (0x4,A3) / pea (0xc2,A2) /
 * pea (0x96,A2) / pea (0x8e,A2) / pea (-0x10,A6), so acl_type = request+0x8E
 * (8-byte ACL type UID), prot_data = request+0x96 (44 bytes, 0x96..0xC1) and
 * src_acl_uid = request+0xC2 (8-byte source ACL UID).  All three are forwarded
 * in that order to dir_$write_def_prot at 0x00E5207E / 0x00E5207A / 0x00E52076.
 *
 * Original address: 0x00E52044
 * Size: 98 bytes
 */
void dir_$do_op_set_def_prot(uid_t *dir_uid, void *acl_type, void *prot_data,
                             void *src_acl_uid, status_$t *status_ret);
void dir_$do_op_get_def_prot(uid_t *uid, void *acl_type, void *prot_buf,
                             void *acl_ret, status_$t *status_ret);
void dir_$do_op_resolve(uint32_t path_data, uint16_t path_len, void *result,
                        uint32_t *extra_ret, uint32_t *parent_uid_ret,
                        uint8_t *flags1, uint8_t *flags2,
                        uint16_t *cont, uint16_t *size,
                        uint16_t *last_start, uint16_t *last_size,
                        uint32_t max, uint16_t *link_count,
                        status_$t *status_ret);
void dir_$do_op_add_mount(uid_t *uid, uid_t *mount_uid, uint32_t node_id, status_$t *status_ret);
void dir_$do_op_drop_mount(uid_t *mount_uid, uint32_t node_id, status_$t *status_ret);

/* Info block parameter cells in the OLD DIR code region.  Widths taken
 * from the reads in each callee:
 *   0x00E56094  word 0x0090  FILE_$GET_ATTRIBUTES size_ptr (`cmpi.w #0x90,(A0)`
 *                            at 0x00E5D9F6)
 *   0x00E56096  word 0x0028  DIR_$OLD_READ_INFOBLK max_len (`move.w (A3),D0w`
 *                            at 0x00E560F2)
 *   0x00E56098  word 0x0004  FILE_$GET_ATTRIBUTES flags
 *   0x00E5609E  long 0x00010000  MST_$UNMAP map_info (`move.l (A0),-(SP)`
 *                            at 0x00E44748)
 */
extern int16_t  DAT_00e56096;
extern uint16_t DAT_00e56098;
extern int16_t  DAT_00e56094;
extern uint32_t DAT_00e5609e;
extern uint8_t DAT_00e560a2;
extern uint8_t DAT_00e5609a;
/* 0x00E564E2, longword 0x00000400: FILE_$FW_PARTIAL byte_count
 * (`move.l (A1),D2` at 0x00E5E6BC). */
extern uint32_t DAT_00e564e2;
/* 0x00E5716A, word 0x0006: FILE_$SET_PROT prot_type
 * (`move.w (A4),D2w` at 0x00E5DF56). */
extern uint16_t DAT_00e5716a;
/* NAME_$CONST_ZERO_L (NAME code region) is declared in name/name.h.
 *
 * The ACL_$RIGHTS constant cells 0xE4BC24, 0xE4CFF4, 0xE4CFF6, 0xE50C5C,
 * 0xE505C4, 0xE51B64, 0xE54B28, 0xE56946, 0xE564DE, 0xE5716C and 0xE5755E
 * are now modelled as typed file statics next to the call site that reads
 * them; 0xE4BC24, 0xE4CFF4, 0xE54B28 and 0xE5716C are Domain BOOLEANS
 * (ACL_$RIGHTS' ignore_super argument), not rights masks. */

/* ACL_$NIL (0xE17384) is declared in acl/acl.h */

/*
 * ============================================================================
 * B-tree Directory Entry Functions
 * ============================================================================
 */

/* DIR_$NAME_OFFSET_TABLE - Name offset by entry type (indexed by type & 7).
 * 0x00E7FC00 = A5+0x2000 with A5 = 0x00E7DC00; the next map symbol,
 * DIR_$ENTRY_CACHE_TOO_LONG_NAME, is at 0x00E7FC10, so the table is exactly
 * the eight words the `& 7` index reaches.  Gives the byte offset from the
 * entry start to its name field for each directory entry type.
 */
extern int16_t DIR_$NAME_OFFSET_TABLE[8];

/* DIR_$CLEANUP is declared in dir/dir.h (also used by NAME_$CLEANUP) */

/* dir_$get_entry_cached - Cached directory entry lookup
 *
 * Per-UID hash cache (modulo 111, 40-byte entries) for accelerating
 * repeated directory entry lookups. On cache miss, calls dir_$lookup_entry.
 *
 * Originally a nested Pascal subprocedure of dir_$do_op_get_entryu.
 *
 * Original address: 0x00E4CD90
 * Size: 612 bytes
 */
void dir_$get_entry_cached(uid_t *uid, void *name, uint16_t name_len,
                           uint16_t *type_ret, uid_t *uid_ret,
                           uint32_t *extra_ret, status_$t *status_ret);

/* dir_$lookup_entry - Uncached directory entry lookup
 *
 * Opens the directory, calls dir_$find_entry, interprets the result by
 * entry type (2=file, 3=hard link, 4=soft link). For root directory,
 * queries remote nodes on miss.
 *
 * Originally a nested Pascal subprocedure of dir_$do_op_get_entryu.
 *
 * Original address: 0x00E4CB6A
 * Size: 538 bytes
 */
void dir_$lookup_entry(uid_t *uid, void *name, uint16_t name_len,
                       uint16_t *type_ret, uid_t *uid_ret,
                       uint32_t *extra_ret, uint8_t *found_ret,
                       status_$t *status_ret);

/* dir_$refind_entry - Re-find current B-tree position after page navigation
 *
 * After processing entries on a page, re-establishes the cursor position
 * in the B-tree by calling dir_$find_entry with the last-seen entry name.
 *
 * Originally a nested Pascal subprocedure of dir_$do_op_dir_readu.
 *
 * Original address: 0x00E4D8AA
 * Size: 170 bytes
 */
void dir_$refind_entry(uint32_t local_handle, uint8_t *page_data,
                       uint8_t *idx_base, void **entry_ptr_ret,
                       void **entry_name_ret, void *extra_array,
                       int16_t *depth_ret, int16_t num_entries,
                       int8_t *eof_ret);

/* dir_$insert_entry - Core B-tree entry insertion
 *
 * Inserts a new entry into a directory's B-tree page structure. Handles:
 *   - Finding space, compacting dead entries, page splitting
 *   - Recursive insertion when splits propagate up the B-tree
 *   - Root page split (creating internal nodes)
 *
 * Originally a nested Pascal subprocedure of dir_$add_entry.
 *
 * Parameters:
 *   ctx        - Shared insertion context (parent frame state)
 *   slot_idx   - Current B-tree level for insertion
 *   param_2    - Recursive parameter (link data offset or 0)
 *   name_len   - Entry name length
 *   status_ret - Output: status code
 *
 * Original address: 0x00E4F3BA
 * Size: 2640 bytes
 */
void dir_$insert_entry(dir_insert_ctx_t *ctx, int16_t slot_idx,
                       uint32_t param_2, int16_t name_len,
                       status_$t *status_ret);

/* dir_$add_bak_default_prot - Add backup entry with default file protection
 *
 * Reads default FILE ACL, sets protection on the backup file,
 * then adds a type-3 entry and writes the file.
 *
 * Originally a nested Pascal subprocedure of dir_$do_op_add_bak.
 *
 * Original address: 0x00E50790
 * Size: 160 bytes
 */
void dir_$add_bak_default_prot(uint32_t local_handle, uid_t *uid,
                               void *name_ptr, uint16_t name_len,
                               uid_t *backup_uid, status_$t *status_ret,
                               char *rollback_flag);

/*
 * ============================================================================
 * B-tree Helper Functions (nested procedures of dir_$add_entry)
 *
 * These are nested Pascal subprocedures that access the shared insertion
 * context via the parent frame pointer. In the C flattening, they take
 * a dir_insert_ctx_t pointer as their first parameter.
 *
 * TODO(source-qvt): These are declared but not yet emitted as C code. They will
 * appear as undefined references until analyzed and implemented.
 * ============================================================================
 */

/* dir_$calc_entry_size - Compute aligned size of a directory entry
 * Original address: 0x00E4EF42, 58 bytes
 */
uint16_t dir_$calc_entry_size(uint8_t *entry);

/* dir_$move_entries_to_page - Move entries between pages during split
 * Original address: 0x00E4EF7C, 184 bytes
 */
void dir_$move_entries_to_page(dir_insert_ctx_t *ctx,
                               int16_t from_idx, int16_t to_idx);

/* dir_$write_entry_to_page - Write/insert entry data into a page
 *
 * Writes a new directory entry to a page including type-specific header,
 * UID/link data, and entry name. For internal pages, sets the child page
 * pointer from the split_pages array. For leaf pages, initializes the
 * header fields based on entry type (2=file, 3=hard link, 4=soft link).
 *
 * The page_ptr_ref is a pointer to either ctx->new_page or ctx->page_data;
 * the function re-reads it after operations that may invalidate page mappings.
 *
 * In the original M68K code, name_len, aligned_size, and src_name_loc were
 * accessed from the parent frame (insert_entry's stack). In the C flattening,
 * they are passed explicitly.
 *
 * Parameters:
 *   ctx          - Shared insertion context
 *   flag         - If negative (0xFF), use split_pages[page_count+2] for
 *                  internal child pointer; if >= 0, use [page_count+1]
 *   page_ptr_ref - Pointer to page data pointer (e.g., &ctx->new_page)
 *   count        - 1-based entry position in the index table
 *   name_len     - Entry name length (from insert_entry's parameter)
 *   aligned_size - Aligned total entry size (from insert_entry's computation)
 *   src_name_loc - Source page/offset for name copy (0 = use ctx->name,
 *                  non-zero = (page_num << 10) | offset_within_page)
 *
 * Original address: 0x00E4F100, 384 bytes
 */
void dir_$write_entry_to_page(dir_insert_ctx_t *ctx, uint8_t flag,
                              uint8_t **page_ptr_ref, int16_t count,
                              int16_t name_len, uint16_t aligned_size,
                              uint32_t src_name_loc);

/* dir_$copy_name_cross_page - Copy entry name between mapped pages
 *
 * Copies byte_count bytes from a source page at src_offset to a destination
 * page at dest_offset. Uses a 32-byte intermediate buffer to handle the case
 * where mapping one page invalidates the other (shared map slots). Maps source
 * and destination pages via dir_$map_page in 32-byte chunks.
 *
 * In the original M68K code, this was a nested procedure that accessed
 * ctx->handle via the grandparent frame pointer chain. In the C flattening,
 * handle is passed explicitly.
 *
 * Original address: 0x00E4F034, 204 bytes
 */
void dir_$copy_name_cross_page(uint32_t handle, int16_t src_page,
                               int16_t src_offset, int16_t dest_page,
                               int16_t dest_offset, int16_t byte_count);

/* dir_$compact_page_entries - Compact/reclaim dead entry space on a page
 * Original address: 0x00E4F2DA, 224 bytes
 */
void dir_$compact_page_entries(dir_insert_ctx_t *ctx);

/* dir_$alloc_split_page - Allocate pages for B-tree splitting
 *
 * Allocates page numbers for B-tree page splitting during directory entry
 * insertion. Scans for free pages in the gap at end of directory, then in
 * the segment map bitmap, extending the directory if needed. Copies B-tree
 * path pages to the newly allocated pages and generates a new directory UID.
 *
 * flag=0xFF for root split (allocates 2 extra pages), 0x00 for non-root.
 * slot_idx and base_offset come from insert_entry's scope (originally
 * accessed via the parent Pascal frame pointer).
 *
 * Original address: 0x00E4EB40, 906 bytes
 */
void dir_$alloc_split_page(dir_insert_ctx_t *ctx, uint8_t flag,
                           int16_t slot_idx, int16_t base_offset,
                           status_$t *status_ret);

/* dir_$purify_split_pages - Sort and purify allocated split pages
 *
 * Sorts split_pages[1..page_count] in descending order, then calls
 * AST_$PURIFY to flush the pages. Called by both alloc_split_page
 * and finalize_split.
 *
 * Original address: 0x00E4EA9C, 164 bytes
 */
void dir_$purify_split_pages(dir_insert_ctx_t *ctx, status_$t *status_ret);

/* dir_$finalize_split - Finalize page split, update parent arrays
 * Original address: 0x00E4EECA, 120 bytes
 */
void dir_$finalize_split(dir_insert_ctx_t *ctx, status_$t *status_ret);

/*
 * ============================================================================
 * Constants in the DIR code region passed by reference (Pascal VAR args)
 * ============================================================================
 *
 * These are literal values embedded in the code segment next to the routines
 * that use them; Ghidra labels them DAT_<address>.  They are declared with
 * the access width used by the code.
 */
extern uint16_t DAT_00e50830;   /* 0xE50830: 0x0005 - FILE_$SET_PROT protection type (add_bak) */
extern uint16_t DAT_00e50c5a;   /* 0xE50C5A: 0x0000 - ACL option flags / DROP_HARD_LINKU flags */
extern int16_t  DAT_00e4dffa;   /* 0xE4DFFA: word 0x0090 - FILE_$GET_ATTRIBUTES size_ptr
                                 * (`cmpi.w #0x90,(A0)` at 0x00E5D9F6) */
extern uint8_t  DAT_00e4dffc;   /* 0xE4DFFC: NUL byte used as the 1-char name "\0" */
extern uint8_t  PTR_DAT_00e4cd84; /* 0xE4CD84: case-folding character bitmap (07 ff ff fe ...) */
extern uint32_t DAT_00e4b448;   /* 0xE4B448: longword 0x00008000 - MST_$REMAP_PRIVI
                                 * length parameter.  It is read as a longword,
                                 * so the cell is four bytes (bead source-wk2f). */
extern const int32_t DAT_00e52040; /* 0xE52040: 0x00000400 - one page; FILE_$FW_PARTIAL byte
                                      count / FILE_$TRUNCATE length (defined in dir_data.c) */

/*
 * A5-relative globals (A5 = 0xE35040 in the DIR/NAME code) used when not
 * compiled for the m68k, where they are read through __A5_BASE().
 */
extern uint16_t DAT_a5_2042;    /* A5+0x2042: request type field for DIR_$ADD_ENTRY_INTERNAL */
extern int16_t  DAT_a5_2046;    /* A5+0x2046: base request length for DIR_$ADD_ENTRY_INTERNAL */

#endif /* DIR_INTERNAL_H */
