/*
 * acl_$get_obj_acl_attrs - fetch an object's location record and ACL attributes
 *
 * Original address: 0x00E45F78 (was FUN_00e45f78), 506 bytes.
 *
 * Callers: acl_$eval_rights (0x00E465EA), ACL_$SET_ACL_CHECK (0x00E470FA and
 * 0x00E471CA), ACL_$RIGHTS' image path (0x00E4690C) and 0x00E46B7A.  Every one
 * of them pushes four arguments and cleans up 0x10 bytes with no result slot,
 * so this is a Pascal procedure - the D0 it happens to leave behind is never
 * read.
 *
 * Frame (A6+):
 *   0x08 uid        (long)  the object UID as the caller has it
 *   0x0C loc        (long)  -> A3, the 0x20-byte file_$obj_loc_t to fill
 *   0x10 attrs      (long)  -> A2, the 0x38-byte ast_$acl_attr_t to fill
 *   0x14 status_ret (long)  -> D3
 *
 * Locals (A6-):
 *   -0x30 hint_key      long   uid.low & 0xFFFFF, HINT_$LOOKUP_CACHE's key
 *   -0x2C hint_flag     byte   HINT_$LOOKUP_CACHE's answer
 *   -0x2A ast_mode      word   1, or 0x21 when the hint says "not local"
 *   -0x24 ast_status    long
 *   -0x20 in_uid        8      the caller's UID, verbatim
 *   -0x18 norm_uid      8      the same with bit 24 of the low half cleared
 *   -0x10 acl_type_uid  8      ACL_$CONVERT_FUNKY_ACL's fourth out-parameter
 */

#include "acl/acl_internal.h"
#include "hint/hint.h"

/*
 * The AST_$GET_ACL_ATTRIBUTES mode words: 1 is the ordinary read, 0x21 adds
 * the bit that makes AST go to the object's home node.  `move.w #0x21` at
 * 0x00E46014 and 0x00E460E0, `move.w #0x1` at 0x00E4601C.
 */
#define ACL_AST_MODE_LOCAL      0x0001
#define ACL_AST_MODE_REMOTE     0x0021

void acl_$get_obj_acl_attrs(uid_t *uid, file_$obj_loc_t *loc,
                            ast_$acl_attr_t *attrs, status_$t *status_ret)
{
    uid_t     in_uid;                   /* A6-0x20 */
    uid_t     norm_uid;                 /* A6-0x18 */
    uid_t     acl_type_uid;             /* A6-0x10 */
    uint32_t  hint_key;                 /* A6-0x30 */
    uint8_t   hint_flag;                /* A6-0x2C -> D2b */
    uint16_t  ast_mode;                 /* A6-0x2A */
    status_$t ast_status;               /* A6-0x24 */
    uint16_t  funky;                    /* D0w */
    uint16_t  obj_type;                 /* D0w at 0x00E46106 / 0x00E4614C */

    in_uid = *uid;                                  /* 0x00E45F90 */
    *status_ret = status_$ok;                       /* 0x00E45F9A */

    /*
     * 0x00E45FA0-0x00E45FAC: the working copy has bit 24 of its low half
     * cleared (`bclr.b #0x0,(-0x14,A6)` - byte 4 of the eight, so bit 0 of the
     * top byte of uid.low).
     */
    norm_uid = in_uid;
    norm_uid.low &= ~0x01000000U;

    /*
     * 0x00E45FAE-0x00E45FBC: bits 4..11 of uid.low's high word select a canned
     * ("funky") ACL encoding; the three the converter knows are 0x80, 0x40 and
     * 0x20 after the shift and mask.
     */
    funky = (uint16_t)(0x0FF0u & (uint16_t)(in_uid.low >> 16));
    funky = (uint16_t)(funky >> 4);
    funky = (uint16_t)(funky & 0x00E0u);
    if (funky != 0) {
        ACL_$CONVERT_FUNKY_ACL(&in_uid, ACL_$PROT_DATA(attrs), &norm_uid,
                               &acl_type_uid, status_ret);
        /* 0x00E45FD8: the converter may set the bit again; clear it. */
        norm_uid.low &= ~0x01000000U;
    }

    loc->uid = norm_uid;                            /* 0x00E45FDE */
    loc->flags &= (int8_t)~FILE_OBJ_LOC_SCRATCH;    /* 0x00E45FEA bclr #6 */

    /*
     * 0x00E45FF0-0x00E4600C: ask the local hint cache whether the object is
     * known to live elsewhere.  The key is passed by reference.
     */
    hint_key = norm_uid.low & 0x000FFFFFU;
    HINT_$LOOKUP_CACHE(&hint_key, &hint_flag);

    /* 0x00E4600E-0x00E46022 */
    if ((int8_t)hint_flag < 0) {
        ast_mode = ACL_AST_MODE_REMOTE;
    } else {
        ast_mode = ACL_AST_MODE_LOCAL;
    }

    /*
     * 0x00E46022-0x00E46066: the well-known volume UIDs answer for themselves.
     * The first test is on the TOP BYTE of norm_uid.high, the second on its
     * top WORD; the last two compare the caller's original UID against the two
     * canned values.
     */
    if (((norm_uid.high >> 24) & 0xFFU) == 0 &&
        ((((norm_uid.high >> 16) & 0xFFFFU) == 1) ||
         (((norm_uid.high >> 16) & 0xFFFFU) == 2) ||
         (acl_$uid_eq(&in_uid, &ACL_$NIL) < 0) ||
         (acl_$uid_eq(&in_uid, &UID_$NIL) < 0))) {

        attrs->default_acl = norm_uid;                      /* 0x00E4606C */
        attrs->obj_flags[ACL_ATTR_OBJ_TYPE] = 3;            /* 0x00E46074 */
        attrs->obj_flags[ACL_ATTR_PRESENT]  = 1;            /* 0x00E4607A */
        loc->flags &= (int8_t)~FILE_OBJ_LOC_REMOTE;         /* 0x00E4607E */
        attrs->obj_flags[ACL_ATTR_FLAGS] &=
            (uint8_t)~ACL_ATTR_FLAG_LOCAL;                  /* 0x00E46084 */

        /* 0x00E4608A-0x00E460AA: the nil UID itself is an error. */
        if (acl_$uid_eq(&in_uid, &UID_$NIL) < 0) {
            *status_ret = file_$object_not_found;           /* 0x000F0001 */
        }
        return;
    }

    /* ----------------------- 0x00E460AE: ask AST ----------------------- */

    AST_$GET_ACL_ATTRIBUTES(loc, ast_mode, attrs, &ast_status);

    /* `tst.w (-0x22,A6)` is the LOW word of the status longword. */
    if (((uint32_t)ast_status & 0xFFFFU) != 0) {            /* 0x00E460C6 */
        *status_ret = ast_status;                           /* 0x00E460F6 */
        return;
    }

    /*
     * 0x00E460CC-0x00E460F4: if AST says the ACL is held locally but the hint
     * cache did not, ask again in remote mode.
     */
    if ((attrs->obj_flags[ACL_ATTR_FLAGS] & ACL_ATTR_FLAG_LOCAL) != 0 &&
        (int8_t)hint_flag >= 0) {
        AST_$GET_ACL_ATTRIBUTES(loc, ACL_AST_MODE_REMOTE, attrs, &ast_status);
        if (((uint32_t)ast_status & 0xFFFFU) != 0) {
            *status_ret = ast_status;                       /* 0x00E460F6 */
            return;
        }
    }

    /* 0x00E460FE-0x00E46148 */
    if (attrs->default_acl.high != 0) {
        obj_type = (uint16_t)attrs->obj_flags[ACL_ATTR_OBJ_TYPE];
        if (obj_type == 2 || obj_type == 1) {
            if (acl_$uid_eq(&attrs->default_acl, &ACL_$NIL) < 0) {
                /* 0x00E4612A: a directory with no ACL gets the canned one. */
                attrs->default_acl = ACL_$DNDCAL;
            } else if ((uint16_t)(attrs->default_acl.high >> 16) == 1) {
                /* 0x00E4613E: `cmpi.w #0x1,(A0)` - the TOP WORD of the high
                 * half, bumped to 2. */
                attrs->default_acl.high =
                    (attrs->default_acl.high & 0x0000FFFFU) | 0x00020000U;
            }
            goto finish;                                    /* 0x00E46148 */
        }
    }

    /* 0x00E4614A-0x00E46160 */
    obj_type = (uint16_t)attrs->obj_flags[ACL_ATTR_OBJ_TYPE];
    if (obj_type == 3) {
        attrs->default_acl = in_uid;                        /* 0x00E4615A */
    }

finish:                                                     /* 0x00E46162 */
    /* `bclr.b #0x0,(0x8,A2)` - byte 4 of default_acl, i.e. bit 24 of its low
     * half, the same normalisation applied to norm_uid above. */
    attrs->default_acl.low &= ~0x01000000U;
}
