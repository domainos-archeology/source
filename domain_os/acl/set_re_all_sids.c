/*
 * ACL_$SET_RE_ALL_SIDS - set the requestor SID blocks of the calling process
 *
 * Replaces ACL_$DATA.original_sids[pid] and ACL_$DATA.current_sids[pid] (and, when the
 * original block actually changes, ACL_$DATA.saved_sids[pid]) plus the two 12-byte
 * project-list cells.  A process that is not a super-user may only move a SID
 * between values it already holds; every such check jumps to the audit tail,
 * never straight out of the routine.
 *
 * Original address: 0x00E481AE, 938 bytes (0x00E481AE-0x00E48557).
 *
 * Frame (link.w A6,-0xac):
 *   A6-0xAC  long  saved 0xE97294 + pid*0x0C base (D6)
 *   A6-0x96  word  audit event flag: 1 = attempt, 0 = success
 *   A6-0x90  36 B  ACL_$DATA.original_sids[pid] on entry
 *   A6-0x6C  36 B  ACL_$DATA.current_sids[pid] on entry
 *   A6-0x48  36 B  ACL_$DATA.original_sids[pid] on exit
 *   A6-0x24  36 B  ACL_$DATA.current_sids[pid] on exit
 * The last four are the 0x90-byte audit record; see acl_$set_re_sids_audit_t.
 *
 * Parameters (A6+0x08 .. A6+0x18):
 *   new_original_sids - 36-byte SID block for ACL_$DATA.original_sids[pid]
 *   new_current_sids  - 36-byte SID block for ACL_$DATA.current_sids[pid]
 *   new_saved_proj    - 12-byte cell for ACL_$DATA.saved_proj[pid]
 *   new_current_proj  - 12-byte cell for ACL_$DATA.proj_lists[pid]
 *   status_ret        - status, 0x00230001 on refusal
 *
 * The image leaves junk in D0 on every path (the `moveq #0x1,D0` that precedes
 * each compare, 0xFFFF on the success path, or AUDIT_$LOG_EVENT_S' D0).  No
 * caller reads it: this is a Pascal procedure, so the C returns void.
 */

#include "acl/acl_internal.h"
#include "audit/audit.h"

/*
 * 0x00E48558: the `pea (0x26,PC)` at 0x00E48530 pushes this word - the length
 * of the audit data block.  Image bytes: 00 90.
 */
static const uint16_t acl_$set_re_sids_audit_len = 0x0090;

void ACL_$SET_RE_ALL_SIDS(void *new_original_sids, void *new_current_sids,
                          void *new_saved_proj, void *new_current_proj,
                          status_$t *status_ret)
{
    const acl_sid_block_t *new_orig  = (const acl_sid_block_t *)new_original_sids;
    const acl_sid_block_t *new_curr  = (const acl_sid_block_t *)new_current_sids;
    const acl_proj_list_t *new_sproj = (const acl_proj_list_t *)new_saved_proj;
    const acl_proj_list_t *new_cproj = (const acl_proj_list_t *)new_current_proj;

    acl_$set_re_sids_audit_t aud;
    uint16_t audit_flag;

    acl_sid_block_t *orig;
    acl_sid_block_t *curr;
    acl_sid_block_t *saved;
    acl_proj_list_t *sproj;
    acl_proj_list_t *cproj;

    /* 0x00E481C4-0x00E481CE: refuse by default. */
    *status_ret = status_$no_right_to_perform_operation;

    /*
     * 0x00E481D0-0x00E48216: audit snapshot.  AUDIT_$ENABLED is the Domain
     * boolean at 0xE2E09E; `bpl` skips the block when it is not negative.
     */
    audit_flag = 0;
    if (AUDIT_$ENABLED < 0) {
        audit_flag = 1;                                     /* 0x00E481D8 */
        aud.old_original = ACL_$DATA.original_sids[PROC1_$CURRENT];  /* 0x00E481F8 */
        aud.old_current  = ACL_$DATA.current_sids[PROC1_$CURRENT];   /* 0x00E48206 */
    }

    /*
     * 0x00E48216-0x00E4824E: the five per-process bases the body reuses.
     * A2/D4 = 0xE97294 + pid*0x24, D5 the same value, D6 and A6-0xAC =
     * 0xE97294 + pid*0x0C.
     */
    orig  = &ACL_$DATA.original_sids[PROC1_$CURRENT];   /* base - 0x6E84 */
    curr  = &ACL_$DATA.current_sids[PROC1_$CURRENT];    /* base - 0x6584 */
    saved = &ACL_$DATA.saved_sids[PROC1_$CURRENT];      /* base - 0x5C84 */
    sproj = &ACL_$DATA.saved_proj[PROC1_$CURRENT];      /* base - 0x536C */
    cproj = &ACL_$DATA.proj_lists[PROC1_$CURRENT];      /* base - 0x506C */

    /*
     * 0x00E48252-0x00E48262: acl_$check_suser_pid returns a Domain boolean in
     * D0.b; `bmi` skips the whole permission gate for a super-user.
     */
    if (acl_$check_suser_pid(PROC1_$CURRENT) >= 0) {
        /*
         * 0x00E48266-0x00E48309: each new ORIGINAL field must already be the
         * process' original or current value.
         */
        if (!acl_$uid_eq(&new_orig->user_sid, &orig->user_sid) &&      /* 0x00E48266 */
            !acl_$uid_eq(&new_orig->user_sid, &curr->user_sid)) {      /* 0x00E48276 */
            goto audit_tail;
        }
        if (!acl_$uid_eq(&new_orig->group_sid, &orig->group_sid) &&    /* 0x00E4828C */
            !acl_$uid_eq(&new_orig->group_sid, &curr->group_sid)) {    /* 0x00E4829E */
            goto audit_tail;
        }
        if (!acl_$uid_eq(&new_orig->org_sid, &orig->org_sid) &&        /* 0x00E482B6 */
            !acl_$uid_eq(&new_orig->org_sid, &curr->org_sid)) {        /* 0x00E482C8 */
            goto audit_tail;
        }
        if (!acl_$uid_eq(&new_orig->login_sid, &orig->login_sid) &&    /* 0x00E482E0 */
            !acl_$uid_eq(&new_orig->login_sid, &curr->login_sid)) {    /* 0x00E482F2 */
            goto audit_tail;
        }

        /*
         * 0x00E4830A-0x00E483FD: each new CURRENT field must already be the
         * process' current, original or saved value.
         */
        if (!acl_$uid_eq(&new_curr->user_sid, &curr->user_sid) &&      /* 0x00E4830A */
            !acl_$uid_eq(&new_curr->user_sid, &orig->user_sid) &&      /* 0x00E4831C */
            !acl_$uid_eq(&new_curr->user_sid, &saved->user_sid)) {     /* 0x00E4832C */
            goto audit_tail;
        }
        if (!acl_$uid_eq(&new_curr->group_sid, &curr->group_sid) &&    /* 0x00E48342 */
            !acl_$uid_eq(&new_curr->group_sid, &orig->group_sid) &&    /* 0x00E48358 */
            !acl_$uid_eq(&new_curr->group_sid, &saved->group_sid)) {   /* 0x00E4836A */
            goto audit_tail;
        }
        if (!acl_$uid_eq(&new_curr->org_sid, &curr->org_sid) &&        /* 0x00E48382 */
            !acl_$uid_eq(&new_curr->org_sid, &orig->org_sid) &&        /* 0x00E48396 */
            !acl_$uid_eq(&new_curr->org_sid, &saved->org_sid)) {       /* 0x00E483A8 */
            goto audit_tail;
        }
        if (!acl_$uid_eq(&new_curr->login_sid, &curr->login_sid) &&    /* 0x00E483C0 */
            !acl_$uid_eq(&new_curr->login_sid, &orig->login_sid) &&    /* 0x00E483D4 */
            !acl_$uid_eq(&new_curr->login_sid, &saved->login_sid)) {   /* 0x00E483E6 */
            goto audit_tail;
        }
    }

    /*
     * 0x00E483FE-0x00E4845B: a genuine change of the ORIGINAL group SID
     * (one that the new CURRENT group SID does not already carry) has to be
     * mirrored in the project-UID row.  ACL_$UNWIRED_DATA.super_count[pid] is bumped so
     * that ACL_$DELETE_PROJ/ACL_$ADD_PROJ see a super-user.
     */
    if (!acl_$uid_eq(&new_orig->group_sid, &orig->group_sid) &&
        !acl_$uid_eq(&new_orig->group_sid, &new_curr->group_sid)) {
        ACL_$UNWIRED_DATA.super_count[PROC1_$CURRENT]++;                          /* 0x00E48424 */
        ACL_$DELETE_PROJ(&orig->group_sid, status_ret);              /* 0x00E4843A */
        ACL_$ADD_PROJ((uid_t *)&new_orig->group_sid, status_ret);    /* 0x00E48446 */
        ACL_$UNWIRED_DATA.super_count[PROC1_$CURRENT]--;                          /* 0x00E4844C */
    }

    /*
     * 0x00E4845C-0x00E4847B: `cmpm.l` + `dbne` over nine longwords.  If the
     * ORIGINAL block is about to change at all, the NEW original block (not
     * the old one) is stamped into ACL_$DATA.saved_sids[pid] first.
     */
    if (!acl_$sid_block_eq(orig, new_orig)) {
        *saved = *new_orig;                                          /* 0x00E4846C */
    }

    /* 0x00E4847C-0x00E48499: nine-longword copies. */
    *orig = *new_orig;
    *curr = *new_curr;

    /* 0x00E4849A-0x00E484BB: three-longword copies. */
    *sproj = *new_sproj;
    *cproj = *new_cproj;

    /* 0x00E484BC-0x00E484C2 */
    *status_ret = status_$ok;
    audit_flag = 0;

audit_tail:
    /*
     * 0x00E484C4-0x00E4854D.  Nothing to report when auditing is off, or when
     * neither SID block moved and the operation succeeded.
     */
    if (AUDIT_$ENABLED < 0) {
        if (acl_$sid_block_eq(curr, &aud.old_current) &&    /* 0x00E484CE */
            *status_ret == status_$ok &&                    /* 0x00E484F8 */
            acl_$sid_block_eq(orig, &aud.old_original)) {   /* 0x00E484FE */
            return;                                         /* 0x00E4850E */
        }

        aud.new_original = *orig;                           /* 0x00E48510 */
        aud.new_current  = *curr;                           /* 0x00E48520 */

        /* 0x00E48530-0x00E48548: six arguments, pushed right to left. */
        AUDIT_$LOG_EVENT_S(&AUDIT_$SET_SID_EU, &audit_flag, &aud.old_current,
                           status_ret, (char *)&aud,
                           &acl_$set_re_sids_audit_len);
    }
}
