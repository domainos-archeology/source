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
 * the raw view spans 0x14..0x47.
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
        /* The same three fields named.  DIR_$GET_ENTRYU_FUN (0x00E4D460)
         * copies them straight into a dir_$old_entry_t at 0x00E4D4D8
         * (word from reply+0x14), 0x00E4D4E4 (the uid at reply+0x16, two
         * longwords) and 0x00E4D4F0 (the longword at reply+0x1E). */
        struct __attribute__((packed, aligned(2))) {
            uint16_t word;      /* 0x14: entry type, or a name length */
            uid_t    uid;       /* 0x16 */
            uint32_t extra;     /* 0x1E */
        } entry;
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
        uint8_t  raw[0x34];     /* 0x14..0x47 raw view */
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
_Static_assert(__builtin_offsetof(Dir_$OpResponse, entry.word)  == 0x14, "Dir_$OpResponse.entry.word");
_Static_assert(__builtin_offsetof(Dir_$OpResponse, entry.uid)   == 0x16, "Dir_$OpResponse.entry.uid");
_Static_assert(__builtin_offsetof(Dir_$OpResponse, entry.extra) == 0x1E, "Dir_$OpResponse.entry.extra");
_Static_assert(__builtin_offsetof(Dir_$OpResponse, resolve.start_uid) == 0x16, "resolve.start_uid");
_Static_assert(__builtin_offsetof(Dir_$OpResponse, resolve.resolved_uid) == 0x1E, "resolve.resolved_uid");
_Static_assert(__builtin_offsetof(Dir_$OpResponse, resolve.param5) == 0x26, "resolve.param5");
_Static_assert(__builtin_offsetof(Dir_$OpResponse, resolve.link_count) == 0x2E, "resolve.link_count");
_Static_assert(__builtin_offsetof(Dir_$OpResponse, resolve.redirect) == 0x30, "resolve.redirect");
/* DIR_$GET_DEF_PROTECTION (0x00E51D9E) asks for a 0x48-byte reply and reads
 * the protection uid out of reply+0x40 (`lea (-0x10,A6),A0` with the reply
 * based at A6-0x50, 0x00E51E00), so the record runs 0x00..0x47. */
_Static_assert(sizeof(Dir_$OpResponse) == 0x48, "Dir_$OpResponse spans 0x14..0x47 of payload");
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
 * ============================================================================
 * dir_$do_op_request_t - the request buffer handed to DIR_$DO_OP (0x00E4C02C)
 * ============================================================================
 *
 * Every DIR_$<op>U client wrapper builds its request in a stack frame and
 * hands the *base* of that buffer to DIR_$DO_OP as parameter 1.  The image
 * sites agree byte for byte on the fixed part; the addresses below are the
 * A6 displacements of DIR_$DROP_LINKU (0x00E517F6, request base A6-0x1B0):
 *
 *   0x00  three bytes no builder ever writes (part of the REM_FILE request
 *         header REM_FILE_$RN_DO_OP fills in downstream)
 *   0x03  op          `move.b #0x40,(-0x1ad,A6)`        0x00E51846
 *   0x04  uid         `move.l (A0)+,(-0x1ac,A6)` x2     0x00E5184E
 *   0x0C  two bytes no builder writes
 *   0x0E  version     `move.w (0x209a,A5),(-0x1a2,A6)`  0x00E51856
 *                     = DIR_$OP_REC(op >> 1).version
 *   0x10  two bytes no builder writes
 *   0x12  reply_version - written by DIR_$DO_OP itself from the same table
 *         record's +0x02 word (`move.w (0x1f9c,A0),(0x12,A2)` 0x00E4C0BA)
 *   0x14  0x7A bytes of REM_FILE request header, untouched here
 *   0x8E  operation body (the variant below)
 *
 * DIR_$DO_OP's second argument is the *body* size; it adds 0x8E
 * (`addi.w #0x8e,D1w` at 0x00E4C110) to reach the wire length.  Each builder
 * computes that body size as DIR_$OP_REC(op >> 1).base_size plus whatever
 * variable-length text follows, so base_size is exactly the fixed part of the
 * body and the text starts at 0x8E + base_size.
 *
 * NOTE ON SIZE: in the image each builder's frame holds only as much of the
 * body as its own operation needs (0x98 bytes for DIR_$SET_ACL, 0x298 for
 * DIR_$CNAMEU).  The union below is sized for the largest variant, so a C
 * frame is bigger than the original; every field offset that the image
 * actually writes is preserved exactly.
 */

/* op 0x2A ADDU / ROOT_ADDU - DIR_$ADD_ENTRY_INTERNAL 0x00E500B8.
 * base_size 0x0E (DIR_$OP_TAB[0].base_size, 0x00E7FC46). */
typedef struct __attribute__((packed, aligned(2))) dir_$req_add_entry_t {
    uint16_t path_len;                  /* +0x8E  0x00E500EC */
    uid_t    file_uid;                  /* +0x90  0x00E50122 */
    uint32_t flags;                     /* +0x98  0x00E5012A */
    char     name[DIR_MAX_LEAF_LEN];    /* +0x9C  0x00E500FE */
} dir_$req_add_entry_t;

/* op 0x32 CNAMEU - DIR_$CNAMEU 0x00E51B68.
 * base_size 0x04 (DIR_$OP_TAB[4].base_size, 0x00E7FC66), so the old name
 * starts at 0x8E + 4 = 0x92 and the new name runs straight on from it:
 * 0x00E51BE0 forms the destination index as
 *   i + DIR_$OP_TAB[4].base_size + old_len   (relative to +0x8E). */
typedef struct __attribute__((packed, aligned(2))) dir_$req_cname_t {
    uint16_t old_len;                       /* +0x8E  0x00E51BB0 */
    uint16_t new_len;                       /* +0x90  0x00E51BCE */
    char     name[2 * DIR_MAX_LEAF_LEN];    /* +0x92  old name then new name */
} dir_$req_cname_t;

/* op 0x3C ADD_LINKU - DIR_$ADD_LINKU 0x00E5068E.
 * base_size 0x08 (DIR_$OP_TAB[9].base_size, 0x00E7FC8E).  The builder stores
 * the caller's target POINTER, never the target text (0x00E5071A
 * `move.l (0x14,A6),(-0x11e,A6)`); only the link name is copied inline. */
typedef struct __attribute__((packed, aligned(2))) dir_$req_add_link_t {
    uint16_t path_len;                  /* +0x8E  0x00E506FC */
    uint16_t target_len;                /* +0x90  0x00E50716 */
    uint32_t target_ptr;                /* +0x92  0x00E5071A */
    char     name[DIR_MAX_LEAF_LEN];    /* +0x96  0x00E5070A */
} dir_$req_add_link_t;

/* ops whose body is just a leaf name: 0x38 CREATE_DIRU (0x00E529EE) and
 * 0x40 DROP_LINKU (0x00E517F6), both base_size 0x02. */
typedef struct __attribute__((packed, aligned(2))) dir_$req_name_t {
    uint16_t path_len;                  /* +0x8E */
    char     name[DIR_MAX_LEAF_LEN];    /* +0x90 */
} dir_$req_name_t;

/* ops whose body is a leaf name preceded by a uid: 0x2C ADD_HARD_LINKU
 * (DIR_$ADD_HARD_LINKU 0x00E505CA, base_size 0x0A = DIR_$OP_TAB[1]) and
 * 0x34 ADD_BAKU (DIR_$ADD_BAKU 0x00E50C60, base_size 0x0A = DIR_$OP_TAB[5]).
 * 0x8E + 0x0A = 0x98, exactly where both name copies land. */
typedef struct __attribute__((packed, aligned(2))) dir_$req_uid_name_t {
    uint16_t path_len;                  /* +0x8E */
    uid_t    target_uid;                /* +0x90 */
    char     name[DIR_MAX_LEAF_LEN];    /* +0x98 */
} dir_$req_uid_name_t;

/* op 0x2E DROP_HARD_LINKU - DIR_$DROP_HARD_LINKU 0x00E516FC, base_size 0x04
 * (DIR_$OP_TAB[2]).  The word at +0x90 is the caller's flags word
 * (`move.w (A0),(-0x128,A6)` at 0x00E51764). */
typedef struct __attribute__((packed, aligned(2))) dir_$req_word_name_t {
    uint16_t path_len;                  /* +0x8E */
    uint16_t flags;                     /* +0x90 */
    char     name[DIR_MAX_LEAF_LEN];    /* +0x92 */
} dir_$req_word_name_t;

/*
 * dir_$find_uid_result_t - the 0x30-byte record REM_NAME_$FIND_UID
 * (0x00E4ADD6) and REM_NAME_$FIND_NETWORK (0x00E4AE84) fill in for
 * dir_$do_op_find_uid.  Its frame cell is A6-0x48 (`pea (-0x48,A6)` at
 * 0x00E4E632 and 0x00E4E65A); the consumers address
 *   (-0x46,A6) name_len, (-0x44,A6) name, (-0x24,A6) extra,
 *   (-0x1c,A6) node_id.
 */
typedef struct __attribute__((packed, aligned(2))) dir_$find_uid_result_t {
    uint16_t word0;         /* 0x00: never read by dir_$do_op_find_uid */
    uint16_t name_len;      /* 0x02 */
    uint8_t  name[0x20];    /* 0x04 */
    uint8_t  extra[8];      /* 0x24 */
    uint32_t node_id;       /* 0x2C */
} dir_$find_uid_result_t;

_Static_assert(__builtin_offsetof(dir_$find_uid_result_t, name_len) == 0x02,
               "dir_$find_uid_result_t.name_len");
_Static_assert(__builtin_offsetof(dir_$find_uid_result_t, name) == 0x04,
               "dir_$find_uid_result_t.name");
_Static_assert(__builtin_offsetof(dir_$find_uid_result_t, extra) == 0x24,
               "dir_$find_uid_result_t.extra");
_Static_assert(__builtin_offsetof(dir_$find_uid_result_t, node_id) == 0x2C,
               "dir_$find_uid_result_t.node_id");
_Static_assert(sizeof(dir_$find_uid_result_t) == 0x30,
               "dir_$find_uid_result_t is 0x30 bytes");

/* op 0x36 DELETE_FILEU - DIR_$DELETE_FILEU 0x00E515BC, base_size 0x04
 * (DIR_$OP_TAB[6]).  The two bytes at +0x90/+0x91 come from two by-reference
 * boolean/flag parameters (`move.b (A0),(-0x128,A6)` at 0x00E51628 and
 * `move.b (A1),(-0x127,A6)` at 0x00E5162E). */
typedef struct __attribute__((packed, aligned(2))) dir_$req_delete_file_t {
    uint16_t path_len;                  /* +0x8E */
    uint8_t  flag0;                     /* +0x90 */
    uint8_t  flag1;                     /* +0x91 */
    char     name[DIR_MAX_LEAF_LEN];    /* +0x92 */
} dir_$req_delete_file_t;

/* op 0x54 SET_DEF_PROTECTION - DIR_$SET_DEF_PROTECTION 0x00E520A6,
 * base_size 0x3C (DIR_$OP_TAB[21]).  0x8E + 0x3C = 0xCA, the end of the
 * ACL uid.  DIR_$DO_OP's case 0x54 hands req+0x8E, req+0x96 and req+0xC2 to
 * dir_$do_op_set_def_prot in exactly these roles (0x00E4C81C). */
typedef struct __attribute__((packed, aligned(2))) dir_$req_set_def_prot_t {
    uid_t    acl_type_uid;              /* +0x8E  0x00E520DE */
    uint32_t prot[11];                  /* +0x96  0x00E520EA, 11 longwords */
    uid_t    acl_uid;                   /* +0xC2  0x00E520F6 */
} dir_$req_set_def_prot_t;

/* op 0x58 RESOLVE - DIR_$RESOLVE 0x00E4D356, base_size 0x22
 * (DIR_$OP_TAB[23]).  0x8E + 0x22 = 0xB0.  The pathname is passed as a
 * POINTER (`move.l (0x8,A6),(-0x162,A6)` at 0x00E4D3BA); the request's own
 * uid at +0x04 is a SECOND copy of the uid that also lands at +0x94. */
typedef struct __attribute__((packed, aligned(2))) dir_$req_resolve_t {
    uint32_t path_ptr;                  /* +0x8E  0x00E4D3BA */
    uint16_t path_len;                  /* +0x92  0x00E4D3B6 */
    uid_t    start_uid;                 /* +0x94  0x00E4D3CA */
    uid_t    resolved_uid;              /* +0x9C  0x00E4D3D4 */
    uint16_t param5;                    /* +0xA4  0x00E4D3DE */
    uint16_t param6;                    /* +0xA6  0x00E4D3E4 */
    uint16_t param7;                    /* +0xA8  0x00E4D3EA */
    uint16_t param8;                    /* +0xAA  0x00E4D3EE */
    uint32_t flags;                     /* +0xAC  0x00E4D3B0 */
} dir_$req_resolve_t;

/* op 0x52 SET_PROTECTION - DIR_$SET_PROTECTION 0x00E521EE, base_size 0x36
 * (DIR_$OP_TAB[20].base_size, 0x00E7FCE6).  0x8E + 0x36 = 0xC4, the end of
 * the record. */
typedef struct __attribute__((packed, aligned(2))) dir_$req_set_prot_t {
    uint32_t prot[11];                  /* +0x8E  0x00E52262 */
    uid_t    acl_uid;                   /* +0xBA  0x00E5226E */
    int16_t  prot_type;                 /* +0xC2  0x00E52278 */
} dir_$req_set_prot_t;

/* ops whose entire body is one uid, base_size 0x08:
 *   0x4A SET_ACL             the ACL uid    (DIR_$SET_ACL 0x00E52CB8)
 *   0x4E GET_DEFAULT_ACL     the type uid   (DIR_$GET_DEFAULT_ACL 0x00E531FC)
 *   0x56 GET_DEF_PROTECTION  the type uid   (DIR_$GET_DEF_PROTECTION 0x00E51D8C)
 */
typedef struct __attribute__((packed, aligned(2))) dir_$req_uid_t {
    uid_t    uid;                       /* +0x8E */
} dir_$req_uid_t;

/* op 0x4C SET_DEFAULT_ACL - DIR_$SET_DEFAULT_ACL 0x00E53004.
 * base_size 0x10 (DIR_$OP_TAB[17].base_size, 0x00E7FCCE).  DIR_$DO_OP's
 * case 0x4C hands dir_$set_default_acl_internal request+0x8E as the ACL type
 * uid and request+0x96 as the ACL uid (0x00E4C794). */
typedef struct __attribute__((packed, aligned(2))) dir_$req_set_default_acl_t {
    uid_t    acl_type_uid;              /* +0x8E  0x00E5304C */
    uid_t    acl_uid;                   /* +0x96  0x00E53058 */
} dir_$req_set_default_acl_t;

/* Largest body any of the variants above needs. */
#define DIR_REQ_BODY_MAX    (4 + 2 * DIR_MAX_LEAF_LEN)

typedef struct __attribute__((packed, aligned(2))) dir_$do_op_request_t {
    uint8_t  pad_00[3];         /* 0x00 never written by a builder */
    uint8_t  op;                /* 0x03 */
    uid_t    uid;               /* 0x04 */
    uint16_t pad_0c;            /* 0x0C never written by a builder */
    uint16_t version;           /* 0x0E DIR_$OP_REC(op >> 1).version */
    uint16_t pad_10;            /* 0x10 never written by a builder */
    uint16_t reply_version;     /* 0x12 written by DIR_$DO_OP, 0x00E4C0BA */
    uint8_t  pad_14[0x7A];      /* 0x14..0x8D REM_FILE request header */
    union {
        dir_$req_name_t             name;
        dir_$req_uid_name_t         uid_name;
        dir_$req_word_name_t        word_name;
        dir_$req_add_entry_t        add_entry;
        dir_$req_cname_t            cname;
        dir_$req_add_link_t         add_link;
        dir_$req_uid_t              uid_body;
        dir_$req_set_prot_t         set_prot;
        dir_$req_delete_file_t      delete_file;
        dir_$req_set_def_prot_t     set_def_prot;
        dir_$req_resolve_t          resolve;
        dir_$req_set_default_acl_t  set_default_acl;
        uint8_t                     raw[DIR_REQ_BODY_MAX];
    } body;                     /* 0x8E */
} dir_$do_op_request_t;

/* The body offset DIR_$DO_OP adds back (`addi.w #0x8e,D1w`, 0x00E4C110). */
#define DIR_REQ_BODY_OFF    0x8E

_Static_assert(__builtin_offsetof(dir_$do_op_request_t, op) == 0x03,
               "dir_$do_op_request_t.op at +0x03");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, uid) == 0x04,
               "dir_$do_op_request_t.uid at +0x04");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, version) == 0x0E,
               "dir_$do_op_request_t.version at +0x0E");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, reply_version) == 0x12,
               "dir_$do_op_request_t.reply_version at +0x12");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, body) == DIR_REQ_BODY_OFF,
               "dir_$do_op_request_t.body at +0x8E");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, body.name.path_len) == 0x8E,
               "name body path_len at +0x8E");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, body.name.name) == 0x90,
               "name body text at +0x90");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, body.add_entry.file_uid) == 0x90,
               "add_entry file_uid at +0x90");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, body.add_entry.flags) == 0x98,
               "add_entry flags at +0x98");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, body.add_entry.name) == 0x9C,
               "add_entry name at +0x9C");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, body.cname.new_len) == 0x90,
               "cname new_len at +0x90");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, body.cname.name) == 0x92,
               "cname names at +0x92");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, body.add_link.target_len) == 0x90,
               "add_link target_len at +0x90");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, body.add_link.target_ptr) == 0x92,
               "add_link target_ptr at +0x92");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, body.add_link.name) == 0x96,
               "add_link name at +0x96");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, body.uid_name.target_uid) == 0x90,
               "uid_name target_uid at +0x90");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, body.uid_name.name) == 0x98,
               "uid_name name at +0x98");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, body.word_name.flags) == 0x90,
               "word_name flags at +0x90");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, body.word_name.name) == 0x92,
               "word_name name at +0x92");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, body.delete_file.flag0) == 0x90,
               "delete_file flag0 at +0x90");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, body.delete_file.name) == 0x92,
               "delete_file name at +0x92");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, body.set_def_prot.prot) == 0x96,
               "set_def_prot prot block at +0x96");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, body.set_def_prot.acl_uid) == 0xC2,
               "set_def_prot acl uid at +0xC2");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, body.resolve.path_len) == 0x92,
               "resolve path_len at +0x92");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, body.resolve.start_uid) == 0x94,
               "resolve start_uid at +0x94");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, body.resolve.resolved_uid) == 0x9C,
               "resolve resolved_uid at +0x9C");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, body.resolve.param5) == 0xA4,
               "resolve param5 at +0xA4");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, body.resolve.flags) == 0xAC,
               "resolve flags at +0xAC");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, body.set_prot.prot) == 0x8E,
               "set_prot prot block at +0x8E");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, body.set_prot.acl_uid) == 0xBA,
               "set_prot acl uid at +0xBA");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, body.set_prot.prot_type) == 0xC2,
               "set_prot type at +0xC2");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, body.uid_body.uid) == 0x8E,
               "single-uid body at +0x8E");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, body.set_default_acl.acl_type_uid) == 0x8E,
               "set_default_acl type uid at +0x8E");
_Static_assert(__builtin_offsetof(dir_$do_op_request_t, body.set_default_acl.acl_uid) == 0x96,
               "set_default_acl acl uid at +0x96");

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
 * dir_$page_path_t - one level of the root-to-leaf path dir_$find_entry
 * records in its `extra` buffer
 *
 * dir_$remove_entry (0x00E50FC8) gives find_entry the buffer at A6-0x30 and
 * then indexes it as `(-0x34,A6,D0*1)` with D0 = level*4 (0x00E510AC), and its
 * nested helper reads both halves the same way (`(-0x34,A3,D3*1)` /
 * `(-0x32,A3,D3*1)` at 0x00E50D7A / 0x00E50E14).  The base is therefore
 * A6-0x34 and the array is 1-BASED: level N lives at buffer[N-1].
 * dir_insert_ctx_t models the same pairs as path_page[]/path_entry[].
 */
typedef struct dir_$page_path_t {
    uint16_t    page_no;        /* +0x00 */
    uint16_t    entry_idx;      /* +0x02 */
} dir_$page_path_t;

#if defined(ARCH_M68K)
_Static_assert(sizeof(dir_$page_path_t) == 4, "sizeof dir_$page_path_t");
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

/* DIR_$OLD_DROP_DIRU (0x00E5734C) takes FOUR parameters: the frame reads
 * (0x8,A6) parent uid, (0xc,A6) name, (0x10,A6) name-length POINTER and
 * (0x14,A6) status, and both call sites push exactly four longwords
 * (DIR_$DROP_DIRU 0x00E52B4A, dir_$do_op_drop_dir 0x00E529D4). */
void DIR_$OLD_DROP_DIRU(uid_t *parent_uid, char *name, uint16_t *name_len,
                        status_$t *status_ret);

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

/* Seven longword parameters, all pointers: the callee frame at 0x00E577F4
 * reads (0x08) dir uid, (0x0C) name, (0x10) name-length pointer
 * (`movea.l (0x10,A6),A0` / `move.w (A0)` at 0x00E57820), (0x14) target
 * buffer, (0x18) target-length pointer, (0x1C) target uid and (0x20) status.
 * DIR_$READ_LINKU's fallback push at 0x00E4D790 matches exactly. */
void DIR_$OLD_READ_LINKU(uid_t *dir_uid, char *name, uint16_t *name_len,
                         void *target, uint16_t *target_len,
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

/*
 * `move.w (0x155a,A5),D0w` reads the LOW half of the longword count at
 * `(0x1558,A5)`: dir_$do_op_drop_mount uses both spellings for the same
 * object (0x00E53404 reads the word, 0x00E5342E compares the longword
 * against 1 and 0x00E5348C decrements it).  A `*(int16_t *)(a5 + 0x155A)`
 * would pick up the HIGH half on a little-endian host, so take the low
 * 16 bits of the longword instead - identical code on m68k, correct
 * everywhere.
 */
#define DIR_MOUNT_COUNT16(a5) \
    ((int16_t)*(const int32_t *)((const char *)(a5) + DIR_MOUNT_COUNT_OFF))

/*
 * The three parallel tables are ONE-BASED.  dir_$do_op_add_mount stores the
 * new entry n = count + 1 at
 *   `(0x1554,A0)` with A0 = A5 + n*8   (0x00E5336E)
 *   `(0x1594,A1)` with A1 = A5 + n*8   (0x00E53382)
 *   `(0x15d8,A2)` with A2 = A5 + n*4   (0x00E53394)
 * and its duplicate scan reads the same cells as base + stride*(k+1) for
 * k = 0..count-1 (0x00E532B4-0x00E532D2).  dir_$do_op_drop_mount uses the
 * same three bases (0x00E53444-0x00E53486).  Slot 0 of the UID table is
 * where the count longword itself lives, which is why nothing uses it.
 */
#define DIR_MOUNT_UID_TAB_OFF   0x1554  /* uid_t[],    entry n at +n*8 */
#define DIR_MOUNT_TGT_TAB_OFF   0x1594  /* uid_t[],    entry n at +n*8 */
#define DIR_MOUNT_NODE_TAB_OFF  0x15D8  /* uint32_t[], entry n at +n*4 */
#define DIR_MOUNT_MAX           8       /* count must stay below this */

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

/* dir_$remove_entry_from_page (0x00E50D5E) is a nested Pascal subprocedure of
 * dir_$remove_entry: 0x00E5108E hands it the parent frame in A1 and it reaches
 * every one of its working cells through that static link.  It is flattened
 * into a file static in dir/remove_entry.c with explicit uplevel arguments, so
 * it is deliberately NOT declared here. */

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
 * `flags` is really the CAPACITY of the `extra` path buffer, in
 * dir_$page_path_t elements: 0x00E4CAE0 compares it against the level about
 * to be recorded and calls CRASH_SYSTEM past it (0x00E4CB00).  The path is
 * 1-based - level N is written at extra+(N-1)*4 (0x00E4CAEE / 0x00E4CAF4) -
 * and `depth_ret` comes back holding the deepest level reached.
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

/*
 * ============================================================================
 * The DIR module data block - A5 = 0x00E7DC00
 * ============================================================================
 *
 * SAU2 map: "D E7DBF8 DIR size = 212C", i.e. 0x00E7DBF8..0x00E7FD24.  Every
 * DIR routine establishes the base with `lea (0xe7dc00).l,A5` (DIR_$DO_OP at
 * 0x00E4C030, DIR_$CLEANUP at 0x00E53580) or `movea.l #0xe7dc00,A0`
 * (DIR_$INIT at 0x00E3141A), so A5 is the segment base + 8: the module's two
 * constant pointers are reached as -0x8/-0x4 and everything else as
 * 0x0000..0x2124.
 *
 * The two tables DIR_$INIT builds and DIR_$CLEANUP walks live here:
 *
 *   A5+0x1680  dir_$lock_entry_t[32]  0x10 stride  0x00E7F280..0x00E7F480
 *   A5+0x1880  dir_$handle_t[32]      0x3C stride  0x00E7F480..0x00E7FC00
 *
 * 0x00E7FC00 is DIR_$NAME_OFFSET_TABLE, the next object in the block, so the
 * handle table ends exactly where it begins.
 */
#define DIR_A5_BASE_VA          0x00E7DC00u /* the `lea (0xe7dc00).l,A5` base */

/* A5-relative offsets of the two tables and the four scalars around them. */
#define DIR_CRASH_STATUS_OFF    (-0x4)       /* status_$t * for CRASH_SYSTEM     */
#define DIR_PURIFY_ARG_OFF      (-0x8)       /* AST_$PURIFY segment-list pointer  */
#define DIR_LOCK_TAB_OFF        0x1680       /* dir_$lock_entry_t[32]            */
#define DIR_HANDLE_TAB_OFF      0x1880       /* dir_$handle_t[32]                */
#define DIR_LOCK_FREE_OFF       0x2030       /* head of the lock free list       */
#define DIR_LOCK_IN_USE_OFF     0x2034       /* lock-entry in-use bitmap         */
#define DIR_HANDLE_FREE_OFF     0x2038       /* head of the handle free list     */
#define DIR_HANDLE_IN_USE_OFF   0x203C       /* handle in-use bitmap, 32 bits    */
#define DIR_LINK_BUF_OWNER_OFF  0x2040       /* DIR_$LINK_BUF_MUTEX owner word   */
#define DIR_SLOT_COUNT          32           /* `moveq #0x1f,D2` + dbf, 0x00E31428 */

/*
 * dir_$lock_entry_t - the 0x10-byte per-object lock records at A5+0x1680
 * (0x00E7F280).  DIR_$INIT chains all 32 through their first longword and
 * numbers them at +0x0E (0x00E3146A-0x00E31472); DIR_$LOCK_OBJ (0x00E4AFA8)
 * takes one off the free list at 0x00E4B02A-0x00E4B04E and overlays the
 * locked object's UID on the first two longwords, so `next` and `uid`
 * alias by design.
 */
typedef struct dir_$lock_entry_t {
    union {
        uint32_t next;                  /* 0x00 free-list link VA (DIR_$INIT) */
        uid_t    uid;                   /* 0x00 locked object's UID (in use)  */
    } u;
    /* TODO(source-ak1g): the next two names come from dir/lock_obj.c's
     * existing comments, not from a fresh read of DIR_$LOCK_OBJ 0x00E4B5F4. */
    uint32_t  waiters;                  /* 0x08 waiter queue head            */
    int16_t   lock_count;               /* 0x0C                              */
    uint16_t  index;                    /* 0x0E slot number, 0..31           */
} dir_$lock_entry_t;

/*
 * Both records carry 32-bit target virtual addresses rather than C pointers
 * (ARCH_VA_TO_PTR / ARCH_PTR_TO_VA convert), so every offset below holds on a
 * 64-bit host build too and the asserts are unconditional.
 */
_Static_assert(sizeof(dir_$lock_entry_t) == 0x10, "dir_$lock_entry_t stride 0x10");
_Static_assert(__builtin_offsetof(dir_$lock_entry_t, waiters) == 0x08, "+0x08");
_Static_assert(__builtin_offsetof(dir_$lock_entry_t, lock_count) == 0x0C, "+0x0C");
_Static_assert(__builtin_offsetof(dir_$lock_entry_t, index) == 0x0E, "+0x0E");

/*
 * dir_$handle_t - the 0x3C-byte directory handle slots at A5+0x1880
 * (0x00E7F480).  `moveq #0x3c,D0` at 0x00E31480 (DIR_$INIT) and 0x00E536EA
 * (DIR_$CLEANUP) is the stride; 32 * 0x3C = 0x780 lands exactly on
 * DIR_$NAME_OFFSET_TABLE at 0x00E7FC00.
 *
 * Field addresses (a handle pointer is always slot base + 0, so the DIR_$INIT
 * displacements below are 0x1880 + the field offset):
 *   0x00 uid          DIR_$LOCK_OBJ compares h[0]/h[1] with the lock entry
 *   0x08 owner        DIR_$CLEANUP 0x00E535B0; DIR_$ALLOC_HANDLE stores
 *                     PROC1_$CURRENT there at 0x00E4B958
 *   0x0A lock_mode    DIR_$LOCK_OBJ 0x00E4AFD6 (1 = read, 2 = write)
 *   0x0E split_busy   set 0xFF by dir_$alloc_split_page (0x00E4EB40),
 *                     cleared by dir_$truncate_pages (0x00E4E90A) and
 *                     DIR_$ALLOC_HANDLE (0x00E4B86E); tested here with
 *                     `tst.b (0xe,A1)` at 0x00E535CA
 *   0x10 length       directory length in bytes (>> 10 = page count)
 *   0x14 wired_page   dir_$release_wire's WP_$UNWIRE argument
 *   0x18 buf          the 0x400-byte page buffer DIR_$WIRE_PAGE hands out
 *   0x1C max_slots    2 = no page wired (DIR_$ALLOC_HANDLE 0x00E4B96C and
 *                     dir_$release_wire both store 2)
 *   0x1E cur_slot     dir_$map_page's 2-entry LRU selector
 *   0x22 page_cache   dir_$map_page's two 8-byte {group, base} entries
 *   0x30 next         free-list link (DIR_$INIT 0x00E31466)
 *   0x34 lock_entry   -> dir_$lock_entry_t (DIR_$LOCK_OBJ 0x00E4B02A)
 *   0x38 slot_index   0..31 (DIR_$INIT 0x00E31458, read by
 *                     DIR_$ALLOC_HANDLE at 0x00E4B930)
 *   0x3A volume       DIR_$VALIDATE_HANDLE 0x00E4B566
 */
typedef struct dir_$handle_t {
    uid_t     uid;                  /* 0x00 */
    int16_t   owner;                /* 0x08 owning PROC1_$CURRENT, 0 = free */
    int16_t   lock_mode;            /* 0x0A */
    uint8_t   _0x0c[2];             /* 0x0C */
    int8_t    split_busy;           /* 0x0E Domain boolean, 0xFF = true */
    uint8_t   _0x0f;                /* 0x0F */
    uint32_t  length;               /* 0x10 */
    uint32_t  wired_page;           /* 0x14 */
    uint32_t  buf;                  /* 0x18 VA of the 0x400-byte page buffer */
    int16_t   max_slots;            /* 0x1C */
    int16_t   cur_slot;             /* 0x1E */
    uint8_t   mapped;               /* 0x20 */
    uint8_t   _0x21;                /* 0x21 */
    uint8_t   page_cache[0x0E];     /* 0x22 two 8-byte dir_$map_page entries */
    uint32_t  next;                 /* 0x30 VA of the next free dir_$handle_t */
    uint32_t  lock_entry;           /* 0x34 VA of a dir_$lock_entry_t         */
    uint16_t  slot_index;           /* 0x38 */
    int16_t   volume;               /* 0x3A */
} dir_$handle_t;

_Static_assert(sizeof(dir_$handle_t) == 0x3C, "dir_$handle_t stride 0x3C");
_Static_assert(__builtin_offsetof(dir_$handle_t, owner) == 0x08, "+0x08");
_Static_assert(__builtin_offsetof(dir_$handle_t, lock_mode) == 0x0A, "+0x0A");
_Static_assert(__builtin_offsetof(dir_$handle_t, split_busy) == 0x0E, "+0x0E");
_Static_assert(__builtin_offsetof(dir_$handle_t, length) == 0x10, "+0x10");
_Static_assert(__builtin_offsetof(dir_$handle_t, wired_page) == 0x14, "+0x14");
_Static_assert(__builtin_offsetof(dir_$handle_t, buf) == 0x18, "+0x18");
_Static_assert(__builtin_offsetof(dir_$handle_t, max_slots) == 0x1C, "+0x1C");
_Static_assert(__builtin_offsetof(dir_$handle_t, cur_slot) == 0x1E, "+0x1E");
_Static_assert(__builtin_offsetof(dir_$handle_t, mapped) == 0x20, "+0x20");
_Static_assert(__builtin_offsetof(dir_$handle_t, page_cache) == 0x22, "+0x22");
_Static_assert(__builtin_offsetof(dir_$handle_t, next) == 0x30, "+0x30");
_Static_assert(__builtin_offsetof(dir_$handle_t, lock_entry) == 0x34, "+0x34");
_Static_assert(__builtin_offsetof(dir_$handle_t, slot_index) == 0x38, "+0x38");
_Static_assert(__builtin_offsetof(dir_$handle_t, volume) == DIR_HANDLE_VOLUME_OFF, "+0x3A");
_Static_assert(DIR_HANDLE_TAB_OFF + DIR_SLOT_COUNT * 0x3C == 0x2000,
               "handle table ends at DIR_$NAME_OFFSET_TABLE (0x00E7FC00)");
_Static_assert(DIR_LOCK_TAB_OFF + DIR_SLOT_COUNT * 0x10 == DIR_HANDLE_TAB_OFF,
               "lock table ends where the handle table begins (0x00E7F480)");

/*
 * Accessors for the block.  On the m68k these resolve through the live A5;
 * a host test overrides __A5_BASE() with a buffer of its own (see
 * dir/test/test_cleanup.c).  Every one carries the absolute address the
 * image uses so the mapping stays checkable.
 */
#define DIR_BLOCK_AT(blk, off)  ((char *)(blk) + (off))

#define DIR_LOCK_TAB_OF(blk) \
    ((dir_$lock_entry_t *)DIR_BLOCK_AT(blk, DIR_LOCK_TAB_OFF))
#define DIR_HANDLE_TAB_OF(blk) \
    ((dir_$handle_t *)DIR_BLOCK_AT(blk, DIR_HANDLE_TAB_OFF))
#define DIR_LOCK_FREE_OF(blk) \
    (*(uint32_t *)DIR_BLOCK_AT(blk, DIR_LOCK_FREE_OFF))
#define DIR_LOCK_IN_USE_OF(blk) \
    (*(uint32_t *)DIR_BLOCK_AT(blk, DIR_LOCK_IN_USE_OFF))
#define DIR_HANDLE_FREE_OF(blk) \
    (*(uint32_t *)DIR_BLOCK_AT(blk, DIR_HANDLE_FREE_OFF))
#define DIR_HANDLE_IN_USE_OF(blk) \
    (*(uint32_t *)DIR_BLOCK_AT(blk, DIR_HANDLE_IN_USE_OFF))
#define DIR_LINK_BUF_OWNER_OF(blk) \
    (*(int16_t *)DIR_BLOCK_AT(blk, DIR_LINK_BUF_OWNER_OFF))

/* The DIR routines proper, which hold the block base in A5. */
#define DIR_$LOCK_TAB       DIR_LOCK_TAB_OF(__A5_BASE())
#define DIR_$HANDLE_TAB     DIR_HANDLE_TAB_OF(__A5_BASE())
#define DIR_$LOCK_FREE      DIR_LOCK_FREE_OF(__A5_BASE())
#define DIR_$LOCK_IN_USE    DIR_LOCK_IN_USE_OF(__A5_BASE())
#define DIR_$HANDLE_FREE    DIR_HANDLE_FREE_OF(__A5_BASE())
#define DIR_$HANDLE_IN_USE  DIR_HANDLE_IN_USE_OF(__A5_BASE())
#define DIR_$LINK_BUF_OWNER DIR_LINK_BUF_OWNER_OF(__A5_BASE())

/*
 * DIR_$INIT (0x00E3140C) is compiled into a different module - the map's
 * "I E3140C DIR size = E8" in the boot-time init segment - so its own A5 is
 * 0x00E3503C (loaded at 0x00E31414 and then never used) and it reaches this
 * block absolutely with `movea.l #0xe7dc00,A0` at 0x00E3141A.  A host build
 * has no such address, so DIR_$BLOCK_ABS falls back to the same overridable
 * __A5_BASE() hook the DIR routines use.
 */
#if defined(ARCH_M68K)
#define DIR_$BLOCK_ABS      ((char *)(uintptr_t)DIR_A5_BASE_VA)
#else
#define DIR_$BLOCK_ABS      ((char *)__A5_BASE())
#endif

/*
 * 0x00E7DBFC (A5-0x4): the module's pointer to the status constant every
 * CRASH_SYSTEM site in DIR pushes -- `move.l (-0x4,A5),-(SP)` at 0x00E5365A,
 * 0x00E53678 and 0x00E536D4.  Image bytes at 0x00E7DBF8 are
 * `00 e4 b3 3c 00 e4 b2 30`, so A5-0x8 holds &DIR_$CONST_ZERO_L (AST_$PURIFY's
 * segment-list argument in DIR_$VALIDATE_PAGES) and A5-0x4 holds
 * &Naming_bad_request_header_ver_err (0x00E4B230).
 */
extern status_$t *const DIR_$CRASH_STATUS;

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
extern uint32_t DIR_$CONST_ZERO_L;

/* 0x00E4B444, word 0x0001 - MST remap / ACL check parameter, read as a
 * word (`btst.b #0,(1,A0)` in FILE_$GET_ATTRIBUTES at 0x00E5D99E). */
extern uint16_t DIR_$CONST_ONE_W;
/* 0x00E4BC24, byte 0xFF: ACL_$RIGHTS' shared `ignore_super` argument. */
extern boolean DIR_$CONST_TRUE_B;

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
/* The fifth parameter is a Domain BOOLEAN BYTE, not a word: the callee reads
 * `move.b (0x16,A6),D2b` at 0x00E5045E and tests it with `tst.b`/`bmi` at
 * 0x00E504CA.  DIR_$DO_OP pushes `clr.w` for opcode 0x2A (0x00E4C2EC) and
 * `st` for opcode 0x2C (0x00E4C364). */
void dir_$do_op_add_link(uid_t *uid, void *name, uint16_t name_len, uid_t *file_uid,
                         boolean is_hard_link, status_$t *status_ret);
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
/* max_entries is a LONGWORD (`move.l (0x18,A6),D5` at 0x00E4D968, compared
 * with `cmp.l (A4),D5` at 0x00E4DA8E) and the eighth parameter is the
 * caller's entry BUFFER, not a size (`movea.l (0x20,A6),A1` at 0x00E4DA9A). */
void dir_$do_op_dir_readu(uid_t *uid, int16_t version, char *name,
                          uint16_t name_flags, void *cont, uint32_t max_entries,
                          uint32_t max_size, void *buf_ptr,
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
/* 0x00E56096, word 0x0028: the info-block buffer size DIR_$OLD_READ_INFOBLK
 * clamps its returned length to (`move.w (A3),D0w` / `cmp.w (A2),D0w` /
 * `bge` at 0x00E560F2).  Passed by reference by every caller. */
extern int16_t  DIR_$INFOBLK_MAX_LEN;
extern uint16_t DAT_00e56098;
/* 0x00E56094, word 0x0090: FILE_$GET_ATTRIBUTES' record size. */
extern int16_t  DIR_$ATTR_REC_SIZE_W;
extern uint32_t DAT_00e5609e;
extern uint8_t DAT_00e560a2;
extern uint8_t DAT_00e5609a;
/* DIR_$SET_DEF_ACL_FLUSH_LEN - 0x00E564E2, longword 0x00000400 (one page):
 * the FILE_$FW_PARTIAL byte_count (`move.l (A1),D2` at 0x00E5E6BC) that
 * DIR_$OLD_SET_DEFAULT_ACL hands over at 0x00E56446 (`pea (0x9a,PC)`).  It is
 * the only reference to the cell.  Image bytes: 00 00 04 00. */
extern uint32_t DIR_$SET_DEF_ACL_FLUSH_LEN;
/* 0x00E5716A, word 0x0006: FILE_$SET_PROT prot_type
 * (`move.w (A4),D2w` at 0x00E5DF56). */
/* 0x00E5716A, word 0x0006: FILE_$SET_PROT's protection type - "the
 * object carries an extended ACL". */
extern uint16_t DIR_$PROT_TYPE_ACL;
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
 * dir_$rep_entry_t - the record REM_NAME_$GET_ENTRY (0x00E4AD18) fills
 *
 * Recovered from DIR_$OLD_VALIDATE_ROOT_ENTRY's frame at A6-0x58
 * (0x00E58102), which is the only consumer in this tree.  The fields it
 * touches:
 *   +0x02  the case-mapped name's length - UNMAP_CASE's in-length VAR
 *          argument at 0x00E581AE (`pea (-0x56,A6)`)
 *   +0x04  the case-mapped name itself - UNMAP_CASE's input at 0x00E581B2
 *          (`pea (-0x54,A6)`); 0x20 bytes, the size the max-out-length cell
 *          0x00E544AE names
 *   +0x24  the object UID, compared against the local entry's at 0x00E58126
 *          and handed to name_$old_add_entry at 0x00E581C4
 *   +0x2C  the entry's extra longword, compared at 0x00E58138 and passed as
 *          name_$old_add_entry's flags at 0x00E581C0
 * The record runs A6-0x58..A6-0x29, i.e. 0x30 bytes.
 */
/* Every field already lands on its natural boundary, so the record needs no
 * packing; the _Static_asserts below pin the offsets to the image's. */
typedef struct dir_$rep_entry_t {
    uint16_t hdr;               /* 0x00: not read by this caller */
    uint16_t name_len;          /* 0x02 */
    uint8_t  name[0x20];        /* 0x04 */
    uid_t    uid;               /* 0x24 */
    uint32_t extra;             /* 0x2C */
} dir_$rep_entry_t;

#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(dir_$rep_entry_t, name_len) == 0x02, "dir_$rep_entry_t.name_len");
_Static_assert(__builtin_offsetof(dir_$rep_entry_t, name) == 0x04, "dir_$rep_entry_t.name");
_Static_assert(__builtin_offsetof(dir_$rep_entry_t, uid) == 0x24, "dir_$rep_entry_t.uid");
_Static_assert(__builtin_offsetof(dir_$rep_entry_t, extra) == 0x2C, "dir_$rep_entry_t.extra");
_Static_assert(sizeof(dir_$rep_entry_t) == 0x30, "sizeof dir_$rep_entry_t");
#endif

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
extern int16_t  DIR_$READU_ATTR_SIZE;   /* 0xE4DFFA: word 0x0090 - FILE_$GET_ATTRIBUTES size_ptr
                                 * (`cmpi.w #0x90,(A0)` at 0x00E5D9F6) */
extern uint8_t  DIR_$READU_NUL_NAME;   /* 0xE4DFFC: NUL byte used as the 1-char name "\0" */
extern uint8_t  DIR_$CASE_FOLD_BITMAP; /* 0xE4CD84: case-folding character bitmap (07 ff ff fe ...) */
extern uint32_t DAT_00e4b448;   /* 0xE4B448: longword 0x00008000 - MST_$REMAP_PRIVI
                                 * length parameter.  It is read as a longword,
                                 * so the cell is four bytes (bead source-wk2f). */
extern const int32_t DAT_00e52040; /* 0xE52040: 0x00000400 - one page; FILE_$FW_PARTIAL byte
                                      count / FILE_$TRUNCATE length (defined in dir_data.c) */

/*
 * DIR_$OLD_LINK_TEXT_MAX - 0xE577F2, the word 0x0100 (256) sitting between the
 * `rts` of DIR_$OLD_ADD_LINKU (0x00E577F0) and the `link` of
 * DIR_$OLD_READ_LINKU (0x00E577F4).  Both routines reach it PC-relative and
 * hand it over as the max_out_len VAR parameter of the case mappers:
 *   0x00E57732  pea (0xbe,PC)    -> MAP_CASE   (DIR_$OLD_ADD_LINKU)
 *   0x00E578D4  pea (-0xe4,PC)   -> UNMAP_CASE (DIR_$OLD_READ_LINKU)
 * It is the size of the 256-byte link-text buffer both frames carry.
 * Image bytes: 01 00.  (Ghidra label was DAT_00e577f2.)
 */
extern int16_t DIR_$OLD_LINK_TEXT_MAX;

/*
 * DIR_$ADD_ENTRY_INTERNAL's two A5 cells - `move.w (0x2042,A5)` at
 * 0x00E5011A and `move.w (0x2046,A5)` at 0x00E5013A.  With A5 = 0xE7DC00
 * those are 0x00E7FC42 and 0x00E7FC46, i.e. the version and base_size words
 * of DIR_$OP_TAB record 0 (opcode 0x2A >> 1).  The routine inherits A5 from
 * DIR_$ADDU / DIR_$ROOT_ADDU; it never loads it itself.
 */

#endif /* DIR_INTERNAL_H */
