/*
 * ast_$set_attribute_internal - Internal attribute setting function
 *
 * Sets attributes on an object, handling both local and remote objects.
 * For remote objects, calls the remote file service.  For local objects,
 * performs the operation directly if allowed.
 *
 * Original address: 0x00E05214 (364 bytes)
 *
 * This file also carries the Pascal procedure nested inside it,
 * AST_$SET_ATTR_DISPATCH (0x00E04B00, 1756 bytes).  That procedure declares
 * no parameters of its own: it loads the static link with
 *
 *     00e04b00  link.w A6,-0x6c
 *     00e04b08  move.l (A6),D4          ; D4 = this frame's parent frame
 *
 * and then reads the parent's arguments and locals directly through D4:
 *
 *     (0x0C,parent)  attr_type   (word)
 *     (0x0E,parent)  value       (pointer)
 *     (0x12,parent)  wait_flag   (byte, high half of the word slot)
 *     (0x14,parent)  subject     (pointer, the caller's ast_$subject_t)
 *     (0x1C,parent)  status      (pointer)
 *     (-0x4C,parent) aote        (local)
 *     (-0x54,parent) clock       (local, 6 bytes: long + word)
 *
 * It is emitted here as a static procedure taking those uplevel references as
 * explicit parameters, so no invented global ABI is needed.
 */

#include "ast/ast_internal.h"

/* PROC1_$TYPE and PROC1_$CURRENT from proc1.h via ast_internal.h */
/* UID_$NIL is defined in base/base.h */

/* Status codes */
#define file_$object_is_remote                 0x000F0002

/*
 * Attribute type constants (the 28-entry jump table lives at 0x00E04BA2).
 */
#define ATTR_TYPE_READONLY       0x00  /* set bit 4 of aote_t.attr_flags_hi */
#define ATTR_TYPE_COPY_ON_WRITE  0x01  /* set bit 3 of aote_t.attr_flags_hi */
#define ATTR_TYPE_DIRTY          0x02  /* set bit 2 of aote_t.attr_flags_hi */
#define ATTR_TYPE_ACL_UID        0x03
#define ATTR_TYPE_CREATION_TIME  0x04
#define ATTR_TYPE_MOD_TIME       0x05
#define ATTR_TYPE_ADD_REFCOUNT   0x06
#define ATTR_TYPE_SUB_REFCOUNT   0x07
#define ATTR_TYPE_SET_REFCOUNT   0x08
#define ATTR_TYPE_DTM            0x09
#define ATTR_TYPE_DTU            0x0A
#define ATTR_TYPE_BLOCKS         0x0B
#define ATTR_TYPE_OS_ONLY        0x0C  /* bit 7 of aote_t.access_flags */
#define ATTR_TYPE_ACCESS_MODE    0x0D  /* bits 5 and 4 of aote_t.access_flags */
#define ATTR_TYPE_UID_84         0x0E
#define ATTR_TYPE_UID_8C         0x0F
#define ATTR_TYPE_OWNER1         0x10
#define ATTR_TYPE_OWNER2         0x11
#define ATTR_TYPE_OWNER3         0x12
#define ATTR_TYPE_SET_ACL_IMAGE  0x13
#define ATTR_TYPE_MERGE_ACL      0x14
#define ATTR_TYPE_SET_RIGHTS     0x15
#define ATTR_TYPE_LINKCOUNT      0x16
#define ATTR_TYPE_DTM_ROUNDED    0x17
#define ATTR_TYPE_DTU_ROUNDED    0x18
#define ATTR_TYPE_SPECIAL_FLAG   0x19  /* bit 2 of aote_t.attr_flags_lo */
#define ATTR_TYPE_DTM_FROM_CLOCK 0x1A
#define ATTR_TYPE_DTU_FROM_CLOCK 0x1B
#define ATTR_TYPE_COUNT          0x1C  /* 0xE04B90: cmpi.w #0x1c / bcc invalid */

/*
 * The attribute types legal on an object whose obj_type is zero.
 * 0xE04B3A: move.l #0x3fff,D3 / btst.l D1,D3.
 */
#define ATTR_SET_BASIC 0x00003FFFu

/*
 * aote_t.attr_flags_lo bit 1 marks a "special" object: only MOD_TIME and
 * BLOCKS may be set on it (0xE04B46: btst.b #0x1,(0xf,A2)).
 */
#define AOTE_ATTR_SPECIAL 0x02

/*
 * The attribute types local to this node that may not be forwarded to a
 * remote file server (0xE052D4: move.l #0x8c0,D0 / btst.l D1,D0):
 * ADD_REFCOUNT, SUB_REFCOUNT and BLOCKS.
 */
#define ATTR_SET_LOCAL_ONLY 0x000008C0u

/* AST_$ATTR_TIMESTAMP_MASK - attribute timestamp mask at A5+0x48C
 * (declared in ast/ast_internal.h) */

/*
 * Frame slot at parent A6-0x40, six bytes.  Cases 0x17/0x18 use it as the
 * (long, word) DTM/DTU pair; the recursive ADD_REFCOUNT call at 0xE05196
 * writes only a word 1 into its first two bytes, which case 6 never reads.
 */
typedef union attr_tmp_t {
    struct {
        int32_t high;
        int16_t low;
    } pair;
    int16_t word;  /* the word at A6-0x40 written by 0xE05196 */
} attr_tmp_t;

/*
 * Copy the 44-byte ACL image from an ast_$attr_rec_t onto aote_t 0x54..0x7F.
 * 0xE04F12-0xE04F20: eleven longword moves from the record to aote+0x54.
 */
static void copy_acl_image(aote_t *aote, const ast_$attr_rec_t *rec)
{
    aote->owner1 = rec->owner1;
    aote->owner2 = rec->owner2;
    aote->owner3 = rec->owner3;
    aote->rights1 = rec->rights1;
    aote->rights2 = rec->rights2;
    aote->rights3 = rec->rights3;
    aote->rights4 = rec->rights4;
    aote->rights5 = rec->rights5;
    aote->access_flags = rec->access_flags;
    aote->unknown_72 = rec->unknown_1e;
    aote->owner1_ext = rec->owner1_ext;
    aote->owner2_ext = rec->owner2_ext;
    aote->owner3_ext = rec->owner3_ext;
}

/* uid == UID_$NIL, as the paired cmpm.l tests against 0xE1737C do. */
static boolean uid_is_nil(const uid_t *uid)
{
    return (uid->high == UID_$NIL.high && uid->low == UID_$NIL.low) ? true : false;
}

static boolean uid_eq(const uid_t *a, const uid_t *b)
{
    return (a->high == b->high && a->low == b->low) ? true : false;
}

/*
 * AST_$SET_ATTR_DISPATCH (0x00E04B00) - nested procedure of
 * ast_$set_attribute_internal.  Every parameter below is an uplevel reference
 * that the original reads through the static link in D4.
 *
 *   attr_type / value / wait_flag / subject / status  are the parent's
 *   arguments at (0x0C,A6), (0x0E,A6), (0x12,A6), (0x14,A6) and (0x1C,A6);
 *   aote_slot and clock are the parent's locals at (-0x4C,A6) and (-0x54,A6).
 *
 * aote_slot is passed as a pointer because the original re-reads (-0x4C,A6)
 * at 0xE04DF8, 0xE05110, 0xE05126, 0xE05154 and 0xE05168 rather than using
 * its cached copy in D5.
 */
static void ast_$set_attr_dispatch(uint16_t attr_type, void *value,
                                   boolean wait_flag,
                                   const ast_$subject_t *subject,
                                   aote_t **aote_slot, clock_t *clock,
                                   status_$t *status)
{
    aote_t *aote;            /* D5/A2 - cached copy of *aote_slot */
    boolean needs_purify;    /* D2b - cleared at 0xE04B14, set by cases 3/0x13/0x14 */
    uint16_t obj_type;       /* D0w - byte at aote_t.obj_type, zero extended */
    uid_t old_acl_uid;       /* A6-0x50 */
    uid_t new_acl_uid;       /* A6-0x48 */
    attr_tmp_t tmp;          /* A6-0x40 */
    boolean truncate_out;    /* A6-0x56, the result byte AST_$TRUNCATE writes
                              * (a bit field: see file/priv_unlock.c) */

    *status = status_$ok;                                   /* 0xE04B10 */
    needs_purify = false;                                   /* 0xE04B14 */
    ML_$LOCK(PMAP_LOCK_ID);                                 /* 0xE04B16: 0x14 */

    aote = *aote_slot;                                      /* 0xE04B24 */
    obj_type = aote->obj_type;                              /* 0xE04B2C, zero extended */

    /* 0xE04B30: only obj_type 0 is restricted to the basic attribute set. */
    if (obj_type == 0 &&
        (((uint32_t)1 << attr_type) & ATTR_SET_BASIC) == 0) {
        goto invalid_attr;                                  /* 0xE04B42 -> 0xE050F4 */
    }

    /* 0xE04B46: special objects accept only MOD_TIME and BLOCKS. */
    if ((aote->attr_flags_lo & AOTE_ATTR_SPECIAL) != 0) {
        if (attr_type == ATTR_TYPE_MOD_TIME) {              /* 0xE04B50 */
            aote->mod_time = *(uid_t *)value;               /* 0xE04B5C */
        } else if (attr_type == ATTR_TYPE_BLOCKS) {         /* 0xE04B68 */
            aote->blocks = *(uint32_t *)value;              /* 0xE04B74 */
        } else {
            *status = status_$file_volume_has_been_mounted_read_only; /* 0xE04B80 */
        }
        goto unlock_and_return;                             /* 0xE0513E */
    }

    /* 0xE04B90: unsigned compare, so anything >= 0x1C is invalid. */
    if (attr_type >= ATTR_TYPE_COUNT) {
        goto invalid_attr;
    }

    switch (attr_type) {

    case ATTR_TYPE_READONLY:                                /* 0xE04BDA */
        /* 0xE04D0C: bit 4 of attr_flags_hi := bit 7 of the value byte. */
        aote->attr_flags_hi =
            (uint8_t)((aote->attr_flags_hi & 0xEF) |
                      (uint8_t)(((*(uint8_t *)value) >> 7) << 4));
        break;

    case ATTR_TYPE_COPY_ON_WRITE:                           /* 0xE04BE4 */
        aote->attr_flags_hi =
            (uint8_t)((aote->attr_flags_hi & 0xF7) |
                      (uint8_t)(((*(uint8_t *)value) >> 7) << 3));
        break;

    case ATTR_TYPE_DIRTY:                                   /* 0xE04BF8 */
        aote->attr_flags_hi =
            (uint8_t)((aote->attr_flags_hi & 0xFB) |
                      (uint8_t)(((*(uint8_t *)value) >> 7) << 2));
        break;

    case ATTR_TYPE_ACL_UID:                                 /* 0xE04C6C */
        /* No change at all: unlock without touching any timestamp. */
        if (uid_eq(&aote->acl_uid, (const uid_t *)value) < 0) {
            goto unlock_and_return;                         /* 0xE04C7C */
        }
        old_acl_uid = aote->acl_uid;                        /* 0xE04C84 */
        new_acl_uid = *(uid_t *)value;                      /* 0xE04C90 */
        needs_purify = true;                                /* 0xE04C98: st D2b */
        aote->acl_uid = *(uid_t *)value;                    /* 0xE04C9E */
        aote->rights1 = 0x10;                               /* 0xE04CA6 */
        aote->rights2 = 0x10;
        aote->rights3 = 0x10;
        aote->rights4 = 0;
        aote->rights5 = 0;
        break;

    case ATTR_TYPE_CREATION_TIME:                           /* 0xE04CC4 */
        aote->dtc = *(uid_t *)value;
        break;

    case ATTR_TYPE_MOD_TIME:                                /* 0xE04CD4 */
        aote->mod_time = *(uid_t *)value;
        break;

    case ATTR_TYPE_ADD_REFCOUNT:                            /* 0xE04CE4 */
        /* cmpi.w #-0xb / bcc: unsigned, so 0xFFF5..0xFFFF is a no-op. */
        if (aote->refcount >= 0xFFF5) {
            goto unlock_and_return;
        }
        aote->refcount = (uint16_t)(aote->refcount + 1);    /* 0xE04CEE */
        aote->attr_flags_hi |= 0x10;                        /* 0xE04CF2: bset.b #4 */
        break;

    case ATTR_TYPE_SUB_REFCOUNT: {                          /* 0xE04D1E */
        uint16_t refcount = aote->refcount;

        if (refcount >= 0xFFF5) {                           /* 0xE04D22 */
            goto unlock_and_return;
        }
        /* 0xE04D2A: zero, or one on a sub_type 1/2 object, is an underflow. */
        if (refcount == 0 ||
            (refcount == 1 && (aote->sub_type == 1 || aote->sub_type == 2))) {
            *status = status_$ast_refcount_says_unused;       /* 0xE04D48 */
            goto unlock_and_return;
        }
        refcount = (uint16_t)(refcount - 1);                /* 0xE04D52 */
        aote->refcount = refcount;
        if (refcount == 0) {
            aote->attr_flags_hi &= (uint8_t)~0x10;          /* 0xE04D5A: bclr.b #4 */
            *status = status_$ast_refcount_says_unused;       /* 0xE04D64 */
            /* 0xE04D6A branches to the common tail, not to the unlock. */
        }
        break;
    }

    case ATTR_TYPE_SET_REFCOUNT: {                          /* 0xE04CFC */
        int16_t new_count = *(int16_t *)value;

        aote->refcount = (uint16_t)new_count;               /* 0xE04D00 */
        /* 0xE04D08: sne then the shared bit-4 sequence at 0xE04D0C. */
        aote->attr_flags_hi =
            (uint8_t)((aote->attr_flags_hi & 0xEF) | (new_count != 0 ? 0x10 : 0));
        break;
    }

    case ATTR_TYPE_DTM:                                     /* 0xE04D88 */
        aote->dtm_high = *(uint32_t *)value;
        aote->dtm_low = 0;
        break;

    case ATTR_TYPE_DTU:                                     /* 0xE04D98 */
        aote->dtu_high = *(uint32_t *)value;
        aote->dtu_low = 0;
        /* 0xE04DF8: bclr.b #4 through the re-read aote pointer. */
        (*aote_slot)->flags &= (uint8_t)~AOTE_FLAG_TOUCHED;
        break;

    case ATTR_TYPE_BLOCKS:                                  /* 0xE04D6E */
        if (aote->blocks == *(uint32_t *)value) {           /* 0xE04D78 */
            goto unlock_and_return;
        }
        aote->blocks = *(uint32_t *)value;
        /*
         * 0xE04D84 jumps to 0xE05110, skipping the attribute-modified clock
         * store that every other case performs.
         */
        goto set_dirty_flag;

    case ATTR_TYPE_OS_ONLY:                                 /* 0xE04C0C */
        aote->access_flags =
            (int8_t)((aote->access_flags & 0x7F) | (*(uint8_t *)value & 0x80));
        break;

    case ATTR_TYPE_ACCESS_MODE: {                           /* 0xE04C1E */
        uint16_t mode;

        /*
         * 0xE04C24 is "btst.l D2,D1" and D2 is still the zeroed needs_purify
         * flag at this point, so the first test is on bit 0.
         */
        mode = *(uint16_t *)value;
        aote->access_flags =
            (int8_t)((aote->access_flags & 0xDF) | ((mode & 1) != 0 ? 0x20 : 0));
        mode = *(uint16_t *)value;                          /* 0xE04C36: re-read */
        aote->access_flags =
            (int8_t)((aote->access_flags & 0xEF) | ((mode & 2) != 0 ? 0x10 : 0));
        break;
    }

    case ATTR_TYPE_UID_84:                                  /* 0xE04E48 */
        aote->uid_84 = *(uid_t *)value;
        break;

    case ATTR_TYPE_UID_8C:                                  /* 0xE04E58 */
        aote->uid_8c = *(uid_t *)value;
        break;

    case ATTR_TYPE_OWNER1: {                                /* 0xE04E68 */
        const ast_$attr_rec_t *rec = (const ast_$attr_rec_t *)value;

        aote->owner1 = rec->owner1;
        aote->owner1_ext = rec->owner1_ext;                 /* 0xE04E78: (0x20,A1) */
        break;
    }

    case ATTR_TYPE_OWNER2: {                                /* 0xE04E82 */
        const ast_$attr_rec_t *rec = (const ast_$attr_rec_t *)value;

        aote->owner2 = rec->owner2;                         /* 0xE04E86: value+8 */
        aote->owner2_ext = rec->owner2_ext;                 /* 0xE04E94: (0x24,A1) */
        break;
    }

    case ATTR_TYPE_OWNER3: {                                /* 0xE04E9E */
        const ast_$attr_rec_t *rec = (const ast_$attr_rec_t *)value;

        aote->owner3 = rec->owner3;                         /* 0xE04EA2: value+0x10 */
        aote->owner3_ext = rec->owner3_ext;                 /* 0xE04EB2: (0x28,A1) */
        break;
    }

    case ATTR_TYPE_SET_ACL_IMAGE: {                         /* 0xE04EDC */
        const ast_$attr_rec_t *rec = (const ast_$attr_rec_t *)value;
        boolean keep_bit6, keep_bit5, keep_bit4;

        needs_purify = true;                                /* 0xE04EE0 */
        old_acl_uid = aote->acl_uid;                        /* 0xE04EE2 */
        new_acl_uid = rec->acl_uid;                         /* 0xE04EEE: value+0x2C */

        /* 0xE04EFA: the three mode bits are saved across the bulk copy. */
        keep_bit6 = (aote->access_flags & 0x40) != 0 ? true : false;
        keep_bit5 = (aote->access_flags & 0x20) != 0 ? true : false;
        keep_bit4 = (aote->access_flags & 0x10) != 0 ? true : false;

        copy_acl_image(aote, rec);                          /* 0xE04F12 */
        aote->acl_uid = rec->acl_uid;                       /* 0xE04F22 */

        aote->access_flags =
            (int8_t)((aote->access_flags & 0xBF) | (keep_bit6 < 0 ? 0x40 : 0));
        aote->access_flags =
            (int8_t)((aote->access_flags & 0xDF) | (keep_bit5 < 0 ? 0x20 : 0));
        aote->access_flags =
            (int8_t)((aote->access_flags & 0xEF) | (keep_bit4 < 0 ? 0x10 : 0));
        break;
    }

    case ATTR_TYPE_MERGE_ACL: {                             /* 0xE04F66 */
        const ast_$attr_rec_t *rec = (const ast_$attr_rec_t *)value;

        needs_purify = true;                                /* 0xE04F72 */
        old_acl_uid = aote->acl_uid;                        /* 0xE04F74 */
        new_acl_uid = rec->acl_uid;                         /* 0xE04F86: value+0x2C */

        /* 0xE04F8E: a nil owner leaves the AOTE's copy alone. */
        if (uid_is_nil(&rec->owner1) >= 0) {
            aote->owner1 = rec->owner1;                     /* 0xE04FA0 */
            aote->owner1_ext = rec->owner1_ext;
        }
        /* 0xE04FB0: bit 5 of the incoming rights byte means "do not set". */
        if ((rec->rights1 & 0x20) == 0) {
            aote->rights1 = rec->rights1;                   /* 0xE04FB8 */
        }
        /* 0xE04FBE: the owner must match the subject's, else drop bit 5. */
        if (uid_eq(&aote->owner1, &subject->owner1) >= 0) {
            aote->rights1 &= (uint8_t)~0x20;                /* 0xE04FD0 */
        }

        if (uid_is_nil(&rec->owner2) >= 0) {                /* 0xE04FD6 */
            aote->owner2 = rec->owner2;                     /* 0xE04FEA */
            aote->owner2_ext = rec->owner2_ext;
        }
        if ((rec->rights2 & 0x20) == 0) {                   /* 0xE04FFC */
            aote->rights2 = rec->rights2;
        }
        if (uid_eq(&aote->owner2, &subject->owner2) >= 0) { /* 0xE0500A */
            /*
             * 0xE0501E: eight supplementary groups, groups[1] through
             * groups[8] (moveq #7,D1 / dbf, with D0 starting at 8).  A nil
             * entry ends the list and drops bit 5; a match keeps it.
             */
            int16_t i;
            boolean matched = false;

            for (i = 1; i <= 8; i++) {                      /* 0xE05028 */
                if (uid_is_nil(&subject->groups[i]) < 0) {
                    break;                                  /* 0xE0503C */
                }
                if (uid_eq(&aote->owner2, &subject->groups[i]) < 0) {
                    matched = true;                         /* 0xE05054 */
                    break;
                }
            }
            if (matched >= 0) {
                aote->rights2 &= (uint8_t)~0x20;            /* 0xE0505C */
            }
        }

        if (uid_is_nil(&rec->owner3) >= 0) {                /* 0xE05062 */
            aote->owner3 = rec->owner3;                     /* 0xE05076 */
            aote->owner3_ext = rec->owner3_ext;
        }
        if ((rec->rights3 & 0x20) == 0) {                   /* 0xE05088 */
            aote->rights3 = rec->rights3;
        }
        if (uid_eq(&aote->owner3, &subject->owner3) >= 0) { /* 0xE05096 */
            aote->rights3 &= (uint8_t)~0x20;                /* 0xE050AE */
        }
        if ((rec->rights4 & 0x20) == 0) {                   /* 0xE050B4 */
            aote->rights4 = rec->rights4;
        }
        aote->rights5 = (uint8_t)(rec->rights5 & 0xCF);     /* 0xE050C2 */
        /* 0xE050CC: only bit 7 of the record's access byte is taken. */
        aote->access_flags =
            (int8_t)((aote->access_flags & 0x7F) |
                     (rec->access_flags < 0 ? 0x80 : 0));
        aote->acl_uid = rec->acl_uid;                       /* 0xE050E0 */
        break;
    }

    case ATTR_TYPE_SET_RIGHTS: {                            /* 0xE04EC8 */
        const ast_$attr_rec_t *rec = (const ast_$attr_rec_t *)value;

        /* 0xE04ECC: one longword move covering rights1..rights4. */
        aote->rights1 = rec->rights1;
        aote->rights2 = rec->rights2;
        aote->rights3 = rec->rights3;
        aote->rights4 = rec->rights4;
        aote->rights5 = aote->rights4;                      /* 0xE04ED2 */
        break;
    }

    case ATTR_TYPE_LINKCOUNT:                               /* 0xE04EBC */
        aote->linkcount = *(uint16_t *)value;
        break;

    case ATTR_TYPE_DTM_ROUNDED:                             /* 0xE04DA6 */
    case ATTR_TYPE_DTU_ROUNDED: {
        struct {
            uint32_t high;
            uint16_t low;
        } *v = value;

        /*
         * 0xE04DA6: on an obj_type 0 object a non-zero low word rounds up.
         * Unreachable in the shipped kernel: 0xE04B3A only admits attribute
         * types in ATTR_SET_BASIC (0x3FFF) when obj_type is zero, and
         * 0x17/0x18 are not in that set.  Reproduced anyway.
         */
        if (obj_type == 0 && v->low != 0) {
            tmp.pair.high = (int32_t)(v->high + 1);         /* 0xE04DBA */
            tmp.pair.low = 0;
        } else {
            tmp.pair.high = (int32_t)v->high;               /* 0xE04DCA */
            tmp.pair.low = (int16_t)v->low;
        }
        if (attr_type == ATTR_TYPE_DTM_ROUNDED) {           /* 0xE04DD4 */
            aote->dtm_high = (uint32_t)tmp.pair.high;
            aote->dtm_low = (uint16_t)tmp.pair.low;
        } else {
            aote->dtu_high = (uint32_t)tmp.pair.high;       /* 0xE04DEC */
            aote->dtu_low = (uint16_t)tmp.pair.low;
            (*aote_slot)->flags &= (uint8_t)~AOTE_FLAG_TOUCHED; /* 0xE04DF8 */
        }
        break;
    }

    case ATTR_TYPE_SPECIAL_FLAG:                            /* 0xE04C54 */
        aote->attr_flags_lo =
            (uint8_t)((aote->attr_flags_lo & 0xFB) |
                      (uint8_t)(((*(uint8_t *)value) >> 7) << 2));
        break;

    case ATTR_TYPE_DTM_FROM_CLOCK:                          /* 0xE04E06 */
    case ATTR_TYPE_DTU_FROM_CLOCK:
        aote->dtu_high = clock->high;                       /* 0xE04E06 */
        aote->dtu_low = clock->low;
        if (obj_type == 0 && clock->low != 0) {             /* 0xE04E12 */
            aote->dtu_high += 1;                            /* 0xE04E1C */
            aote->dtu_low = 0;
            (*aote_slot)->flags &= (uint8_t)~AOTE_FLAG_TOUCHED; /* 0xE04E24 */
        }
        if (attr_type == ATTR_TYPE_DTM_FROM_CLOCK) {        /* 0xE04E2E */
            aote->dtm_high = aote->dtu_high;                /* 0xE04E38 */
            aote->dtm_low = aote->dtu_low;
        }
        break;

    default:
        goto invalid_attr;                                  /* 0xE04B94 */
    }

    /* 0xE05100: every case but BLOCKS stamps the attribute-modified clock. */
    aote->dta_high = clock->high;
    aote->dta_low = clock->low;

set_dirty_flag:
    /* 0xE05110: bset.b #5 through the re-read aote pointer. */
    (*aote_slot)->flags |= AOTE_FLAG_DIRTY;

    /* 0xE0511A: some attribute types also refresh the absolute clock. */
    if ((AST_$ATTR_TIMESTAMP_MASK & ((uint32_t)1 << attr_type)) != 0) {
        if ((*aote_slot)->remote_flag >= 0) {               /* 0xE0512A: bmi skips */
            TIME_$ABS_CLOCK((clock_t *)&aote->dtv_high);    /* 0xE05132 */
        }
    }
    goto unlock_and_return;

invalid_attr:
    *status = status_$ast_incompatible_request;           /* 0xE050F4 */

unlock_and_return:
    ML_$UNLOCK(PMAP_LOCK_ID);                               /* 0xE0513E: 0x14 */

    /*
     * 0xE0514C: the epilogue is gated on needs_purify (D2b) only.  The three
     * ACL-changing cases (3, 0x13, 0x14) are the ones that set it; BLOCKS
     * reaches here with it still clear.
     */
    if (needs_purify < 0 && (*aote_slot)->remote_flag >= 0) {
        ast_$purify_aote(*aote_slot, 0xFF, status);         /* 0xE05160 */
        ML_$UNLOCK(AST_LOCK_ID);                            /* 0xE05174: 0x12 */

        if (*status != status_$ok) {                        /* 0xE05188 */
            return;
        }

        /*
         * 0xE0518E: "move.b (-0x48,A6),D3b" then "tst.w D3w" - only the top
         * byte of the new ACL UID is tested, not all 32 or 64 bits.
         */
        if (((new_acl_uid.high >> 24) & 0xFF) != 0) {
            tmp.word = 1;                                   /* 0xE05196 */
            ast_$set_attribute_internal(&new_acl_uid, ATTR_TYPE_ADD_REFCOUNT,
                                        &tmp, wait_flag, NULL, clock, status);
            if (*status != status_$ok) {                    /* 0xE051C0 */
                return;
            }
        }

        /* 0xE051C6: likewise the old ACL UID's top byte only. */
        if (((old_acl_uid.high >> 24) & 0xFF) != 0) {
            AST_$TRUNCATE(&old_acl_uid, 0, 3, &truncate_out, status); /* 0xE051E2 */
            /* 0xE051EE: "object not found" is not an error here. */
            if (*status == status_$file_object_not_found) {
                *status = status_$ok;                       /* 0xE051FA */
            }
        }
        return;
    }

    ML_$UNLOCK(AST_LOCK_ID);                                /* 0xE051FE: 0x12 */
}

void ast_$set_attribute_internal(uid_t *uid, uint16_t attr_type, void *value,
                                 boolean wait_flag, ast_$subject_t *subject,
                                 clock_t *clock_info, status_$t *status)
{
    aote_t *aote;            /* A6-0x4C */
    clock_t clock;           /* A6-0x54, a copy of *clock_info */
    boolean is_special_proc; /* D2b */
    int16_t proc_type;
    uint16_t aote_flags;
    boolean len_adjust;      /* D2b, reused after the dispatch call */
    int16_t local_value;     /* A6-0x38 */
    uint32_t net_info[2];    /* A6-0x40, an 8-byte copy of aote+0xAC/0xB0 */

    /* 0xE05220: the caller's clock record is copied into the frame. */
    clock = *clock_info;

    /* 0xE0522E: nil UID goes straight to the validator. */
    if (uid->high == UID_$NIL.high && uid->low == UID_$NIL.low) {
        *status = ast_$validate_uid(uid, 0x30F01);          /* 0xE05240 */
        return;
    }

    /* 0xE05256: process types 8 and 9 are handled specially below. */
    proc_type = PROC1_$DATA.type[PROC1_$CURRENT];
    is_special_proc = (proc_type == 8 || proc_type == 9) ? true : false;

    ML_$LOCK(AST_LOCK_ID);                                  /* 0xE05278: 0x12 */

    aote = ast_$lookup_aote_by_uid(uid);                    /* 0xE05286 */
    if (aote == NULL) {
        /* 0xE05296: not cached - activate it. */
        aote = ast_$force_activate_segment(uid, 0, status, is_special_proc);
        if (aote == NULL) {
            goto unlock_and_return;                         /* 0xE052BA */
        }
    } else {
        aote->flags |= AOTE_FLAG_BUSY;                      /* 0xE052BE: bset.b #6 */
    }

    if (aote->remote_flag < 0) {                            /* 0xE052C8 */
        /* Remote object. */
        if ((((uint32_t)1 << attr_type) & ATTR_SET_LOCAL_ONLY) != 0) {
            *status = file_$object_is_remote;               /* 0xE052E2 */
            goto unlock_and_return;
        }

        /*
         * 0xE052EA: eight bytes are copied out of the AOTE *before* the
         * dispatch runs; the remote call is made from that copy.
         */
        net_info[0] = aote->obj_loc_net;
        net_info[1] = aote->obj_loc_node;

        /* 0xE052FA: bit 0 of the 16-bit attribute flags word. */
        aote_flags = (uint16_t)(((uint16_t)aote->attr_flags_hi << 8) |
                                aote->attr_flags_lo);
        len_adjust = (aote_flags & 1) != 0 ? true : false;

        ast_$set_attr_dispatch(attr_type, value, wait_flag, subject,
                               &aote, &clock, status);      /* 0xE05302 */

        if (*status != status_$ok) {                        /* 0xE0530A */
            return;
        }
        if (wait_flag >= 0) {                               /* 0xE0530E */
            return;
        }

        /* 0xE05314: SET_REFCOUNT on an executable object is sent one lower. */
        if (len_adjust < 0 && attr_type == ATTR_TYPE_SET_REFCOUNT) {
            local_value = (int16_t)(*(int16_t *)value - 1); /* 0xE05326 */
            value = &local_value;
        }

        REM_FILE_$SET_ATTRIBUTE(net_info, uid, attr_type, value, status);
        return;                                             /* 0xE05348 */
    }

    /*
     * 0xE05350: an OS-only object (access_flags bit 7) refuses the change
     * when the caller *is* one of the special process types.
     */
    if (aote->access_flags < 0 && is_special_proc < 0) {
        *status = status_$ast_only_local_access_allowed;     /* 0xE0535E */
        goto unlock_and_return;
    }

    ast_$set_attr_dispatch(attr_type, value, wait_flag, subject,
                           &aote, &clock, status);          /* 0xE05372 */
    return;

unlock_and_return:
    ML_$UNLOCK(AST_LOCK_ID);                                /* 0xE05364: 0x12 */
}
