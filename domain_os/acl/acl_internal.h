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

/* The ACL_$DATA segment (0xE88834, 0xAD98 bytes) is the ACL_$DATA block below. */

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

/* The ACL image cache records (acl_$cache_slot_t, acl_$cache_dir_t,
 * acl_$cache_link_t) and ACL_$UNWIRED_DATA are in acl/acl.h. */

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
 * ACL_$UNWIRED_DATA.image_buf (A5+0x400 = 0xE7D354) is the 0x400-byte
 * scratch image acl_$load_acl_image hands to acl_$convert_image (`pea
 * (0x400,A5)`, 0x00E45D64) and then copies into ACL_$DATA.acl_cache[slot]
 * (0x00E45DD0).
 */

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
 * reads the ACL object into ACL_$DATA.acl_cache and returns the slot index, or
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
                               void *subsys_uid, int8_t flag, void *image,
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
 * ACL_$DATA - the per-process tables and the image cache (map "D69 E88834
 * ACL_$DATA loaded at 196B4E, size = AD98")
 * ============================================================================
 *
 * Module data block ACL_$DATA: Claude Opus 5.5 (source-l2yd).
 *
 * ACL_$INIT zeroes it whole with OS_$DATA_ZERO (0x00E310AA-0x00E310C0:
 * 0xE935CC - 0xE88834).  The image cache is reached from its literal base
 * 0xE88834 (acl_$eval_rights 0x00E4680C); the per-process tables are
 * reached as negative displacements from the literal 0xE97294 (a base
 * inside FILE_$LOT_DATA that FILE shares, file/file_internal.h), indexed by
 * the pid (or, in ACL_$ALLOC_ASID / ACL_$FREE_ASID, by the ASID - the same
 * number for every table here, bead source-x5dd).  A MODULE_DATA block
 * linked in the SAU2 map's order after the COLOR7 cells and before
 * FILE_$LOT_DATA; the address is the ordering key, not the link address.
 *
 *   off     image      field                  from 0xE97294   stride
 *   0x0000  0xE88834   acl_cache[0..30]                        0x400
 *   0x7BDC  0xE90410   original_sids[0..64]   -0x6E84          0x24
 *   0x84DC  0xE90D10   current_sids[0..64]    -0x6584          0x24
 *   0x8DDC  0xE91610   saved_sids[0..64]      -0x5C84          0x24
 *   0x96F4  0xE91F28   saved_proj[0..64]      -0x536C          0x0C
 *   0x99F4  0xE92228   proj_lists[0..64]      -0x506C          0x0C
 *   0x9CC8  0xE924FC   proj_uids[0..64][8]    -0x4DA0 + 8      0x40
 *   0x9D00  0xE92534   asid_free_bitmap[8]    -0x4D60          bits
 *   0xAD06  0xE9353A   subsys_level[0..64]    -0x3D5A          2
 *   0xAD88  0xE935BC   locksmith_override_bitmap[8]  -0x3CD8   bits
 *   0xAD90  0xE935C4   asid_suser_bitmap[8]   -0x3CD0          bits
 *
 * Each per-process table is Pascal [1..64] and is declared from its bias
 * slot, [0..64], so every user indexes with the pid the assembly scales:
 * ACL_$FREE_ASID (0x00E74C78-0x00E74D5A) builds asid*0x24, asid*0xC,
 * asid*0x40 and asid*2 against 0xE97294 and stores through (-0x6584,A0),
 * (-0x506c,A4), (-0x4da0,A3)+8.. and (-0x3d5a,A0); ACL_$INIT writes pid 1's
 * login SIDs at (-0x6e48,A2) / (-0x6548,A2) = original_sids[1] /
 * current_sids[1] + 0x18 (0x00E3110C, 0x00E3111A) and pid 1's project row
 * from 0xE9253C = proj_uids[1][0] (0x00E31132).  Every bias slot overlays
 * the tail of the object before it (original_sids[0] the end of
 * acl_cache[30], current_sids[0] original_sids[64], ..., proj_uids[0]
 * proj_lists[60..64] and asid_free_bitmap, subsys_level[0] the last word of
 * proj_uids[64]), a chain, so the block is one union with an arm per object
 * each placed from the block start by its own pad.
 *
 * The three bitmaps are 64 bits for pids 1..64, byte (pid-1)>>3, mask
 * 0x80 >> ((pid-1)&7): the -1 is part of the assembly's bit-number
 * arithmetic (`subq.w #0x1,D1w' 0x00E74D62), not a table bias, and stays in
 * ACL_PID_BITMAP_BYTE / ACL_PID_BITMAP_MASK.
 *
 * Pointer-free and alignment-independent (every pad is a multiple of its
 * arm's alignment), so every assert is unconditional.  Image contents: none -
 * the segment is loaded at file offset 0x196B4E, the end of the SR10.2 SAU2
 * file, so it starts zero-filled (the bytes Ghidra shows at 0xE88834 belong
 * to the zero-length RELOC segment that shares the address).
 */
#define ACL_$DATA_SIZE      0xAD98U         /* map: ACL_$DATA size = AD98 */

/* ACL_$DATA's per-process tables: [0..64], Pascal [1..64] plus bias slot */
#define ACL_PID_TABLE_SIZE  PROC1_MAX_PROCESSES

typedef struct acl_$data_t {
    union {
        acl_$cache_slot_t acl_cache[ACL_CACHE_SLOTS];   /* +0x0000 */
        struct {
            uint8_t         _original_sids_bias[0x7BDC];
            acl_sid_block_t original_sids[ACL_PID_TABLE_SIZE];  /* +0x7BDC */
        };
        struct {
            uint8_t         _current_sids_bias[0x84DC];
            acl_sid_block_t current_sids[ACL_PID_TABLE_SIZE];   /* +0x84DC */
        };
        struct {
            uint8_t         _saved_sids_bias[0x8DDC];
            acl_sid_block_t saved_sids[ACL_PID_TABLE_SIZE];     /* +0x8DDC */
        };
        struct {
            uint8_t         _saved_proj_bias[0x96F4];
            acl_proj_list_t saved_proj[ACL_PID_TABLE_SIZE];     /* +0x96F4 */
        };
        struct {
            uint8_t         _proj_lists_bias[0x99F4];
            acl_proj_list_t proj_lists[ACL_PID_TABLE_SIZE];     /* +0x99F4 */
        };
        struct {
            uint8_t         _proj_uids_bias[0x9CC8];
            uid_t           proj_uids[ACL_PID_TABLE_SIZE][ACL_MAX_PROJECTS]; /* +0x9CC8 */
        };
        struct {
            uint8_t         _asid_free_bitmap_at[0x9D00];
            uint8_t         asid_free_bitmap[8];                /* +0x9D00 1 = free */
        };
        struct {
            uint8_t         _subsys_level_bias[0xAD06];
            int16_t         subsys_level[ACL_PID_TABLE_SIZE];   /* +0xAD06 */
            uint8_t         locksmith_override_bitmap[8];       /* +0xAD88 */
            uint8_t         asid_suser_bitmap[8];               /* +0xAD90 1 = used suser */
        };
    };
} acl_$data_t;

#define ACL_DATA_OFF_(f) __builtin_offsetof(acl_$data_t, f)
_Static_assert(ACL_DATA_OFF_(acl_cache) == 0x0000, "acl_cache at 0xE88834");
_Static_assert(ACL_DATA_OFF_(acl_cache[1]) == 0x0400, "acl_cache stride 0x400");
_Static_assert(ACL_DATA_OFF_(acl_cache[ACL_CACHE_SLOTS]) == 0x7C00, "acl_cache[30] ends at original_sids[1]");
_Static_assert(ACL_DATA_OFF_(original_sids) == 0x7BDC, "original_sids[0] (-0x6e84,0xE97294)");
_Static_assert(ACL_DATA_OFF_(original_sids[1]) == 0x7C00, "original_sids stride 0x24");
_Static_assert(ACL_DATA_OFF_(current_sids) == 0x84DC, "current_sids[0] (-0x6584,0xE97294)");
_Static_assert(ACL_DATA_OFF_(current_sids[0]) + 0x24 == ACL_DATA_OFF_(original_sids[ACL_PID_TABLE_SIZE]),
               "current_sids[1] follows original_sids[64]");
_Static_assert(ACL_DATA_OFF_(current_sids[1]) - ACL_DATA_OFF_(current_sids) == 0x24, "current_sids stride 0x24");
_Static_assert(ACL_DATA_OFF_(saved_sids) == 0x8DDC, "saved_sids[0] (-0x5c84,0xE97294)");
_Static_assert(ACL_DATA_OFF_(saved_sids[1]) == ACL_DATA_OFF_(current_sids[ACL_PID_TABLE_SIZE]),
               "saved_sids[1] follows current_sids[64]");
_Static_assert(ACL_DATA_OFF_(saved_proj) == 0x96F4, "saved_proj[0] (-0x536c,0xE97294)");
_Static_assert(ACL_DATA_OFF_(saved_proj[1]) == ACL_DATA_OFF_(saved_sids[ACL_PID_TABLE_SIZE]),
               "saved_proj[1] follows saved_sids[64]; stride 0xC");
_Static_assert(ACL_DATA_OFF_(proj_lists) == 0x99F4, "proj_lists[0] (-0x506c,0xE97294)");
_Static_assert(ACL_DATA_OFF_(proj_lists[1]) == ACL_DATA_OFF_(saved_proj[ACL_PID_TABLE_SIZE]),
               "proj_lists[1] follows saved_proj[64]; stride 0xC");
_Static_assert(ACL_DATA_OFF_(asid_free_bitmap) == ACL_DATA_OFF_(proj_lists[ACL_PID_TABLE_SIZE]),
               "asid_free_bitmap (-0x4d60,0xE97294) follows proj_lists[64]");
_Static_assert(ACL_DATA_OFF_(proj_uids) == 0x9CC8, "proj_uids[0] (-0x4da0+8,0xE97294)");
_Static_assert(ACL_DATA_OFF_(proj_uids[1]) == 0x9D08, "proj_uids[1][0] = 0xE9253C; row stride 0x40");
_Static_assert(ACL_DATA_OFF_(proj_uids[1][1]) - ACL_DATA_OFF_(proj_uids[1]) == 8, "proj_uids column stride 8");
_Static_assert(ACL_DATA_OFF_(subsys_level) == 0xAD06, "subsys_level[0] (-0x3d5a,0xE97294)");
_Static_assert(ACL_DATA_OFF_(subsys_level[1]) == ACL_DATA_OFF_(proj_uids[ACL_PID_TABLE_SIZE]),
               "subsys_level[1] follows proj_uids[64]; stride 2");
_Static_assert(ACL_DATA_OFF_(locksmith_override_bitmap) == 0xAD88, "locksmith_override_bitmap (-0x3cd8,0xE97294)");
_Static_assert(ACL_DATA_OFF_(asid_suser_bitmap) == 0xAD90, "asid_suser_bitmap (-0x3cd0,0xE97294)");
_Static_assert(sizeof(acl_$data_t) == ACL_$DATA_SIZE, "ACL_$DATA: map size 0xAD98");
#undef ACL_DATA_OFF_

MODULE_DATA_DECLARE(acl_$data_t, ACL_$DATA, 0x00E88834);

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
 * ============================================================================
 * ACL_$WIRED_DATA - the ACL_WIRED segment (map "D E2C014 ACL_WIRED size = 14")
 * ============================================================================
 *
 * Module data block ACL_$WIRED_DATA: Claude Opus 5.5 (source-l2yd).  Its one
 * object is the map's ACL_$EXCLUSION_LOCK (ACL_$INIT
 * ML_$EXCLUSION_INIT(&lock) 0x00E3117E; ACL_$CONVERT_TO_9ACL /
 * _FROM_9ACL / ACL_$CLEAR_SUPER start and stop it).  A MODULE_DATA block
 * linked in the SAU2 map's order after UID_ and before FILE_WIRED.  The lock
 * is 0x12 bytes on the target; the segment's last word is never addressed.
 * ml_$exclusion_t holds pointers, so the asserts are target only.  Image
 * contents (`gsk read 0xE2C014 20'): zero.
 */
#define ACL_$WIRED_DATA_SIZE 0x14           /* map: ACL_WIRED size = 14 */

typedef struct acl_$wired_data_t {
    ml_$exclusion_t exclusion_lock;         /* +0x00 map ACL_$EXCLUSION_LOCK */
    uint16_t        _0012;                  /* +0x12 never addressed */
} acl_$wired_data_t;

#if defined(ARCH_M68K)
/* Target only: ml_$exclusion_t holds pointers. */
_Static_assert(__builtin_offsetof(acl_$wired_data_t, _0012) == 0x12, "exclusion lock is 0x12 bytes");
_Static_assert(sizeof(acl_$wired_data_t) == ACL_$WIRED_DATA_SIZE, "ACL_WIRED: map size 0x14");
#endif

MODULE_DATA_DECLARE(acl_$wired_data_t, ACL_$WIRED_DATA, 0x00E2C014);

/*
 * The ACL_ A5 block's cells (workspace, image buffer, cache directory,
 * locksmith state, super-user counts) are fields of ACL_$UNWIRED_DATA in
 * acl/acl.h.
 */

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
 * ACL_$SERVER request views (0x00E49594).  REM_FILE_$SERVER hands over its
 * 0x298-byte request (rem_file_server_req_t: version, opcode at +3, object
 * UID at +4); each opcode reads its own fields past the common header:
 */
typedef struct acl_$srv_hdr_t {
    uint16_t version;           /* 0x00 */
    uint8_t  _02;               /* 0x02 */
    uint8_t  opcode;            /* 0x03: 0x64..0x6C, `move.b (0x3,A2)` */
    uid_t    uid;               /* 0x04: the object */
} acl_$srv_hdr_t;

/* 0x64 ACL_IMAGE (0x00E495F6-0x00E49736) */
typedef struct acl_$srv_image_req_t {
    acl_$srv_hdr_t hdr;
    uint16_t mode;              /* 0x0C: 4 = image into ACL_$UNWIRED_DATA.image_buf
                                 *       and rebuild it into `buf` */
    int8_t   flag;              /* 0x0E: acl_$image_internal's flag; forced TRUE
                                 *       for mode 4 (`st (0xe,A2)` 0x00E4966C) */
    uint8_t  _0f;
    uint32_t buf;               /* 0x10: VA of the bulk page */
} acl_$srv_image_req_t;

/* 0x68 ACL_CREATE (0x00E4973A-0x00E49784) */
typedef struct acl_$srv_create_req_t {
    acl_$srv_hdr_t hdr;
    uint8_t  _0c[4];
    uid_t    type;              /* 0x10: ACL_$PRIM_CREATE's 4th argument */
    uint8_t  _18[0x24];
    uint32_t acl_data;          /* 0x3C: VA of the ACL data (a word at +0xE
                                 *       counts its 0x20-byte entries) */
    uid_t    acl_uid;           /* 0x40 */
} acl_$srv_create_req_t;

/* 0x6A ACL_SETIDS (0x00E497BE-0x00E497F4) */
typedef struct acl_$srv_setids_req_t {
    acl_$srv_hdr_t hdr;
    uint8_t  _0c[4];
    uid_t    sids[4];           /* 0x10 */
    uint8_t  _30[4];
    uint32_t owner_ext[3];      /* 0x34 */
} acl_$srv_setids_req_t;

/* 0x6C ACL_CHECK_RIGHTS (0x00E49788-0x00E497BA) */
typedef struct acl_$srv_rights_req_t {
    acl_$srv_hdr_t hdr;
    uint8_t  _0c[2];
    int16_t  option_flags;      /* 0x0E */
    acl_sid_block_t sids;       /* 0x10 */
    uid_t    proj[ACL_MAX_PROJECTS];  /* 0x34 */
    uint32_t required_mask;     /* 0x74 */
    int8_t   ignore_super;      /* 0x78 */
    int8_t   in_super;          /* 0x79 */
    int8_t   in_subsys;         /* 0x7A */
} acl_$srv_rights_req_t;

/* 0x66 SET_ACL (0x00E497F8-0x00E498EC) */
typedef struct acl_$srv_set_acl_req_t {
    acl_$srv_hdr_t hdr;
    uint8_t  _0c[2];
    int16_t  proj_count;        /* 0x0E */
    acl_sid_block_t sids;       /* 0x10: new current SIDs */
    uid_t    proj[ACL_MAX_PROJECTS];  /* 0x34 */
    uid_t    acl_uid;           /* 0x74 */
    acl_$prot_data_t prot;      /* 0x7C */
    int16_t  op_type;           /* 0xA8 */
} acl_$srv_set_acl_req_t;

/* The reply ACL_$SERVER builds (0x00E495AE-0x00E495C4) */
typedef struct acl_$srv_resp_t {
    uint16_t one;               /* 0x00: always 1 */
    uint8_t  flags;             /* 0x02: 0x80 */
    uint8_t  opcode;            /* 0x03: request opcode + 1, or 3 */
    status_$t status;           /* 0x04 */
    uint8_t  _08[2];
    union {
        int16_t image_len;      /* 0x0A: ACL_IMAGE */
        int8_t  changed;        /* 0x0A: ACL_SETIDS, a Domain boolean */
    } a;
    uint8_t  data[0x30];        /* 0x0C */
} acl_$srv_resp_t;

_Static_assert(offsetof(acl_$srv_image_req_t, mode) == 0x0C, "image.mode");
_Static_assert(offsetof(acl_$srv_image_req_t, flag) == 0x0E, "image.flag");
_Static_assert(offsetof(acl_$srv_image_req_t, buf) == 0x10, "image.buf");
_Static_assert(offsetof(acl_$srv_create_req_t, type) == 0x10, "create.type");
_Static_assert(offsetof(acl_$srv_create_req_t, acl_data) == 0x3C, "create.acl_data");
_Static_assert(offsetof(acl_$srv_create_req_t, acl_uid) == 0x40, "create.acl_uid");
_Static_assert(offsetof(acl_$srv_setids_req_t, sids) == 0x10, "setids.sids");
_Static_assert(offsetof(acl_$srv_setids_req_t, owner_ext) == 0x34, "setids.owner_ext");
_Static_assert(offsetof(acl_$srv_rights_req_t, option_flags) == 0x0E, "rights.option_flags");
_Static_assert(offsetof(acl_$srv_rights_req_t, sids) == 0x10, "rights.sids");
_Static_assert(offsetof(acl_$srv_rights_req_t, proj) == 0x34, "rights.proj");
_Static_assert(offsetof(acl_$srv_rights_req_t, required_mask) == 0x74, "rights.required_mask");
_Static_assert(offsetof(acl_$srv_rights_req_t, in_subsys) == 0x7A, "rights.in_subsys");
_Static_assert(offsetof(acl_$srv_set_acl_req_t, proj_count) == 0x0E, "set_acl.proj_count");
_Static_assert(offsetof(acl_$srv_set_acl_req_t, acl_uid) == 0x74, "set_acl.acl_uid");
_Static_assert(offsetof(acl_$srv_set_acl_req_t, prot) == 0x7C, "set_acl.prot");
_Static_assert(offsetof(acl_$srv_set_acl_req_t, op_type) == 0xA8, "set_acl.op_type");
_Static_assert(offsetof(acl_$srv_resp_t, status) == 0x04, "resp.status");
_Static_assert(offsetof(acl_$srv_resp_t, a) == 0x0A, "resp.a");
_Static_assert(offsetof(acl_$srv_resp_t, data) == 0x0C, "resp.data");
_Static_assert(sizeof(acl_$srv_resp_t) == 0x3C, "resp: reply_len 0x3C");

/*
 * acl_$setids (0x00E46B4E, was FUN_00e46b4e)
 *
 * Applies (set < 0) or checks the set-ID SIDs of `uid`'s protection and the
 * required-subsystem UID of its ACL image against sids[0..3] /
 * owner_ext[0..2]; forwards remote objects to REM_FILE_$ACL_SETIDS.
 * Relies on the caller's A5 (ACL_$UNWIRED_DATA).  Emitted in acl/setids.c.
 */
void acl_$setids(uid_t *uid, int8_t set, uid_t *sids, uint32_t *owner_ext,
                 int8_t *changed, status_$t *status_ret);

/*
 * acl_$sids_allowed (0x00E44CE8, 214 bytes, was FUN_00e44ce8; no map symbol)
 *
 * TRUE (0xFF) when every SID of `sids` is one process `pid` may assume: the
 * user, group and org SIDs each equal the process's current or saved one,
 * and the login SID is UID_$NIL or the saved or original login SID.  Called
 * by ACL_$ENTER_SUBS (0x00E46EB0) and ACL_$CHECK_DEBUG_RIGHTS (0x00E48B22,
 * 0x00E48B36).  Emitted in acl/sids_allowed.c.
 */
int8_t acl_$sids_allowed(acl_sid_block_t *sids, int16_t pid);

/*
 * acl_$find_acl_slot (0x00E45E8E, was FUN_00e45e8e)
 *
 * Hashes `acl_uid` with UID_$HASH (0x00E45EB0) and walks the module's chained
 * ACL-image cache directory, returning the slot index into ACL_$DATA.acl_cache or
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
