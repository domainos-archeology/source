/*
 * ACL Internal Header - Access Control List Management
 *
 * Internal data structures and functions for the ACL subsystem.
 * This file should only be included by ACL implementation files.
 */

#ifndef ACL_INTERNAL_H
#define ACL_INTERNAL_H

#include "acl/acl.h"
#include "proc1/proc1.h"
#include "ml/ml.h"
#include "rgyc/rgyc.h"
#include "uid/uid.h"   /* UID_$NIL */
#include "ast/ast.h"   /* ast_$acl_attr_t */
#include "file/file.h" /* file_$obj_loc_t */

/*
 * ============================================================================
 * Status Codes
 * ============================================================================
 */
/* "wrong type - operation illegal on system objects" (stcode 230004) */
/* "no right to set subsystem data or subsystem manager" (stcode 230010) */
/* "ACL object not found" (stcode 23000d) */
#define status_$acl_object_not_found            0x0023000d
#define status_$project_list_is_full            0x00230011
#define status_$acl_proj_list_too_big           0x00230012
#define status_$image_buffer_too_small          0x0023000c

/*
 * ============================================================================
 * Constants
 * ============================================================================
 */

/* Maximum number of project UIDs per process */
#define ACL_MAX_PROJECTS    8

/*
 * The ACL_ module's data segment.  ACL_$INIT zeroes it whole:
 *   00e310aa  lea (0xe935cc).l,A0
 *   00e310b0  move.l A0,D0
 *   00e310b2  sub.l #0xe88834,D0
 *   00e310b8  move.l D0,-(SP) / move.l #0xe88834,-(SP) / jsr OS_$DATA_ZERO
 * and the SAU2 link map agrees: `D69 E88834 ACL_$DATA size = AD98`, with
 * 0xE935CC the start of the next segment, FILE_$LOT_DATA.  The individual C
 * objects that live inside it overlap in the image (ACL_$ACL_CACHE's 31st
 * 0x400-byte slot covers ACL_$ORIGINAL_SIDS[0], which is never used because
 * process numbers start at 1), so the length is the segment size and NOT a
 * sum of sizeofs.
 */
#define ACL_DATA_BASE       0x00E88834U
#define ACL_DATA_END        0x00E935CCU
#define ACL_DATA_SIZE       (ACL_DATA_END - ACL_DATA_BASE)
_Static_assert(ACL_DATA_SIZE == 0xAD98U,
               "ACL_$DATA segment size (SAU2 link map: E88834, size AD98)");

/*
 * The ML resource-lock id the ACL cache runs under.  acl_$eval_rights brackets
 * its whole ACL-image path with `move.w #0xa,-(SP)` + ML_$LOCK / ML_$UNLOCK
 * (0x00E467C6, 0x00E467F4, 0x00E4683E, 0x00E46888).
 */
#define ML_LOCK_ACL         0x0A

/*
 * Rights bits.  acl_$eval_rights masks the granted rights with these two
 * constants on its privileged short circuits:
 *   `moveq #0xf,D2`   (0x00E4668A) - super-user / locksmith: all four of
 *                     read/write/execute/delete.
 *   `moveq #-0x51,D2` (0x00E466CE, 0x00E46838) - locksmith or subsystem
 *                     manager: everything except bits 4 and 6.
 * and clears bit 4 of every rights word produced from an ACL image
 *   `andi.l #-0x11,D2` (0x00E46894).
 */
#define ACL_RIGHTS_ALL              0x0000000FU
#define ACL_RIGHTS_PRIVILEGED       0xFFFFFFAFU
#define ACL_RIGHTS_NOT_IGNORED      0xFFFFFFEFU
/* Bit 4 in a per-SID rights byte marks the slot as "not a plain SID match";
 * `btst.b #0x4,...` at 0x00E466FA / 0x00E4671E / 0x00E46792. */
#define ACL_RIGHT_IGNORE            0x10
/* The two rights the "local locksmith" downgrade adds back (`moveq #0x5,D0`
 * at 0x00E468A0). */
#define ACL_RIGHTS_READ_EXECUTE     0x00000005U

/*
 * ============================================================================
 * ACL Data Structures
 * ============================================================================
 *
 * Per-process ACL data is stored in several parallel arrays indexed by PID.
 * Each process has:
 *   - Current SIDs (user, group, org, login): 36 bytes at 0x24 stride
 *   - Saved SIDs (for subsystem entry): 36 bytes at 0x24 stride
 *   - Pre-subsystem SIDs: 36 bytes at 0x24 stride
 *   - Project list: 12 bytes at 0x0C stride
 *   - Subsystem level counter: 2 bytes at 0x02 stride
 *   - Super mode counter: 2 bytes at 0x02 stride
 */

/*
 * SID block - Security ID information for a process (36 bytes)
 * Stored at stride 0x24 (36 bytes) per process
 */
typedef struct acl_sid_block_t {
    uid_t user_sid;      /* 0x00: User SID */
    uid_t group_sid;     /* 0x08: Group/Project SID */
    uid_t org_sid;       /* 0x10: Organization SID */
    uid_t login_sid;     /* 0x18: Login SID */
    uint32_t pad;        /* 0x20: Padding to 36 bytes */
} acl_sid_block_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(acl_sid_block_t, user_sid) == 0x00, "acl_sid_block_t.user_sid");
_Static_assert(__builtin_offsetof(acl_sid_block_t, group_sid) == 0x08, "acl_sid_block_t.group_sid");
_Static_assert(__builtin_offsetof(acl_sid_block_t, org_sid) == 0x10, "acl_sid_block_t.org_sid");
_Static_assert(__builtin_offsetof(acl_sid_block_t, login_sid) == 0x18, "acl_sid_block_t.login_sid");
_Static_assert(__builtin_offsetof(acl_sid_block_t, pad) == 0x20, "acl_sid_block_t.pad");

/*
 * An eight-byte UID compare, the shape the image uses everywhere:
 * `moveq #0x1,D0 / cmpm.l (A1)+,(A0)+ / bne / cmpm.l (A1)+,(A0)+`
 * (e.g. acl_$eval_rights 0x00E4650A).  Returns a Domain boolean.
 */
static inline boolean acl_$uid_eq(const uid_t *a, const uid_t *b)
{
    return (a->high == b->high && a->low == b->low) ? true : false;
}

/*
 * A whole 36-byte SID block compare, the shape ACL_$SET_RE_ALL_SIDS
 * (0x00E4845C, 0x00E484F0) and ACL_$SET_RES_ALL_SIDS (0x00E48706) use:
 * `moveq #0x8,D1 / cmpm.l (A2)+,(A1)+ / dbne D1w,...` - nine longwords, so
 * the trailing `pad` word takes part in the comparison.  Returns a Domain
 * boolean.
 */
static inline boolean acl_$sid_block_eq(const acl_sid_block_t *a,
                                        const acl_sid_block_t *b)
{
    const uint32_t *pa = (const uint32_t *)a;
    const uint32_t *pb = (const uint32_t *)b;
    int i;

    for (i = 0; i < 9; i++) {
        if (pa[i] != pb[i]) {
            return false;
        }
    }
    return true;
}

/*
 * The audit data block ACL_$SET_RE_ALL_SIDS builds in its frame at A6-0x90
 * and hands to AUDIT_$LOG_EVENT_S as (data, data_len).  Its length is the
 * word 0x0090 at 0x00E48558, pushed by the `pea (0x26,PC)` at 0x00E48530,
 * which is exactly four consecutive SID blocks; `sid` is a pointer to the
 * second one (`pea (-0x6c,A6)` at 0x00E4853A).
 */
typedef struct acl_$set_re_sids_audit_t {
    acl_sid_block_t old_original;   /* A6-0x90 */
    acl_sid_block_t old_current;    /* A6-0x6C */
    acl_sid_block_t new_original;   /* A6-0x48 */
    acl_sid_block_t new_current;    /* A6-0x24 */
} acl_$set_re_sids_audit_t;

_Static_assert(__builtin_offsetof(acl_$set_re_sids_audit_t, old_original) == 0x00,
               "acl_$set_re_sids_audit_t.old_original (A6-0x90)");
_Static_assert(__builtin_offsetof(acl_$set_re_sids_audit_t, old_current) == 0x24,
               "acl_$set_re_sids_audit_t.old_current (A6-0x6C)");
_Static_assert(__builtin_offsetof(acl_$set_re_sids_audit_t, new_original) == 0x48,
               "acl_$set_re_sids_audit_t.new_original (A6-0x48)");
_Static_assert(__builtin_offsetof(acl_$set_re_sids_audit_t, new_current) == 0x6C,
               "acl_$set_re_sids_audit_t.new_current (A6-0x24)");
_Static_assert(sizeof(acl_$set_re_sids_audit_t) == 0x90,
               "audit data length word at 0x00E48558");

/*
 * The same record for ACL_$SET_RES_ALL_SIDS, which also carries the SAVED
 * block.  Length is the word 0x00D8 at 0x00E48790 (`pea (0x26,PC)` at
 * 0x00E48768); `sid` points at old_current (`pea (-0xb4,A6)`).
 */
typedef struct acl_$set_res_sids_audit_t {
    acl_sid_block_t old_original;   /* A6-0xD8 */
    acl_sid_block_t old_current;    /* A6-0xB4 */
    acl_sid_block_t old_saved;      /* A6-0x90 */
    acl_sid_block_t new_original;   /* A6-0x6C */
    acl_sid_block_t new_current;    /* A6-0x48 */
    acl_sid_block_t new_saved;      /* A6-0x24 */
} acl_$set_res_sids_audit_t;

_Static_assert(__builtin_offsetof(acl_$set_res_sids_audit_t, old_original) == 0x00,
               "acl_$set_res_sids_audit_t.old_original (A6-0xD8)");
_Static_assert(__builtin_offsetof(acl_$set_res_sids_audit_t, old_current) == 0x24,
               "acl_$set_res_sids_audit_t.old_current (A6-0xB4)");
_Static_assert(__builtin_offsetof(acl_$set_res_sids_audit_t, old_saved) == 0x48,
               "acl_$set_res_sids_audit_t.old_saved (A6-0x90)");
_Static_assert(__builtin_offsetof(acl_$set_res_sids_audit_t, new_original) == 0x6C,
               "acl_$set_res_sids_audit_t.new_original (A6-0x6C)");
_Static_assert(__builtin_offsetof(acl_$set_res_sids_audit_t, new_current) == 0x90,
               "acl_$set_res_sids_audit_t.new_current (A6-0x48)");
_Static_assert(__builtin_offsetof(acl_$set_res_sids_audit_t, new_saved) == 0xB4,
               "acl_$set_res_sids_audit_t.new_saved (A6-0x24)");
_Static_assert(sizeof(acl_$set_res_sids_audit_t) == 0xD8,
               "audit data length word at 0x00E48790");

/* ast_$acl_attr_t.acl_data is a raw byte array in ast/ast.h; this is the
 * protection record the ACL subsystem reads out of it. */
#define ACL_$PROT_DATA(attr)    ((acl_$prot_data_t *)((attr)->acl_data))

/*
 * ast_$acl_attr_t.obj_flags[] - the FIRST LONGWORD of the 0x90-byte attribute
 * record, copied verbatim by AST_$GET_ACL_ATTRIBUTES (`move.l (-0x90,A6),(A2)`
 * at 0x00E04AD6).  AST_$GET_ATTRIBUTES fills that record from aote+0x0C
 * onwards (0x00E049A2), so the four bytes are aote+0x0C..+0x0F - exactly the
 * four bytes AST_$GET_COMMON_ATTRIBUTES copies into ast_$common_attr_t's
 * obj_type / sub_type / attr_flags_hi / attr_flags_lo (0x00E04A2C).
 * (source-qg0q.)
 *
 *   [0] obj_type       aote+0x0C.  acl/ only ever asks whether it is non-zero,
 *                      i.e. "the object has a type, so it has a protection
 *                      record" (`move.b (-0x80,A6),D0b` + `tst.w` at
 *                      0x00E466F0).
 *   [1] sub_type       aote+0x0D (`move.b (-0x7f,A6),D0b` at 0x00E46690); this
 *                      is the byte compared against ACL_$RIGHTS' option_flags
 *                      word, and the one acl_$get_obj_acl_attrs forces to 3
 *                      for the well-known volume UIDs (0x00E46074).
 *   [2] attr_flags_hi  aote+0x0E, with bits 1..0 replaced from aote+0x71
 *                      bits 5..4.  acl/ never reads it.
 *   [3] attr_flags_lo  aote+0x0F.  Bit 0 = "the ACL is held locally"; when
 *                      clear and the object location record says remote,
 *                      acl_$eval_rights forwards the whole question to
 *                      REM_FILE_$ACL_CHECK_RIGHTS (`btst.b #0x0,(-0x7d,A6)`
 *                      at 0x00E46606).
 *
 * TODO(source-qg0q, 0x00E04AD6): ast/ast.h still declares the longword as
 * `uint8_t obj_flags[4]`; splitting it into the four named bytes belongs to
 * ast/, and file/ and rem_file/ index it too.
 */
#define ACL_ATTR_OBJ_TYPE       0
#define ACL_ATTR_SUB_TYPE       1
#define ACL_ATTR_FLAGS_HI       2
#define ACL_ATTR_FLAGS_LO       3
/* Bit 0 of attr_flags_lo. */
#define ACL_ATTR_FLAG_LOCAL     0x01

/*
 * acl_$cache_slot_t - one slot of the in-memory ACL image cache at 0xE88834.
 *
 * ACL_$INIT zeroes 0xE88834..0xE935CC (0x00E310AA-0x00E310C0) and builds a
 * 31-entry circular free list with `divs.w #0x1f` (0x00E31140-0x00E3117A), so
 * the cache holds 31 slots of 0x400 bytes: 0xE88834 + 31*0x400 = 0xE90434,
 * exactly where ACL_$ORIGINAL_SIDS[1] begins.
 *
 * acl_$eval_rights indexes it with `lsl.l #0x8` + `lsl.l #0x2` (= *0x400) at
 * 0x00E4680C and 0x00E46870.  ACL_$SET_ACL_CHECK uses the same stride.
 *
 * Packed: type_uid lands on an odd multiple of two, which the m68k ABI allows
 * but a 4-byte-aligning host would pad.
 */
#define ACL_CACHE_SLOTS         31
#define ACL_CACHE_SLOT_SIZE     0x400
#define ACL_CACHE_NO_SLOT       ((int16_t)-1)

typedef struct __attribute__((packed)) acl_$cache_slot_t {
    int16_t  version;           /* 0x00: image format version.  acl_$load_acl_image
                                 *       tests it with `cmpi.w #0x3,(A2)`
                                 *       (0x00E45BF6) and `cmpi.w #0x5,(A2)` +
                                 *       `bge` (0x00E45C52); acl_$convert_image
                                 *       stamps 5 into the image it builds
                                 *       (`move.w #0x5,(A2)`, 0x00E44E86).  The
                                 *       compare is signed, hence int16_t. */
    uid_t    type_uid;          /* 0x02: ACL_$FILE_ACL / ACL_$DIR_ACL / ...
                                 *       (0x00E4745C, 0x00E4763A) */
    uint32_t reserved_0a;       /* 0x0A */
    uint16_t entry_count;       /* 0x0E: `move.w (0xe,A1),D2w`, 0x00E461B6 */
    uint16_t reserved_10;       /* 0x10 */
    uid_t    required_uid;      /* 0x12: 0x00E474E6, 0x00E475C6 */
    uid_t    subsys_uid;        /* 0x1A: subsystem manager; compared against
                                 *       sids->login_sid at 0x00E46828 */
    uint32_t reserved_22;       /* 0x22: `clr.l (0x22,A1)` 0x00E45C1C */
    uint16_t reserved_26;       /* 0x26: `clr.w (0x26,A1)` 0x00E45C20 */
    int8_t   world_entry_present;
                                /* 0x28: Pascal boolean.  Its ONE reader is the
                                 *       version-3/4 directory fixup in
                                 *       acl_$load_acl_image: `tst.b (0x28,A2)`
                                 *       + `bmi` at 0x00E45CA0 skips appending
                                 *       the all-nil (person = group = org =
                                 *       UID_$NIL) entry with rights
                                 *       ACL_V4_RIGHTS_DEFAULT when the flag is
                                 *       negative.  That all-nil entry is what
                                 *       acl_$convert_image turns into
                                 *       prot->world_rights (0x00E44F44-
                                 *       0x00E44F56), so a true flag means "this
                                 *       image already carries its world entry".
                                 *       The version-3 promotion clears it unless
                                 *       the image is a directory ACL
                                 *       (0x00E45C38); every version-5 image the
                                 *       kernel builds clears it outright
                                 *       (acl_$convert_image 0x00E44EC6,
                                 *       acl_$image_internal 0x00E47DD8). */
    int8_t   unused_29;         /* 0x29: no reader anywhere in the image; the
                                 *       three writers only ever clear it
                                 *       alongside world_entry_present
                                 *       (0x00E45C3C, 0x00E44ECA, 0x00E47DDC). */
    uint8_t  reserved_2a[0x0A]; /* 0x2A..0x33: five words the version-3 fixup
                                 *       zeroes (0x00E45C40-0x00E45C4E) */
    uint8_t  entries[0x3CC];    /* 0x34: 0x20-byte ACL entries
                                 *       (`lea (0x34,A0),A0` + `lea (0x20,A0),A0`
                                 *        at 0x00E461AC / 0x00E46232) */
} acl_$cache_slot_t;

#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(acl_$cache_slot_t, type_uid)    == 0x02, "cache.type_uid");
_Static_assert(__builtin_offsetof(acl_$cache_slot_t, world_entry_present) == 0x28,
               "cache.world_entry_present");
_Static_assert(__builtin_offsetof(acl_$cache_slot_t, unused_29)  == 0x29, "cache.unused_29");
_Static_assert(__builtin_offsetof(acl_$cache_slot_t, entry_count) == 0x0E, "cache.entry_count");
_Static_assert(__builtin_offsetof(acl_$cache_slot_t, required_uid)== 0x12, "cache.required_uid");
_Static_assert(__builtin_offsetof(acl_$cache_slot_t, subsys_uid)  == 0x1A, "cache.subsys_uid");
_Static_assert(__builtin_offsetof(acl_$cache_slot_t, entries)     == 0x34, "cache.entries");
_Static_assert(sizeof(acl_$cache_slot_t) == ACL_CACHE_SLOT_SIZE, "sizeof acl_$cache_slot_t");
#endif

extern acl_$cache_slot_t ACL_$ACL_CACHE[ACL_CACHE_SLOTS];   /* 0xE88834 */

/*
 * ----------------------------------------------------------------------------
 * The ACL image cache directory (ACL module A5 data, A5 = 0xE7CF54)
 * ----------------------------------------------------------------------------
 *
 * ACL_$ACL_CACHE holds the 31 raw 0x400-byte ACL images.  The bookkeeping that
 * decides which image lives in which slot is a set of parallel A5-relative
 * arrays that acl_$find_acl_slot (0x00E45E8E) walks:
 *
 *   A5+0x800  ACL_$CACHE_DIR[31]          16 bytes each  (0xE7D754)
 *   A5+0x9F0  ACL_$CACHE_LRU_LINKS[32]     4 bytes each  (0xE7D944)
 *   A5+0xA70  ACL_$CACHE_HASH_LINKS[32]    4 bytes each  (0xE7D9C4)
 *   A5+0xAF0  ACL_$CACHE_HASH_BUCKETS[64]  2 bytes each  (0xE7DA44)
 *   A5+0xB74  ACL_$CACHE_FREE_HEAD                       (0xE7DAC8)
 *   A5+0xB76  ACL_$CACHE_LRU_HEAD                        (0xE7DACA)
 *
 * The addresses are contiguous and each array ends exactly where the next
 * begins, which is what fixes their element counts:
 * 0x800 + 31*0x10 = 0x9F0, 0x9F0 + 32*4 = 0xA70, 0xA70 + 32*4 = 0xAF0 and
 * 0xAF0 + 64*2 = 0xB70 (= ACL_$LOCAL_LOCKSMITH).
 *
 * NOTE: 0xE7DACA is also the address of ACL_$SUPER_COUNT[0].  Process numbers
 * are 1-based, so element 0 of that array is never used and the LRU head lives
 * inside it - the same trick ACL_$ASID_FREE_BITMAP plays inside
 * ACL_$PROJ_UIDS[0].  ACL_$ENTER_SUPER's `addq.w #0x1,(0xb76,A0)` with
 * A0 = A5 + PROC1_$CURRENT*2 (0x00E46FA8) is what pins the array base there.
 */

/* UID_$HASH modulus for the ACL cache: the word at 0x00E45E8C, reached by the
 * `pea (-0x20,PC)` at 0x00E45EAA.  Raw bytes 00 3D. */
#define ACL_CACHE_HASH_MOD          61
/* The bucket array runs to A5+0xB70, so 64 words are reserved for the 61 the
 * modulus can produce. */
#define ACL_CACHE_HASH_BUCKETS      64
/* Both link arrays are 32 elements wide even though only slots 0..30 exist. */
#define ACL_CACHE_LINK_SLOTS        32

/*
 * One directory entry: which ACL UID a slot currently holds, plus the two
 * rights bytes acl_$find_acl_slot replays into the caller's protection record
 * when the cached image is a "default ACL" (flag byte negative).
 */
typedef struct acl_$cache_dir_t {
    uid_t    acl_uid;           /* 0x00: `cmpm.l` pair at 0x00E45F06 */
    uint16_t hash_bucket;       /* 0x08: the UID_$HASH bucket this slot is
                                 *       chained in.  acl_$load_acl_image stores
                                 *       it (`move.w D0w,(0x808,A2)`, 0x00E45E5E)
                                 *       and acl_$alloc_cache_slot reads it back
                                 *       to unlink an evicted slot from its
                                 *       bucket (`move.w (0x808,A0),D2w`,
                                 *       0x00E45954). */
    int8_t   cached_flag;       /* 0x0A: `move.b (0x80a,A2),(A1)` 0x00E45F10 */
    uint8_t  reserved_0b;       /* 0x0B */
    uint16_t world_rights;      /* 0x0C: WORD - acl_$load_acl_image widens
                                 *       prot->world_rights into it
                                 *       (`move.w D5w,(0x80c,A2)`, 0x00E45E40).
                                 *       acl_$find_acl_slot reads only its low
                                 *       byte at +0x0D (0x00E45F24). */
    uint16_t subsys_rights;     /* 0x0E: WORD, same treatment
                                 *       (`move.w D5w,(0x80e,A2)`, 0x00E45E4A;
                                 *        low byte read at +0x0F, 0x00E45F2A) */
} acl_$cache_dir_t;

#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(acl_$cache_dir_t, acl_uid)       == 0x00, "cache_dir.acl_uid");
_Static_assert(__builtin_offsetof(acl_$cache_dir_t, cached_flag)   == 0x0A, "cache_dir.cached_flag");
_Static_assert(__builtin_offsetof(acl_$cache_dir_t, hash_bucket)   == 0x08, "cache_dir.hash_bucket");
_Static_assert(__builtin_offsetof(acl_$cache_dir_t, world_rights)  == 0x0C, "cache_dir.world_rights");
_Static_assert(__builtin_offsetof(acl_$cache_dir_t, subsys_rights) == 0x0E, "cache_dir.subsys_rights");
_Static_assert(sizeof(acl_$cache_dir_t) == 0x10, "sizeof acl_$cache_dir_t");
#endif

/*
 * One node of a circular doubly-linked slot list.  acl_$cache_list_insert /
 * acl_$cache_list_remove read `next` at +0 and `prev` at +2 (0x00E44C54,
 * 0x00E44CB2) and index the array with `lsl.l #0x2`.
 */
typedef struct acl_$cache_link_t {
    int16_t next;               /* 0x00 */
    int16_t prev;               /* 0x02 */
} acl_$cache_link_t;

#if defined(ARCH_M68K)
_Static_assert(sizeof(acl_$cache_link_t) == 4, "sizeof acl_$cache_link_t");
#endif

extern acl_$cache_dir_t  ACL_$CACHE_DIR[ACL_CACHE_SLOTS];            /* 0xE7D754 */
extern acl_$cache_link_t ACL_$CACHE_LRU_LINKS[ACL_CACHE_LINK_SLOTS]; /* 0xE7D944 */
extern acl_$cache_link_t ACL_$CACHE_HASH_LINKS[ACL_CACHE_LINK_SLOTS];/* 0xE7D9C4 */
extern int16_t ACL_$CACHE_HASH_BUCKETS_TAB[ACL_CACHE_HASH_BUCKETS];  /* 0xE7DA44 */
extern int16_t ACL_$CACHE_FREE_HEAD;                                 /* 0xE7DAC8 */
extern int16_t ACL_$CACHE_LRU_HEAD;                                  /* 0xE7DACA */

/*
 * acl_$acl_entry_t - one 0x20-byte entry of a cached ACL image.
 *
 * acl_$eval_acl_entries walks them with `lea (0x34,A0),A0` + `lea (0x20,A0),A0`
 * (0x00E461AC / 0x00E46232) and also addresses entry i directly as
 * `lea (0x14,slot,i*0x20)` (0x00E462DE) - so the entries are numbered from 1
 * and entry i starts at slot+0x14+i*0x20 = &slot->entries[(i-1)*0x20].
 */
typedef struct acl_$acl_entry_t {
    uid_t    person;            /* 0x00: 0x00E461C6 */
    uid_t    group;             /* 0x08: 0x00E461DC */
    uid_t    org;               /* 0x10: 0x00E46202 */
    uint16_t reserved_18;       /* 0x18 */
    uint16_t rights;            /* 0x1A: `and.w (0x1a,A0),D0w` 0x00E46228 */
    uint32_t reserved_1c;       /* 0x1C */
} acl_$acl_entry_t;

#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(acl_$acl_entry_t, group)  == 0x08, "acl_entry.group");
_Static_assert(__builtin_offsetof(acl_$acl_entry_t, org)    == 0x10, "acl_entry.org");
_Static_assert(__builtin_offsetof(acl_$acl_entry_t, rights) == 0x1A, "acl_entry.rights");
_Static_assert(sizeof(acl_$acl_entry_t) == 0x20, "sizeof acl_$acl_entry_t");
#endif

/* Entry i (1-based) of a cached image.  slot+0x34 is entry 1. */
#define ACL_$CACHE_ENTRY(slot, i)                                             \
    ((acl_$acl_entry_t *)((uint8_t *)(slot)->entries + ((int32_t)(i) - 1) * 0x20))

/*
 * acl_$v4_entry_t - one entry of a PRE-version-5 (version 3 or 4) ACL image.
 *
 * acl_$load_acl_image walks these with a base pointer of `slot + i*0x2C` and
 * field displacements of +0x08..+0x30 (0x00E45C7E-0x00E45D54), i.e. entry i
 * (1-based) starts at slot+0x34+(i-1)*0x2C - the same place version-5 entry i
 * starts, but 0x2C bytes wide instead of 0x20.  acl_$convert_image reads them
 * with the same stride (`lea (0x2c,A4),A2`, 0x00E44EF8).
 *
 * Every member is naturally aligned inside the record, so no packing
 * attribute is needed - the _Static_asserts below pin the layout.
 */
typedef struct acl_$v4_entry_t {
    uid_t    person;            /* 0x00: `(0x08,A0)` 0x00E45CD8 */
    uid_t    group;             /* 0x08: `(0x10,A0)` 0x00E45CEC */
    uid_t    org;               /* 0x10: `(0x18,A0)` 0x00E45CC4 */
    uid_t    subsys;            /* 0x18: `(0x20,A0)` 0x00E45D44 */
    uint32_t reserved_20;       /* 0x20: `tst.l (0x28,A0)` 0x00E45CBE */
    uint32_t reserved_24;       /* 0x24: `clr.l (0x2c,A2)` 0x00E45D50 */
    uint32_t rights;            /* 0x28: `(0x30,A0)` 0x00E45C86 / 0x00E45D54 */
} acl_$v4_entry_t;

#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(acl_$v4_entry_t, subsys) == 0x18, "v4_entry.subsys");
_Static_assert(__builtin_offsetof(acl_$v4_entry_t, rights) == 0x28, "v4_entry.rights");
_Static_assert(sizeof(acl_$v4_entry_t) == 0x2C, "sizeof acl_$v4_entry_t");
#endif

/* Version-3/4 entry i (1-based).  slot+0x34 is entry 1, stride 0x2C. */
#define ACL_$V4_ENTRY(slot, i)                                                \
    ((acl_$v4_entry_t *)((uint8_t *)(slot)->entries + ((int32_t)(i) - 1) * 0x2C))

/*
 * The largest entry index the version-3/4 "append the missing required entry"
 * fixup will write: `cmpi.w #0x16,(0xe,A2)` + `bge` at 0x00E45CB4 gives up when
 * the image already holds 0x16 entries.
 */
#define ACL_V4_MAX_ENTRIES          0x16

/*
 * The rights bits acl_$load_acl_image forces into every directory-ACL entry of
 * a pre-version-5 image whose bit 29 is clear (`btst.l #0x1d` +
 * `ori.l #0x200001e0,(0x30,A0)`, 0x00E45C8A-0x00E45C90), and the value it gives
 * the required entry it appends (`move.l #0x1e0,(0x30,A2)`, 0x00E45D54).
 */
#define ACL_V4_RIGHTS_CONVERTED     0x20000000UL
#define ACL_V4_RIGHTS_DEFAULT       0x000001E0UL
#define ACL_V4_RIGHTS_FIXUP         (ACL_V4_RIGHTS_CONVERTED | ACL_V4_RIGHTS_DEFAULT)

/*
 * ACL_$IMAGE_BUF (A5+0x400 = 0xE7D354) - the 0x400-byte scratch image
 * acl_$load_acl_image hands to acl_$convert_image (`pea (0x400,A5)`,
 * 0x00E45D64) and then copies into ACL_$ACL_CACHE[slot] (0x00E45DD0).  It ends
 * exactly where ACL_$CACHE_DIR (A5+0x800) begins, which fixes its size.
 */
extern acl_$cache_slot_t ACL_$IMAGE_BUF;    /* 0xE7D354 */

/*
 * The version-5 rights-byte bits acl_$convert_rights produces.  Bits 0..3 are
 * the four rights ACL_RIGHTS_ALL covers; bit 6 is the one ACL_RIGHTS_PRIVILEGED
 * also withholds.  Named by bit number rather than by right letter: nothing in
 * the image spells the letters out.
 */
#define ACL_V5_RIGHT_0              0x01
#define ACL_V5_RIGHT_1              0x02
#define ACL_V5_RIGHT_2              0x04
#define ACL_V5_RIGHT_3              0x08
#define ACL_V5_RIGHT_6              0x40

/*
 * The pre-version-5 32-bit rights bits acl_$convert_rights reads
 * (`btst.l #n,D1`, 0x00E44DE2-0x00E44E58).
 */
#define ACL_V4_RIGHT_0              0x00000001UL
#define ACL_V4_RIGHT_1              0x00000002UL
#define ACL_V4_RIGHT_2              0x00000004UL
#define ACL_V4_RIGHT_3              0x00000008UL
#define ACL_V4_RIGHT_4              0x00000010UL
#define ACL_V4_RIGHT_5              0x00000020UL
#define ACL_V4_RIGHT_6              0x00000040UL
/* Bit 25 - the bit acl_$expand_default_acl forces on before it converts an
 * encoded default-ACL rights word (ACL_CONVERT_RIGHTS_DEFAULT). */
#define ACL_V4_RIGHT_25             0x02000000UL

/*
 * acl_$convert_rights (0x00E44DBE, was FUN_00e44dbe), 170 bytes
 *
 * Maps a pre-version-5 32-bit ACL rights word onto the version-5 rights byte.
 * `acl_type_uid` selects the bit assignment; a UID that is neither
 * ACL_$FILE_ACL nor ACL_$DIR_ACL contributes nothing but bit 25.
 *
 *   ACL_$FILE_ACL     old 1 -> new 2, old 2 -> new 1, old 0 -> new 0,
 *                     old 3 CLEAR -> new 6
 *   ACL_$DIR_ACL      old 0 -> new 2, old 3&1&2&6 -> new 1, old 5 -> new 0,
 *                     old 4 CLEAR -> new 6
 *   both              old 25 -> new 3
 *
 * Module-level: no static link (`movea.l (A6),An`) and no A5 reference; it
 * reads ACL_$FILE_ACL / ACL_$DIR_ACL through absolute addresses.
 */
uint8_t acl_$convert_rights(uint32_t old_rights, uid_t *acl_type_uid);

/*
 * acl_$convert_image (0x00E44E68, was FUN_00e44e68)
 *
 * Rewrites the version-3/4 image `src` as a version-5 image in `dst` and
 * rebuilds `prot` from it.  *length_ret = 0x34 + 0x20 * dst->entry_count
 * (0x00E4502C-0x00E4503A).  Argument order from acl_$load_acl_image's pushes
 * at 0x00E45D5C-0x00E45D6C.
 *
 * Module-level: no static link and no A5 reference.
 */
void acl_$convert_image(acl_$cache_slot_t *src, acl_$prot_data_t *prot,
                        acl_$cache_slot_t *dst, uint16_t *length_ret,
                        status_$t *status_ret);

/*
 * acl_$expand_default_acl (0x00E45984, was FUN_00e45984)
 *
 * A "default ACL" carries its whole definition inside the ACL UID: the high
 * word of uid.high is 1 (file) or 2 (directory) and the low word holds the
 * rights bits.  Returns false when the UID is neither - a real ACL object that
 * acl_$load_acl_image has to map - and true after filling `prot` from
 * ACL_$DEF_ACLDATA plus the encoded rights.
 */
boolean acl_$expand_default_acl(uid_t *acl_uid, acl_$prot_data_t *prot);

/*
 * acl_$alloc_cache_slot (0x00E458E4, was FUN_00e458e4)
 *
 * Takes the head of the free list, or evicts the LRU victim, and returns the
 * slot.  The returned slot is linked into no list; the caller does that.
 * Always clears *status_ret and never sets it.
 */
int16_t acl_$alloc_cache_slot(status_$t *status_ret);

/* The two "default ACL" type words in the high half of a default-ACL UID
 * (`cmpi.w #0x1` / `#0x2` at 0x00E459C4 / 0x00E459CA). */
#define ACL_DEFAULT_TYPE_FILE       1
#define ACL_DEFAULT_TYPE_DIR        2
/* Rights-bit surgery acl_$expand_default_acl performs on the encoded word:
 * bit 13 set means "use the bits as they are" and is cleared
 * (`btst.b #0x5` / `bclr.b #0x5` on the HIGH byte, 0x00E459E6/0x00E459EE);
 * otherwise a directory default ACL gets 0x1E0 OR'd in (0x00E459FE).  What
 * survives is masked with 0x3FFF (`move.l #0x3fff,D0`, 0x00E45A04) and handed
 * to acl_$convert_rights with bit 25 set (0x00E45A44). */
#define ACL_DEFAULT_RIGHTS_LITERAL  0x2000
#define ACL_DEFAULT_RIGHTS_DIR_ADD  0x01E0
#define ACL_DEFAULT_RIGHTS_MASK     0x3FFF
#define ACL_CONVERT_RIGHTS_DEFAULT  0x02000000UL

/*
 * acl_$cache_list_insert (0x00E44C3C, was FUN_00e44c3c)
 * acl_$cache_list_remove (0x00E44C92, was FUN_00e44c92)
 *
 * The two circular-doubly-linked-list primitives the ACL image cache runs its
 * LRU and free lists on.  `head` is -1 when the list is empty; insert makes
 * `slot` the new head, remove unlinks it (and empties the list when it was the
 * only member).  Neither touches the entry it unlinks.
 */
void acl_$cache_list_insert(int16_t *head, acl_$cache_link_t *links, int16_t slot);
void acl_$cache_list_remove(int16_t *head, acl_$cache_link_t *links, int16_t slot);

/*
 * acl_$load_acl_image (0x00E45A60, was FUN_00e45a60)
 *
 * acl_$find_acl_slot's miss path: takes a free (or LRU-victim) slot, maps and
 * reads the ACL object into ACL_$ACL_CACHE and returns the slot index, or
 * ACL_CACHE_NO_SLOT.  Argument order from acl_$find_acl_slot's pushes at
 * 0x00E45ECE-0x00E45ED4 and confirmed by the callee frame
 * (A6+0x08 uid, +0x0C flag, +0x10 prot, +0x14 status).
 */
int16_t acl_$load_acl_image(uid_t *acl_uid, int8_t *cached_flag_ret,
                            acl_$prot_data_t *prot, status_$t *status_ret);


/*
 * acl_$prim_create_internal (0x00E4519C, 1864 bytes; was FUN_00e4519c).
 * Module-local - the ACL_ code segment starts at 0xE44C3C and the SAU2 map
 * exports no symbol at 0xE4519C - and reached with `bsr.w` from
 * ACL_$PRIM_CREATE (0x00E47ACA).
 *
 * Frame, from the callee's own reads:
 *   A6+0x08  acl_data      (A4)
 *   A6+0x0C  acl_header    pointer, copied to A6-0x14 and dereferenced
 *                          at 0x00E451C0 (`lea (0x12,A0),A2`)
 *   A6+0x10  data_len      word (`tst.w`, signed)
 *   A6+0x12  subsys_uid    pointer to the caller's acl_data+2
 *   A6+0x16  flag          BYTE (D5)
 *   A6+0x18  image         the mapped 0x400-byte page, copied to A6-0x10
 *   A6+0x1C  image_len_ret out: WORD, 0x34 + entries*0x2C (0x00E458CE-0x00E458D8)
 *   A6+0x20  status
 */
void acl_$prim_create_internal(void *acl_header, void *acl_data, int16_t data_len,
                               void *subsys_uid, int16_t flag, void *image,
                               int16_t *image_len_ret, status_$t *status_ret);


/*
 * Project list entry (12 bytes per process)
 */
typedef struct acl_proj_list_t {
    uint32_t field_00;
    uint32_t field_04;
    uint32_t field_08;
} acl_proj_list_t;

/*
 * ============================================================================
 * ACL Global Variables
 * ============================================================================
 *
 * Original m68k addresses documented for reference.
 * A5-relative base: 0xE7CF54
 */

/*
 * ACL data base address: 0xE88834
 * Total size: 0xAD98 bytes (zeroed by ACL_$INIT)
 */

/*
 * Per-process SID arrays (indexed by PID, stride 0x24 = 36 bytes)
 * Each holds 9 uint32_t values (36 bytes) per process.
 */

/* Current SIDs: base 0xE90D10, offset from A5-relative 0xE97294 is -0x6584 */
extern acl_sid_block_t ACL_$CURRENT_SIDS[PROC1_MAX_PROCESSES];  /* 0xE90D10 */

/* Saved SIDs (pre-enter_super): base 0xE91610, offset -0x5C84 */
extern acl_sid_block_t ACL_$SAVED_SIDS[PROC1_MAX_PROCESSES];    /* 0xE91610 */

/* Original SIDs (pre-enter_subs): base 0xE90410, offset -0x6E84 */
extern acl_sid_block_t ACL_$ORIGINAL_SIDS[PROC1_MAX_PROCESSES]; /* 0xE90410 */

/*
 * Project lists metadata (indexed by PID, stride 0x0C = 12 bytes)
 * Used by SET/GET_RE_ALL_SIDS for opaque 12-byte project metadata
 */
extern acl_proj_list_t ACL_$PROJ_LISTS[PROC1_MAX_PROCESSES];    /* 0xE92228 */
extern acl_proj_list_t ACL_$SAVED_PROJ[PROC1_MAX_PROCESSES];    /* 0xE91F28 */

/*
 * Per-process project UID array (indexed by PID, 8 UIDs per process).
 * Used by ADD_PROJ/DELETE_PROJ/GET_PROJ_LIST/SET_PROJ_LIST and read by
 * acl_$eval_rights.  Stride 0x40 = 64 bytes per process (8 UIDs * 8 bytes).
 *
 * BASE = 0xE924FC (source-4h7g).  ACL_$ADD_PROJ (0x00E47EFE-0x00E47F06) walks
 * the row with D2 starting at 8 and A3 = 0xE97294 + cur*0x40 + D2 - 0x4DA0,
 * so the first slot it touches is 0xE924F4 + cur*0x40 + 8 = 0xE924FC +
 * cur*0x40.  ACL_$INIT settles it: at 0x00E31122-0x00E3113C it stores UID_$NIL
 * to eight consecutive slots starting at 0xE9729C - 0x4D60 = 0xE9253C, which
 * is 0xE924FC + 1*0x40 - process 1's row, element 0.  Processes are numbered
 * from 1 (ACL_$INIT's ACL_$FREE_ASID loop runs D3 = 1..64), so row 0 is never
 * used; ACL_$ASID_FREE_BITMAP at 0xE92534 lives inside it.
 */
extern uid_t ACL_$PROJ_UIDS[PROC1_MAX_PROCESSES][ACL_MAX_PROJECTS]; /* 0xE924FC */

/*
 * Per-process subsystem level counter (indexed by PID, stride 2)
 * Incremented by ACL_$UP, decremented by ACL_$DOWN
 * Offset from 0xE97294: -0x3D5A = 0xE9353A
 */
extern int16_t ACL_$SUBSYS_LEVEL[PROC1_MAX_PROCESSES];          /* 0xE9353A */

/*
 * Per-process superuser mode counter (indexed by PID, stride 2)
 * Incremented by ENTER_SUPER, decremented by EXIT_SUPER
 * A5+0xB76 = 0xE7DACA
 */
extern int16_t ACL_$SUPER_COUNT[PROC1_MAX_PROCESSES];           /* 0xE7DACA */

/*
 * Per-process bitmaps (8 bytes each, 64 bits for processes 1..64).
 *
 * All three are addressed the same way: byte (pid-1)>>3, bit (pid-1)&7
 * counted from the MOST significant bit, i.e. mask 0x80 >> ((pid-1)&7).
 * acl_$check_suser_pid builds the mask with `move.l #0x80,D4` +
 * `lsr.l D3,D4` (0x00E46472-0x00E4647C).
 */
#define ACL_PID_BITMAP_BYTE(pid)    ((uint16_t)(((uint8_t)(pid) - 1u) >> 3))
#define ACL_PID_BITMAP_MASK(pid)    ((uint8_t)(0x80u >> (((uint8_t)(pid) - 1u) & 7u)))

/*
 * ASID and PID are the SAME number as far as every table on this page is
 * concerned (bead source-x5dd).  ACL_$ALLOC_ASID (0x00E73BB8) copies row
 * PROC1_$CURRENT (the word at 0xE20608) into row `new_asid` (its A6+0x08 word)
 * using identical bases, biases and strides for all six arrays -
 * -0x6E84/0x24, -0x6584/0x24, -0x5C84/0x24, -0x4D98/0x40, -0x536C/0x0C and
 * -0x506C/0x0C off 0xE97294 (0x00E73BCC-0x00E73C98) - and then applies the
 * (n-1)>>3 / 0x80>>((n-1)&7) bitmap arithmetic to PROC1_$CURRENT's low byte
 * against ACL_$ASID_FREE_BITMAP (0x00E73C9A-0x00E73CBA).  ACL_$FREE_ASID
 * (0x00E74C6A) uses exactly the same bases with its `asid` argument.  If the
 * two were different numbering spaces, the row ACL_$ALLOC_ASID writes would
 * never be the row acl_$eval_rights and ACL_$RIGHTS read.
 *
 * 0xE20608 (PROC1_$CURRENT) and 0xE2060A (PROC1_$AS_ID) are still separate
 * cells; only the ACL tables treat their contents interchangeably.
 */
extern uint8_t ACL_$ASID_FREE_BITMAP[8];    /* 0xE92534: 1=free */
/*
 * 1 = the process was granted locksmith rights while ACL_$LOCAL_LOCKSMITH was
 * in force; set by ACL_$OVERRIDE_LOCAL_LOCKSMITH (0x00E492BC) and tested by
 * acl_$eval_rights (0x00E4657C) and ACL_$SET_ACL_CHECK (0x00E473A4).
 */
extern uint8_t ACL_$LOCKSMITH_OVERRIDE_BITMAP[8];   /* 0xE935BC */
extern uint8_t ACL_$ASID_SUSER_BITMAP[8];   /* 0xE935C4: 1=used suser */

/*
 * Locksmith state (A5+0xB70 = 0xE7DAC4).
 *
 * 0 = the locksmith SIDs keep full rights.  Non-zero = a type-9 process
 * holding a locksmith SID is downgraded to the generic user identity
 * (acl_$eval_rights 0x00E4653E-0x00E465D6); the value 1 additionally grants
 * back read+execute.
 */
extern int16_t ACL_$LOCAL_LOCKSMITH;        /* 0xE7DAC4 */

/*
 * Locksmith override state
 */
extern int16_t ACL_$LOCKSMITH_OWNER_PID;    /* 0xE7DAC6 (A5+0xB72) */
extern int8_t ACL_$LOCKSMITH_OVERRIDE;      /* 0xE7DB4C (A5+0xBF8) */

/*
 * Subsystem entry magic value (stored on first entry)
 * A5+0xB6C = 0xE7DAC0
 */
extern int32_t ACL_$SUBS_MAGIC;             /* 0xE7DAC0 */

/*
 * Exclusion lock for ACL operations
 */
extern ml_$exclusion_t ACL_$EXCLUSION_LOCK; /* 0xE2C014 */

/*
 * ACL workspace buffer - the ACL_ module data block's first object
 * (A5 + 0, A5 = 0xE7CF54; `D E7CF54 ACL_ size = BFC` in the SAU2 map).
 *
 * ACL_$CONVERT_TO_9ACL and ACL_$CONVERT_FROM_9ACL hand it to
 * acl_$image_internal / ACL_$PRIM_CREATE as the image buffer, and both pass
 * 0x400 as the buffer's capacity, so it is a full 0x400-byte ACL image.  That
 * is also exactly the distance to the next object in the block, ACL_$IMAGE_BUF
 * at A5 + 0x400 (0xE7D354).
 */
#define ACL_WORKSPACE_SIZE 0x400
extern uint8_t ACL_$WORKSPACE[ACL_WORKSPACE_SIZE];  /* 0xE7CF54 */

/*
 * acl_$image_t - the head of the ACL image acl_$image_internal builds in that
 * workspace, and the only part of it any caller edits by hand.
 *
 * Recovered from acl_$image_internal's default-image arm (0x00E47D62 -
 * 0x00E47DDC, A1 = the image buffer):
 *
 *   0x00E47D64  move.w #0x34,(A0)          the length it reports back
 *   0x00E47DB4  move.w #0x5,(A1)           version
 *   0x00E47D80/88  ACL_$DIR_ACL (0xE1744C) or ACL_$FILE_ACL (0xE17444)
 *   0x00E47D8E  move.l (A2)+,(0x2,A1)      that UID, +0x02..+0x09
 *   0x00E47D92  move.l (A2)+,(0x6,A1)
 *   0x00E47D96  clr.w (0xe,A1)
 *   0x00E47D9A  UID_$NIL (0xE1737C)
 *   0x00E47DA0  move.l (A2)+,(0x12,A1)     +0x12..+0x19
 *   0x00E47DA8  lea (0x2,A1),A2
 *   0x00E47DAC  move.l (A2)+,(0x1a,A1)     a SECOND copy of +0x02..+0x09
 *   0x00E47DB0  move.l (A2)+,(0x1e,A1)
 *   0x00E47DC8  clr.l (0xa,A1) / clr.w (0x10,A1) / clr.l (0x22,A1) /
 *               clr.w (0x26,A1) / clr.b (0x28,A1) / clr.b (0x29,A1)
 *   0x00E47DB8  moveq #0xb,D0 / lea (0x2,A1),A0 / clr.w (0x28,A0) /
 *               addq.l #0x2,A0 / dbf  - twelve words at +0x2A..+0x41
 *
 * ACL_$CONVERT_TO_9ACL overwrites BOTH eight-byte UIDs with the caller's
 * default protection when the source UID is nil (0x00E48D8C-0x00E48D9A), so
 * the record exists to pin those two offsets.  (source-9j7d)
 */
typedef struct __attribute__((packed)) acl_$image_t {
    uint16_t    version;            /* 0x00: 5, or 0 on the empty arm */
    uid_t       acl_uid;            /* 0x02: the manager UID */
    uint32_t    reserved_0a;        /* 0x0A */
    uint16_t    reserved_0e;        /* 0x0E */
    uint16_t    reserved_10;        /* 0x10 */
    uid_t       subsys_uid;         /* 0x12: UID_$NIL in the default image */
    uid_t       initial_acl_uid;    /* 0x1A: a second copy of acl_uid */
    uint32_t    reserved_22;        /* 0x22 */
    uint16_t    reserved_26;        /* 0x26 */
    uint8_t     reserved_28;        /* 0x28 */
    uint8_t     reserved_29;        /* 0x29 */
    uint16_t    rights[12];         /* 0x2A: the dbf loop's twelve words */
} acl_$image_t;

_Static_assert(offsetof(acl_$image_t, acl_uid)         == 0x02, "acl_image.acl_uid");
_Static_assert(offsetof(acl_$image_t, subsys_uid)      == 0x12, "acl_image.subsys_uid");
_Static_assert(offsetof(acl_$image_t, initial_acl_uid) == 0x1A, "acl_image.initial_acl_uid");
_Static_assert(offsetof(acl_$image_t, rights)          == 0x2A, "acl_image.rights");
_Static_assert(sizeof(acl_$image_t) == 0x42, "acl_$image_t must be 0x42 bytes");

/*
 * Default ACL UIDs (referenced in acl.h, defined here for internal use)
 * These are loaded from RGYC during initialization.
 */
/* ACL_$DNDCAL - 0xE174DC */
/* ACL_$FNDWRX - 0xE174C4 */

/*
 * ACL type UIDs - used to identify ACL operations
 */
extern uid_t ACL_$FILE_ACL;         /* 0xE17444 (Ghidra label) */
extern uid_t ACL_$FILEIN_ACL;       /* 0xE17454 */
/* ACL_$DIRIN_ACL (0xE1745C) is declared in acl/acl.h */
extern uid_t ACL_$DIR_MERGE_ACL;    /* 0xE17464 */
extern uid_t ACL_$FILE_MERGE_ACL;   /* 0xE1746C */
extern uid_t ACL_$FILE_SUBS_ACL;    /* 0xE17474 */

/*
 * ============================================================================
 * Internal Helper Functions
 * ============================================================================
 */

/*
 * acl_$eval_rights - Core access-rights evaluator (0x00E464B8)
 *
 * Shared by ACL_$RIGHTS (0x00E46A00) and ACL_$RIGHTS_CHECK (0x00E46AEC).
 * Both push exactly nine arguments; the callee frame is
 *
 *   A6+0x08  sids           (long)  -> the caller's SID block
 *   A6+0x0C  proj_uids      (long)  -> the caller's project-UID list
 *   A6+0x10  uid            (long)  -> the object UID
 *   A6+0x14  ignore_super   (byte)  0x00E464C4 `move.b (0x14,A6),D5b`
 *   A6+0x16  required_mask  (long)  0x00E464C8 `move.l (0x16,A6),D3`
 *   A6+0x1A  option_flags   (word)  0x00E464CC `move.w (0x1a,A6),D2w`
 *   A6+0x1C  in_super       (byte)  0x00E464D0 `move.b (0x1c,A6),D4b`
 *   A6+0x1E  in_subsys      (byte)  0x00E464D4 `move.b (0x1e,A6),D6b`
 *   A6+0x20  status_ret     (long)
 *
 * 0x00E464DC-0x00E464E2: `tst.b D4b / bpl` then `tst.b D5b / bpl` - when the
 * process is in super mode AND ignore_super is FALSE the evaluator short
 * circuits to "all rights" (0xF) at 0x00E4668A.
 *
 * Returns the granted rights in D0 (a full longword).
 *
 * Emitted in acl/eval_rights.c (source-0qxe).
 */
uint32_t acl_$eval_rights(acl_sid_block_t *sids, uid_t *proj_uids, uid_t *uid,
                          boolean ignore_super, uint32_t required_mask,
                          int16_t option_flags, boolean in_super,
                          boolean in_subsys, status_$t *status_ret);

/*
 * acl_$get_obj_acl_attrs (0x00E45F78, was FUN_00e45f78)
 *
 * Fills a 0x20-byte object-location record and the 0x38-byte ACL attribute
 * record for `uid`.  Argument order is fixed by acl_$eval_rights'
 * 0x00E465DE-0x00E465EA pushes (right to left: status, attrs, loc, uid) and
 * by ACL_$SET_ACL_CHECK's identical sequence at 0x00E470EA-0x00E470FA.
 *
 * It seeds loc->uid from the caller's UID (0x00E45F90), clears bits 6 and 7 of
 * loc->flags (0x00E45FEA, 0x00E4607E), sets attrs->obj_flags[ACL_ATTR_OBJ_TYPE]
 * = 1 and obj_flags[ACL_ATTR_SUB_TYPE] = 3 for the well-known volume UIDs
 * (0x00E46074-0x00E4607A) and
 * otherwise calls AST_$GET_ACL_ATTRIBUTES (0x00E460BC).
 *
 * Emitted in acl/get_obj_acl_attrs.c.
 */
void acl_$get_obj_acl_attrs(uid_t *uid, file_$obj_loc_t *loc,
                            ast_$acl_attr_t *attrs, status_$t *status_ret);

/*
 * acl_$find_acl_slot (0x00E45E8E, was FUN_00e45e8e)
 *
 * Hashes `acl_uid` with UID_$HASH (0x00E45EB0) and walks the module's chained
 * ACL-image cache directory, returning the slot index into ACL_$ACL_CACHE or
 * ACL_CACHE_NO_SLOT.  `cached_flag_ret` receives one byte (0x00E45F10); when
 * that byte is negative the routine also refreshes prot->world_rights and
 * prot->subsys_rights from the cached default ACL data (0x00E45F24).
 *
 * Argument order from acl_$eval_rights' 0x00E467D2-0x00E467E2 pushes.
 *
 * Emitted in acl/find_acl_slot.c.
 */
int16_t acl_$find_acl_slot(uid_t *acl_uid, int8_t *cached_flag_ret,
                           acl_$prot_data_t *prot, status_$t *status_ret);

/*
 * acl_$eval_acl_entries (0x00E46172, was FUN_00e46172)
 *
 * Walks the 0x20-byte entries of one cached ACL image and returns the rights
 * word for the caller's SIDs.  Argument order from acl_$eval_rights'
 * 0x00E4685A-0x00E46874 pushes (right to left: prot, proj_uids, sids, slot).
 * The result is only 16 bits wide - acl_$eval_rights zero-extends it with
 * `clr.l D1 / move.w D0w,D1w` at 0x00E46880.
 *
 * Emitted in acl/eval_acl_entries.c.
 */
uint16_t acl_$eval_acl_entries(acl_$cache_slot_t *slot, uid_t *sids,
                               uid_t *proj_uids, acl_$prot_data_t *prot);

/*
 * acl_$check_suser_pid - Check if a process has superuser privileges
 *
 * Checks if the specified process is:
 *   - PID 1 (always superuser)
 *   - In super mode (SUPER_COUNT > 0)
 *   - Has login UID matching RGYC_$G_LOGIN_UID
 *   - Has user/group/login UID matching RGYC_$G_LOCKSMITH_UID
 *
 * If superuser, sets the corresponding bit in ASID_SUSER_BITMAP.
 *
 * Parameters:
 *   pid - Process ID to check
 *
 * Returns:
 *   Non-zero (0xFF) if superuser, 0 otherwise
 *
 * Original address: 0x00E463E4
 */
int8_t acl_$check_suser_pid(int16_t pid);

/*
 * ACL_$FREE_ASID - Free/reset ACL state for an ASID
 *
 * Resets all SID state for a process to system defaults:
 *   - User SID = RGYC_$P_SYS_USER_UID
 *   - Group SID = RGYC_$G_SYS_PROJ_UID
 *   - Org SID = RGYC_$O_SYS_ORG_UID
 *   - Login SID = UID_$NIL
 *
 * Marks ASID as free in bitmap and clears suser flag.
 *
 * Parameters:
 *   asid - Address space ID to free
 *   status_ret - Output status code
 *
 * Original address: 0x00E74C6A
 */
void ACL_$FREE_ASID(int16_t asid, status_$t *status_ret);

/*
 * ACL_$GET_SID - Get SID for an ASID
 *
 * Returns the current user SID for the specified ASID.
 *
 * Parameters:
 *   asid - Address space ID
 *   sid_ret - Output SID buffer
 *
 * Original address: 0x00E74C24
 */
void ACL_$GET_SID(int16_t asid, uid_t *sid_ret);

/*
 * acl_$is_process_type_2 - Check if process is type 2 (user process)
 *
 * Returns non-zero if the specified process is not type 2.
 * Type 2 appears to be a regular user process.
 *
 * Parameters:
 *   pid - Process ID to check
 *
 * Returns:
 *   Non-zero (-1) if process type != 2, 0 if type == 2
 *
 * Original address: 0x00E46498
 */
int8_t acl_$is_process_type_2(int16_t pid);

/*
 * acl_$image_internal - Internal image helper function
 *
 * Creates an internal image/representation of ACL data.
 *
 * Parameters:
 *   source_uid - Source UID
 *   buffer_len - Length of output buffer
 *   flag       - Operation flag
 *   output_buf - Output buffer
 *   len_out    - Output: actual length
 *   data_out   - Output: data buffer
 *   flag_out   - Output: flag byte
 *   status     - Output status code
 *
 * Original address: 0x00E47B78
 */
void acl_$image_internal(void *source_uid, int16_t buffer_len, int8_t flag,
                         void *output_buf, void *len_out, void *data_out,
                         void *flag_out, status_$t *status);

#endif /* ACL_INTERNAL_H */
