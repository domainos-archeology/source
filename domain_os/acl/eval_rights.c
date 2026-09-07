/*
 * acl_$eval_rights - core access-rights evaluator (0x00E464B8, 1066 bytes)
 *
 * The single evaluator behind ACL_$RIGHTS (0x00E46A00) and ACL_$RIGHTS_CHECK
 * (0x00E46AEC).  Both fill the same nine-argument frame; ACL_$SET_ACL_CHECK
 * reaches it through ACL_$RIGHTS.
 *
 * Frame (A6+):
 *   0x08 sids           (long)  the caller's four-SID block
 *   0x0C proj_uids      (long)  the caller's eight-entry project-UID row
 *   0x10 uid            (long)  the object UID                      -> A2
 *   0x14 ignore_super   (byte)                                      -> D5b
 *   0x16 required_mask  (long)                                      -> D3
 *   0x1A option_flags   (word)                                      -> D2w
 *   0x1C in_super       (byte)                                      -> D4b
 *   0x1E in_subsys      (byte)                                      -> D6b
 *   0x20 status_ret     (long)                                      -> A3
 *
 * Locals (A6-), all recovered from the disassembly:
 *   -0x9C is_locksmith   byte    -0x98 add_read_exec byte
 *   -0x9A cached_flag    byte    (out-parameter of acl_$find_acl_slot)
 *   -0x94 result         long    -0x90 remote_rights long
 *   -0x8C local_status   long    -0x88 proj_p  -0x84 sid_p
 *   -0x80 attrs          0x38 bytes (ast_$acl_attr_t)
 *   -0x48 loc            0x20 bytes (file_$obj_loc_t)
 *   -0x28 local_sids     4 * uid_t
 *
 * The five evaluation paths, in the order the code tries them:
 *   1. super-user short circuit                    (0x00E464DC)
 *   2. remote object -> REM_FILE_$ACL_CHECK_RIGHTS (0x00E46614)
 *   3. locksmith short circuit                     (0x00E46668)
 *   4. the object's own owner/group/org/world
 *      protection record                           (0x00E466D6)
 *   5. the object's full ACL image, under ML lock 10 (0x00E467C4)
 *
 * A5 for this module is 0xE7CF54, so `(0xb70,A5)` is ACL_$LOCAL_LOCKSMITH.
 */

#include "acl/acl_internal.h"
#include "rem_file/rem_file.h"
/* PPO_$NIL_ORG_UID (0xE17574): the organisation SID the local-locksmith
 * downgrade substitutes at 0x00E465A2. */
#include "vtoc/vtoc.h"

uint32_t acl_$eval_rights(acl_sid_block_t *sids, uid_t *proj_uids, uid_t *uid,
                          boolean ignore_super, uint32_t required_mask,
                          int16_t option_flags, boolean in_super,
                          boolean in_subsys, status_$t *status_ret)
{
    file_$obj_loc_t   loc;              /* A6-0x48 */
    ast_$acl_attr_t   attrs;            /* A6-0x80 */
    acl_$prot_data_t *prot;             /* = ACL_$PROT_DATA(&attrs) = A6-0x74 */
    uid_t             local_sids[4];    /* A6-0x28 */
    uid_t            *sid_p;            /* A6-0x84 */
    uid_t            *proj_p;           /* A6-0x88 */
    /*
     * A6-0x9C / A6-0x98.  Both are cleared at 0x00E464F2/0x00E464F6, which the
     * super-user short circuit at 0x00E464E2 jumps over - the original then
     * reads an undefined A6-0x98 at 0x00E4689A.  Initialised here so the C is
     * defined; on that path the original's byte is whatever the previous frame
     * left behind.
     */
    boolean   is_locksmith  = false;
    boolean   add_read_exec = false;
    uint32_t  result = 0;               /* A6-0x94, cleared at 0x00E465DA */
    uint32_t  remote_rights;            /* A6-0x90 */
    status_$t local_status;             /* A6-0x8C */
    int8_t    cached_flag;              /* A6-0x9A */
    uint32_t  rights;                   /* D2 */
    int16_t   slot;                     /* D2w on the ACL-image path */
    int16_t   obj_type;                 /* D0w at 0x00E4668E */
    int16_t   i;
    uint16_t  pid;

    prot = ACL_$PROT_DATA(&attrs);

    /*
     * 0x00E464DC-0x00E464E4:
     *   tst.b D4b / bpl  -> in_super false, fall through
     *   tst.b D5b / bpl  -> ignore_super false, grant everything
     */
    if (in_super < 0 && ignore_super >= 0) {
        goto grant_all;
    }

    /* 0x00E464E6-0x00E464F6 */
    sid_p  = &sids->user_sid;
    proj_p = proj_uids;
    is_locksmith  = false;
    add_read_exec = false;

    /*
     * 0x00E464FA-0x00E46536: is any of the login, group or user SID the
     * locksmith?  The three tests read the *parameter* block, in that order.
     */
    if (acl_$uid_eq(&sids->login_sid, &RGYC_$G_LOCKSMITH_UID) < 0 ||
        acl_$uid_eq(&sids->group_sid, &RGYC_$G_LOCKSMITH_UID) < 0 ||
        acl_$uid_eq(&sids->user_sid,  &RGYC_$G_LOCKSMITH_UID) < 0) {

        is_locksmith = true;                        /* 0x00E4653A st */

        /*
         * 0x00E4653E-0x00E465D6: the "local locksmith" downgrade.  A type-9
         * process that has not called ACL_$OVERRIDE_LOCAL_LOCKSMITH loses the
         * locksmith identity and evaluates as the generic user instead.
         */
        pid = PROC1_$CURRENT;                       /* 0x00E46546 */
        if (ACL_$LOCAL_LOCKSMITH != 0 &&
            PROC1_$TYPE[pid] == 9 &&
            (ACL_$LOCKSMITH_OVERRIDE_BITMAP[ACL_PID_BITMAP_BYTE(PROC1_$CURRENT)] &
             ACL_PID_BITMAP_MASK(PROC1_$CURRENT)) == 0) {

            is_locksmith = false;                   /* 0x00E46582 */
            local_sids[0] = RGYC_$P_USER_UID;       /* 0x00E174F4 */
            local_sids[1] = RGYC_$G_NIL_UID;        /* 0x00E17524 */
            local_sids[2] = PPO_$NIL_ORG_UID;       /* 0x00E17574 */
            local_sids[3] = UID_$NIL;               /* 0x00E1737C */
            sid_p  = local_sids;                    /* 0x00E465BE */
            proj_p = &UID_$NIL;                     /* 0x00E465C6 */
            if (ACL_$LOCAL_LOCKSMITH == 1) {        /* 0x00E465CE */
                add_read_exec = true;
            }
        }
    }

    /* 0x00E465DA-0x00E465EE */
    result = 0;
    acl_$get_obj_acl_attrs(uid, &loc, &attrs, status_ret);

    /* 0x00E465F2-0x00E46602 */
    if (*status_ret != status_$ok) {
        if (is_locksmith < 0) {
            goto grant_all;
        }
        /* `bset.b #0x7,(A3)`: bit 7 of the FIRST byte = bit 31 of the long. */
        *status_ret = (status_$t)((uint32_t)*status_ret | 0x80000000U);
        goto done;
    }

    /*
     * 0x00E46606-0x00E46664: the ACL is not held locally and the object lives
     * on another node - ask that node.  Note the SID block and project row
     * pushed here come from the per-process globals, not from the caller's
     * arguments (0x00E46626-0x00E4664E).
     */
    if ((attrs.obj_flags[ACL_ATTR_FLAGS] & ACL_ATTR_FLAG_LOCAL) == 0 &&
        loc.flags < 0) {
        REM_FILE_$ACL_CHECK_RIGHTS(&loc.loc_info,
                                   &ACL_$CURRENT_SIDS[PROC1_$CURRENT],
                                   &ACL_$PROJ_UIDS[PROC1_$CURRENT][0],
                                   uid,
                                   (uint8_t)ignore_super,
                                   required_mask,
                                   (uint16_t)option_flags,
                                   (uint8_t)in_super,
                                   (uint8_t)in_subsys,
                                   &remote_rights,
                                   status_ret);
        result = remote_rights;                     /* 0x00E4665C */
        goto done;
    }

    /*
     * 0x00E46668-0x00E46688: a locksmith asking about a non-directory object
     * whose group SID is the locksmith UID gets everything.  This test also
     * re-reads the sids *parameter*, not sid_p.
     */
    if (is_locksmith < 0 && option_flags == 0 &&
        acl_$uid_eq(&sids->group_sid, &RGYC_$G_LOCKSMITH_UID) < 0) {
        goto grant_all;
    }

    /*
     * 0x00E4668E-0x00E466C4: the object type must match what the caller said
     * it was asking about.
     */
    obj_type = (int16_t)attrs.obj_flags[ACL_ATTR_OBJ_TYPE];
    if (obj_type != option_flags &&
        !(option_flags == 1 && obj_type == 2) &&        /* seq/seq/and/bmi */
        option_flags != (int16_t)-1 &&
        (option_flags != 0 || (obj_type != 4 && obj_type != 5))) {
        *status_ret = status_$acl_wrong_type;           /* 0x00230004 */
        goto done;
    }

    /* 0x00E466C8-0x00E466CE */
    if (is_locksmith < 0) {
        rights = ACL_RIGHTS_PRIVILEGED;                 /* moveq #-0x51 */
        goto apply_mask;
    }

    /*
     * 0x00E466D6-0x00E467C0: the object has no ACL of its own, so its
     * owner/group/org/world protection record decides.
     */
    if (acl_$uid_eq(&attrs.default_acl, &UID_$NIL) < 0 &&
        attrs.obj_flags[ACL_ATTR_PRESENT] != 0) {

        /* 0x00E466FA-0x00E4671A */
        if ((prot->owner_rights & ACL_RIGHT_IGNORE) == 0 &&
            acl_$uid_eq(&sid_p[0], &prot->owner) < 0) {
            rights = prot->owner_rights;
            goto finish;
        }

        /* 0x00E4671E-0x00E4678E */
        if ((prot->group_rights & ACL_RIGHT_IGNORE) == 0) {
            if (acl_$uid_eq(&sid_p[1], &prot->group) < 0) {
                rights = prot->group_rights;            /* 0x00E4673C */
                goto finish;
            }
            /*
             * 0x00E46746: the project list is walked only when its first slot
             * is not NIL.  The loop then re-tests that same slot - preserved.
             */
            if (acl_$uid_eq(&proj_p[0], &UID_$NIL) >= 0) {
                for (i = 0; i <= 7; i++) {              /* moveq #0x7 + dbf */
                    if (acl_$uid_eq(&proj_p[i], &UID_$NIL) < 0) {
                        break;                          /* 0x00E46778 */
                    }
                    if (acl_$uid_eq(&proj_p[i], &prot->group) < 0) {
                        rights = prot->group_rights;    /* 0x00E4678A */
                        goto finish;
                    }
                }
            }
        }

        /* 0x00E46792-0x00E467B6 */
        if ((prot->org_rights & ACL_RIGHT_IGNORE) == 0 &&
            acl_$uid_eq(&sid_p[2], &prot->org) < 0) {
            rights = prot->org_rights;
            goto finish;
        }

        /* 0x00E467BA */
        rights = prot->world_rights;
        goto finish;
    }

    /* ---------------- 0x00E467C4: the full ACL image ---------------- */
    ML_$LOCK(ML_LOCK_ACL);                              /* 0x00E467CA */

    slot = acl_$find_acl_slot(&attrs.default_acl, &cached_flag, prot,
                              &local_status);           /* 0x00E467E2 */

    /* `tst.w (-0x8a,A6)` is the LOW word of the longword at A6-0x8C. */
    if ((uint16_t)((uint32_t)local_status & 0xFFFFU) != 0) {
        ML_$UNLOCK(ML_LOCK_ACL);                        /* 0x00E467F8 */
        goto store_status;
    }

    /*
     * 0x00E46802-0x0E4684A: a subsystem manager evaluating an object whose
     * cached ACL names it as the subsystem gets the privileged rights mask.
     * The login SID here again comes from the sids parameter (0x00E46814).
     */
    if (slot != ACL_CACHE_NO_SLOT && in_subsys < 0) {
        /* Compared field by field: acl_$cache_slot_t is packed, so taking the
         * address of subsys_uid would be an unaligned pointer on the host. */
        if (sids->login_sid.high == ACL_$ACL_CACHE[slot].subsys_uid.high &&
            sids->login_sid.low  == ACL_$ACL_CACHE[slot].subsys_uid.low &&
            (ignore_super >= 0 || in_super < 0)) {
            rights = ACL_RIGHTS_PRIVILEGED & required_mask;  /* 0x00E46838 */
            ML_$UNLOCK(ML_LOCK_ACL);
            goto finish;
        }
    }

    /* 0x00E4684C-0x00E46884 */
    if (slot == ACL_CACHE_NO_SLOT) {
        rights = prot->world_rights;
    } else {
        /* `clr.l D1 / move.w D0w,D1w`: the result is a zero-extended word. */
        rights = (uint32_t)acl_$eval_acl_entries(&ACL_$ACL_CACHE[slot],
                                                 sid_p, proj_p, prot);
    }
    ML_$UNLOCK(ML_LOCK_ACL);                            /* 0x00E46888 */
    rights &= ACL_RIGHTS_NOT_IGNORED;                   /* andi.l #-0x11 */
    goto finish;

grant_all:
    rights = ACL_RIGHTS_ALL;                            /* 0x00E4668A moveq #0xf */
apply_mask:
    rights &= required_mask;                            /* 0x00E466D0 */

finish:                                                 /* 0x00E4689A */
    if (add_read_exec < 0) {
        rights |= (ACL_RIGHTS_READ_EXECUTE & required_mask);
    }

    local_status = status_$ok;                          /* 0x00E468A6 */
    if (rights == 0) {
        local_status = status_$no_right_to_perform_operation;
    } else if ((required_mask & rights) != required_mask) {
        local_status = status_$insufficient_rights_to_perform_operation;
    }
    result = required_mask & rights;                    /* 0x00E468C8 */

store_status:                                           /* 0x00E468D0 */
    *status_ret = local_status;

done:                                                   /* 0x00E468D4 */
    return result;
}
