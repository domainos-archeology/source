/*
 * ACL_$SET_ACL_CHECK - decide whether the caller may replace an object's ACL
 *
 * Original address: 0x00E470C4, 1972 bytes.  A5 = 0xE7CF54.
 *
 * Called by FILE_$SET_ACL's guard (0x00E5DE22) and by ACL_$SERVER
 * (0x00E4989E).  It answers two questions at once: may this caller install
 * `acl_uid` (and the protection record `new_prot`) on `obj_uid`, and does
 * doing so amount to a set-id operation (the `setid_ret` byte)?
 *
 * Frame (A6+):
 *   0x08 obj_uid    (long)  the object whose ACL is being replaced
 *   0x0C new_prot   (long)  the 44-byte protection record being installed
 *   0x10 acl_uid    (long)  the ACL object being installed              -> A2
 *   0x14 op_type    (long)  pointer to an operation-type word           -> A3
 *   0x18 setid_ret  (long)  out: Domain boolean
 *   0x1C status_ret (long)
 *
 * Returns the Domain boolean in D4 (`move.b D4b,D0b` at 0x00E4786C).
 *
 * Locals (A6-):
 *   -0xD4 sid_row      long  0xE97294 + cur*0x24 (a biased pointer)
 *   -0xCE acl_opts     word  the ACL_$RIGHTS option-flags cell
 *   -0xC8 sid_row_copy long
 *   -0xAC cached_flag  byte  acl_$find_acl_slot's out byte (shared)
 *   -0xAA acl_missing  byte  Domain boolean
 *   -0xA6 proj_count   word
 *   -0xA0 result_status long
 *   -0x98 local_uid    8     the caller's obj_uid, copied
 *   -0x90 attrs1       0x38  the OBJECT's attributes; prot1 = -0x84
 *   -0x58 loc          0x20  the object-location record; +0x10 = -0x48
 *   -0x38 attrs2       0x38  the ACL OBJECT's attributes; prot2 = -0x2c
 */

#include "acl/acl_internal.h"
#include "rem_file/rem_file.h"
#include "file/file.h"

/*
 * Constant cells for the ACL_$RIGHTS call at 0x00E47232 (source-u4vy).  Both
 * are `pea (d,PC)` operands; raw bytes at 0x00E47878 are 00 00 00 00 00 0f.
 *
 *   0x00E4722A  pea (0x64c,PC) -> 0x00E47878  byte 0x00
 *   0x00E47226  pea (0x652,PC) -> 0x00E4787A  longword 0x0000000F
 *
 * The option-flags argument is NOT a constant: 0x00E47218-0x00E47222 widens
 * the object type byte attrs1.obj_flags[1] into the frame word at A6-0xCE and
 * passes that.
 */

/* 0x00E47878: ignore_super, FALSE - the super-user bypass applies. */
static const boolean ACL_$SET_ACL_CHECK_IGNORE_SUPER = false;

/* 0x00E4787A: required rights mask - all four of read/write/execute/delete. */
static const uint32_t ACL_$SET_ACL_CHECK_RIGHTS = 0x0000000F;

/*
 * Bit 5 of each per-SID rights byte of the protection record being installed
 * marks "this SID is being changed" (`btst.b #0x5,(0x18,A0)` and the two
 * following bytes at 0x00E47720-0x00E47736).
 */
#define ACL_PROT_SETID_BIT      0x20

/* Bit 3 of the rights word ACL_$RIGHTS returns (`btst.l #0x3,D3` at
 * 0x00E473F4). */
#define ACL_SET_RIGHT_CHANGE    0x00000008U

/* op_type values the code special-cases. */
#define ACL_SET_OP_MERGE        3   /* take the SIDs from the object's own
                                     * protection record, not from new_prot */
#define ACL_SET_OP_NO_SID_CHECK 5   /* 0x00E4770E: skip the SID checks */

/* Object types the two well-known ACL kinds accept (0x00E47646 and
 * 0x00E47670). */
#define ACL_OBJ_TYPE_DIR_A      2
#define ACL_OBJ_TYPE_DIR_B      1
#define ACL_OBJ_TYPE_FILE_A     5
#define ACL_OBJ_TYPE_FILE_B     4
#define ACL_OBJ_TYPE_FILE_C     0

boolean ACL_$SET_ACL_CHECK(uid_t *obj_uid, acl_$prot_data_t *new_prot,
                           uid_t *acl_uid, int16_t *op_type,
                           boolean *setid_ret, status_$t *status_ret)
{
    uid_t             local_uid;        /* A6-0x98 */
    ast_$acl_attr_t   attrs1;           /* A6-0x90, the object's */
    ast_$acl_attr_t   attrs2;           /* A6-0x38, the ACL object's */
    file_$obj_loc_t   loc;              /* A6-0x58, reused by both fetches */
    acl_$prot_data_t *prot1;            /* = attrs1 + 0x0C = A6-0x84 */
    acl_$prot_data_t *prot2;            /* = attrs2 + 0x0C = A6-0x2C */
    acl_sid_block_t  *sid_row;          /* A6-0xD4 / A6-0xC8 */
    uid_t            *proj_row;
    int16_t   acl_opts;                 /* A6-0xCE */
    int16_t   proj_count;               /* A6-0xA6 */
    int8_t    cached_flag;              /* A6-0xAC */
    boolean   acl_missing;              /* A6-0xAA */
    /* A6-0xA0.  Every path that reaches unlock_and_store has assigned it
     * (0x00E473D6, 0x00E473E0, 0x00E473EC, 0x00E473FC, 0x00E47454,
     * 0x00E47472, 0x00E4768C, 0x00E47852); initialised only to keep the C
     * defined. */
    status_$t result_status = status_$ok;
    boolean   result;                   /* D4b */
    uint32_t  rights;                   /* D3 */
    /* D2w / D1w.  The original leaves them undefined when the matching
     * acl_$find_acl_slot call is skipped; every use is guarded by the same
     * "default_acl is not NIL" test that made the call. */
    int16_t   slot1 = ACL_CACHE_NO_SLOT;
    int16_t   slot2 = ACL_CACHE_NO_SLOT;
    int16_t   obj_type;                 /* D5w */
    int16_t   i;
    uid_t    *sid;

    prot1 = ACL_$PROT_DATA(&attrs1);
    prot2 = ACL_$PROT_DATA(&attrs2);

    /* 0x00E470D6-0x00E470E8 */
    local_uid = *obj_uid;
    result = false;
    *setid_ret = result;

    /* 0x00E470EA-0x00E47108 */
    acl_$get_obj_acl_attrs(&local_uid, &loc, &attrs1, status_ret);
    if (*status_ret != status_$ok) {
        goto set_status_bit31;
    }

    /*
     * 0x00E4710C-0x00E471B8: the object's ACL is not held locally and the
     * object itself lives on another node - forward the whole request.
     * `btst.b D4,(-0x8d,A6)` tests bit 0 (D4 is still zero here).
     */
    if ((attrs1.obj_flags[ACL_ATTR_FLAGS] & ACL_ATTR_FLAG_LOCAL) == 0 &&
        loc.flags < 0) {

        /*
         * 0x00E4711C-0x00E47164: how many of the current process' eight
         * project UIDs are in use.  D1 counts from 1 and the answer is the
         * index of the first NIL slot, or 8 when none is NIL.
         */
        proj_count = 8;
        proj_row = &ACL_$PROJ_UIDS[PROC1_$CURRENT][0];
        for (i = 1; i <= 8; i++) {
            if (acl_$uid_eq(&proj_row[i - 1], &UID_$NIL) < 0) {
                proj_count = (int16_t)(i - 1);
                break;
            }
        }

        /*
         * 0x00E47166-0x00E471AA.  The SID block and the project row come
         * from the per-process globals, each with its own reload of
         * PROC1_$CURRENT (0x00E4717A, and 0x00E47122 for the project row).
         */
        REM_FILE_$SET_ACL(&loc.loc_info,
                          &local_uid,
                          acl_uid,
                          new_prot,
                          &ACL_$CURRENT_SIDS[PROC1_$CURRENT],
                          &ACL_$PROJ_UIDS[PROC1_$CURRENT][0],
                          (uint16_t)proj_count,
                          (uint16_t)*op_type,
                          status_ret);

        if (*status_ret != status_$ok) {
            goto done;
        }
        result = true;                                  /* 0x00E471B8 */
        goto done;
    }

    /*
     * 0x00E471BE-0x00E471F6: the ACL object's own attributes.  A missing ACL
     * object is tolerated when the caller asked for the NIL ACL.  Note the
     * SAME location record is reused.
     */
    acl_$get_obj_acl_attrs(acl_uid, &loc, &attrs2, status_ret);
    if (*status_ret != status_$ok) {
        if (*status_ret != file_$object_not_found) {
            goto set_status_bit31;
        }
        if (acl_$uid_eq(acl_uid, &UID_$NIL) >= 0) {
            goto set_status_bit31;
        }
        *status_ret = status_$ok;                       /* 0x00E471F4 */
    }

    /* 0x00E47200-0x00E47212: the ACL object must be of type 3. */
    if ((int16_t)attrs2.obj_flags[ACL_ATTR_OBJ_TYPE] != 3) {
        *status_ret = status_$acl_wrong_type;
        goto done;
    }

    /*
     * 0x00E47216-0x00E47236: the caller needs all four rights on the object,
     * checked with the object's own type as the option-flags word.
     */
    acl_opts = (int16_t)attrs1.obj_flags[ACL_ATTR_OBJ_TYPE];
    rights = ACL_$RIGHTS(&local_uid,
                         (boolean *)&ACL_$SET_ACL_CHECK_IGNORE_SUPER,
                         (uint32_t *)&ACL_$SET_ACL_CHECK_RIGHTS,
                         &acl_opts,
                         status_ret);

    /* 0x00E4723A-0x00E47252 */
    if (*status_ret != status_$ok &&
        *status_ret != status_$no_right_to_perform_operation &&
        *status_ret != status_$insufficient_rights_to_perform_operation) {
        goto done;
    }

    /* ------------------ 0x00E47256: under the ACL lock ------------------ */
    ML_$LOCK(ML_LOCK_ACL);

    /*
     * 0x00E47264-0x00E472AA: locate the OBJECT's current ACL image, unless it
     * has none (a NIL ACL UID together with a present protection record).
     */
    if (!(acl_$uid_eq(&attrs1.default_acl, &UID_$NIL) < 0 &&
          attrs1.obj_flags[ACL_ATTR_PRESENT] != 0)) {
        slot1 = acl_$find_acl_slot(&attrs1.default_acl, &cached_flag, prot1,
                                   status_ret);
        if (*status_ret != status_$ok &&
            *status_ret != status_$acl_object_not_found) {
            goto unlock_and_return;                     /* 0x00E472AA */
        }
    }

    /* 0x00E472AC-0x00E472C0 */
    acl_missing = (*status_ret == status_$acl_object_not_found) ? true : result;

    /* 0x00E472C2-0x00E472FE: and the ACL object's own image. */
    if (!(acl_$uid_eq(&attrs2.default_acl, &UID_$NIL) < 0 &&
          attrs2.obj_flags[ACL_ATTR_PRESENT] != 0)) {
        slot2 = acl_$find_acl_slot(&attrs2.default_acl, &cached_flag, prot2,
                                   status_ret);
        if (*status_ret != status_$ok) {
            goto unlock_and_return;                     /* 0x00E47300 */
        }
    }

    /*
     * 0x00E47310-0x00E473D8: a locksmith is allowed everything, unless the
     * local-locksmith downgrade applies to this type-9 process.
     */
    sid_row = &ACL_$CURRENT_SIDS[PROC1_$CURRENT];
    if (acl_$uid_eq(&sid_row->login_sid, &RGYC_$G_LOCKSMITH_UID) < 0 ||
        acl_$uid_eq(&sid_row->group_sid, &RGYC_$G_LOCKSMITH_UID) < 0 ||
        acl_$uid_eq(&sid_row->user_sid,  &RGYC_$G_LOCKSMITH_UID) < 0) {

        if (ACL_$LOCAL_LOCKSMITH == 0 ||
            PROC1_$TYPE[PROC1_$CURRENT] != 9 ||
            (ACL_$LOCKSMITH_OVERRIDE_BITMAP[ACL_PID_BITMAP_BYTE(PROC1_$CURRENT)] &
             ACL_PID_BITMAP_MASK(PROC1_$CURRENT)) != 0) {

            /* 0x00E473AA-0x00E473D6 */
            result = true;
            ACL_$ASID_SUSER_BITMAP[ACL_PID_BITMAP_BYTE(PROC1_$CURRENT)] |=
                ACL_PID_BITMAP_MASK(PROC1_$CURRENT);
            result_status = status_$ok;
            goto unlock_and_store;
        }
    }

    /* 0x00E473DA-0x00E473F8 */
    if (acl_missing < 0) {
        result_status = status_$acl_object_not_found;
        goto unlock_and_store;
    }
    result_status = status_$no_right_to_perform_operation;
    if ((rights & ACL_SET_RIGHT_CHANGE) == 0) {
        goto unlock_and_store;
    }

    /* From here on the refusal reason is "no right to set subsystem data". */
    result_status = status_$acl_no_right_to_set_subsystem;

    if (acl_$uid_eq(&attrs1.default_acl, &UID_$NIL) >= 0 &&
        acl_$uid_eq(&attrs2.default_acl, &UID_$NIL) >= 0) {

        /* ---- 0x00E47430-0x00E47708: both objects carry an ACL image ---- */
        acl_$cache_slot_t *s1 = &ACL_$ACL_CACHE[slot1];
        acl_$cache_slot_t *s2 = &ACL_$ACL_CACHE[slot2];

        /* 0x00E47454-0x00E4746E: the two ACLs must protect the same kind of
         * object. */
        result_status = status_$acl_wrong_type;
        if (!(s1->type_uid.high == s2->type_uid.high &&
              s1->type_uid.low  == s2->type_uid.low)) {
            goto unlock_and_store;
        }

        result_status = status_$acl_no_right_to_set_subsystem;
        sid_row = &ACL_$CURRENT_SIDS[PROC1_$CURRENT];

        /* 0x00E4747E-0x00E474E4: the subsystem manager may change hands only
         * between "no subsystem" and the caller's own login SID. */
        if (!(s1->subsys_uid.high == s2->subsys_uid.high &&
              s1->subsys_uid.low  == s2->subsys_uid.low)) {

            if (!(s1->subsys_uid.high == s1->type_uid.high &&
                  s1->subsys_uid.low  == s1->type_uid.low &&
                  s2->subsys_uid.high == sid_row->login_sid.high &&
                  s2->subsys_uid.low  == sid_row->login_sid.low)) {
                /* 0x00E474B4-0x00E474DC: the mirror case. */
                if (!(s2->subsys_uid.high == s2->type_uid.high &&
                      s2->subsys_uid.low  == s2->type_uid.low)) {
                    goto unlock_and_store;
                }
                if (!(s1->subsys_uid.high == sid_row->login_sid.high &&
                      s1->subsys_uid.low  == sid_row->login_sid.low)) {
                    goto unlock_and_store;
                }
            }
            *setid_ret = true;                          /* 0x00E474E4 */
        }

        /* 0x00E474E6-0x00E47562: the same rule for the required entry. */
        if (!(s1->required_uid.high == s2->required_uid.high &&
              s1->required_uid.low  == s2->required_uid.low)) {

            if (s1->required_uid.high == UID_$NIL.high &&
                s1->required_uid.low  == UID_$NIL.low) {
                if ((s2->required_uid.high == sid_row->login_sid.high &&
                     s2->required_uid.low  == sid_row->login_sid.low) ||
                    (s2->required_uid.high == local_uid.high &&
                     s2->required_uid.low  == local_uid.low)) {
                    goto set_setid;                     /* 0x00E47704 */
                }
            }
            if (!(s2->required_uid.high == UID_$NIL.high &&
                  s2->required_uid.low  == UID_$NIL.low)) {
                goto unlock_and_store;
            }
            if (!(s1->required_uid.high == sid_row->login_sid.high &&
                  s1->required_uid.low  == sid_row->login_sid.low)) {
                goto unlock_and_store;
            }
            goto set_setid;                             /* 0x00E47704 */
        }
        goto sid_checks;                                /* 0x00E4770A */
    }

    /* ---- 0x00E47564: at most one of the two carries an ACL image ---- */
    if (acl_$uid_eq(&attrs1.default_acl, &UID_$NIL) >= 0 &&
        acl_$uid_eq(&attrs2.default_acl, &UID_$NIL) < 0) {

        /* 0x00E4758C-0x00E475F2: only the OBJECT has an ACL today. */
        acl_$cache_slot_t *s1 = &ACL_$ACL_CACHE[slot1];

        if (!(s1->subsys_uid.high == s1->type_uid.high &&
              s1->subsys_uid.low  == s1->type_uid.low)) {
            sid_row = &ACL_$CURRENT_SIDS[PROC1_$CURRENT];
            if (!(s1->subsys_uid.high == sid_row->login_sid.high &&
                  s1->subsys_uid.low  == sid_row->login_sid.low)) {
                goto unlock_and_store;
            }
        }
        if (s1->required_uid.high == UID_$NIL.high &&
            s1->required_uid.low  == UID_$NIL.low) {
            goto set_setid;
        }
        sid_row = &ACL_$CURRENT_SIDS[PROC1_$CURRENT];
        if (!(s1->required_uid.high == sid_row->login_sid.high &&
              s1->required_uid.low  == sid_row->login_sid.low)) {
            goto unlock_and_store;
        }
        goto set_setid;
    }

    /* 0x00E475F6-0x00E47620 */
    if (acl_$uid_eq(&attrs1.default_acl, &UID_$NIL) < 0 &&
        acl_$uid_eq(&attrs2.default_acl, &UID_$NIL) >= 0) {

        /* 0x00E47624-0x00E476FE: only the incoming ACL has an image. */
        acl_$cache_slot_t *s2 = &ACL_$ACL_CACHE[slot2];

        obj_type = (int16_t)attrs1.obj_flags[ACL_ATTR_OBJ_TYPE];

        /* 0x00E4763A-0x00E47694: the ACL's kind has to suit the object. */
        if (s2->type_uid.high == ACL_$DIR_ACL.high &&
            s2->type_uid.low  == ACL_$DIR_ACL.low) {
            if (!(obj_type == ACL_OBJ_TYPE_DIR_A ||
                  obj_type == ACL_OBJ_TYPE_DIR_B)) {
                result_status = status_$acl_wrong_type;
                goto unlock_and_store;
            }
        } else if (s2->type_uid.high == ACL_$FILE_ACL.high &&
                   s2->type_uid.low  == ACL_$FILE_ACL.low) {
            if (!(obj_type == ACL_OBJ_TYPE_FILE_A ||
                  obj_type == ACL_OBJ_TYPE_FILE_B ||
                  obj_type == ACL_OBJ_TYPE_FILE_C)) {
                result_status = status_$acl_wrong_type;
                goto unlock_and_store;
            }
        }

        /* 0x00E47698-0x00E476C0 */
        if (!(s2->subsys_uid.high == s2->type_uid.high &&
              s2->subsys_uid.low  == s2->type_uid.low)) {
            sid_row = &ACL_$CURRENT_SIDS[PROC1_$CURRENT];
            if (!(s2->subsys_uid.high == sid_row->login_sid.high &&
                  s2->subsys_uid.low  == sid_row->login_sid.low)) {
                goto unlock_and_store;
            }
        }

        /* 0x00E476C4-0x00E47702 */
        if (s2->required_uid.high == UID_$NIL.high &&
            s2->required_uid.low  == UID_$NIL.low) {
            goto set_setid;
        }
        sid_row = &ACL_$CURRENT_SIDS[PROC1_$CURRENT];
        if (s2->required_uid.high == sid_row->login_sid.high &&
            s2->required_uid.low  == sid_row->login_sid.low) {
            goto set_setid;
        }
        if (!(s2->required_uid.high == local_uid.high &&
              s2->required_uid.low  == local_uid.low)) {
            goto unlock_and_store;
        }
        goto set_setid;
    }

    /* Neither carries an ACL image: 0x00E47620 falls straight through. */
    goto sid_checks;

set_setid:                                              /* 0x00E47704 */
    *setid_ret = true;

sid_checks:                                             /* 0x00E4770A */
    /* 0x00E4770E: op type 5 skips every SID check. */
    if (*op_type == ACL_SET_OP_NO_SID_CHECK) {
        goto grant;
    }

    /* 0x00E47716-0x00E47738: no SID is being changed at all. */
    if ((new_prot->owner_rights & ACL_PROT_SETID_BIT) == 0 &&
        (new_prot->group_rights & ACL_PROT_SETID_BIT) == 0 &&
        (new_prot->org_rights   & ACL_PROT_SETID_BIT) == 0) {
        goto grant;
    }

    /* 0x00E4773A-0x00E47756: a type-9 process may not set-id while the
     * local-locksmith feature is in force. */
    if (ACL_$LOCAL_LOCKSMITH != 0 && PROC1_$TYPE[PROC1_$CURRENT] == 9) {
        goto unlock_and_store;
    }

    *setid_ret = true;                                  /* 0x00E4775C */
    sid_row = &ACL_$CURRENT_SIDS[PROC1_$CURRENT];

    /*
     * 0x00E4775E-0x00E4779C: each SID being changed must be one the caller
     * already holds.  Operation type 3 takes the value from the OBJECT's own
     * protection record instead of from `new_prot`.
     */
    if ((new_prot->owner_rights & ACL_PROT_SETID_BIT) != 0) {
        sid = (*op_type == ACL_SET_OP_MERGE) ? &prot1->owner
                                             : &new_prot->owner;
        if (acl_$uid_eq(sid, &sid_row->user_sid) >= 0) {
            goto unlock_and_store;
        }
    }

    /* 0x00E4779E-0x00E4781E */
    if ((new_prot->group_rights & ACL_PROT_SETID_BIT) != 0) {
        sid = (*op_type == ACL_SET_OP_MERGE) ? &prot1->group
                                             : &new_prot->group;
        if (acl_$uid_eq(sid, &sid_row->group_sid) >= 0) {
            /* Not the caller's own group - it must be one of the eight
             * project UIDs, and a NIL slot ends the list. */
            boolean found = false;
            proj_row = &ACL_$PROJ_UIDS[PROC1_$CURRENT][0];
            for (i = 0; i <= 7; i++) {
                if (acl_$uid_eq(&proj_row[i], &UID_$NIL) < 0) {
                    break;                              /* 0x00E47800 */
                }
                if (acl_$uid_eq(&proj_row[i], sid) < 0) {
                    found = true;                       /* 0x00E47812 */
                    break;
                }
            }
            if (found >= 0) {
                goto unlock_and_store;                  /* 0x00E4781C */
            }
        }
    }

    /* 0x00E47820-0x00E47850 */
    if ((new_prot->org_rights & ACL_PROT_SETID_BIT) != 0) {
        sid = (*op_type == ACL_SET_OP_MERGE) ? &prot1->org
                                             : &new_prot->org;
        if (acl_$uid_eq(sid, &sid_row->org_sid) >= 0) {
            goto unlock_and_store;
        }
    }

grant:                                                  /* 0x00E47852 */
    result_status = status_$ok;
    result = true;

unlock_and_store:                                       /* 0x00E47858 */
    ML_$UNLOCK(ML_LOCK_ACL);
    *status_ret = result_status;
    goto done;

unlock_and_return:                                      /* 0x00E47300 */
    ML_$UNLOCK(ML_LOCK_ACL);
    goto done;

set_status_bit31:                                       /* 0x00E471F8 */
    /* `bset.b #0x7,(A0)`: bit 7 of the FIRST byte = bit 31 of the long. */
    *status_ret = (status_$t)((uint32_t)*status_ret | 0x80000000U);

done:                                                   /* 0x00E4786C */
    return result;
}
