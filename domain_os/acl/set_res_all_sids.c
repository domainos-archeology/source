/*
 * ACL_$SET_RES_ALL_SIDS - set the requestor, current and saved SID blocks
 *
 * The super-user-only sibling of ACL_$SET_RE_ALL_SIDS: it also replaces
 * ACL_$DATA.saved_sids[pid] and it has no "the SID is one you already hold"
 * fallback.  A non-super-user is refused, but the refusal still runs through
 * the audit tail (`bpl.w 0x00E486DA` at 0x00E4861E), so the attempt is logged.
 *
 * Original address: 0x00E4855A, 566 bytes (0x00E4855A-0x00E4878F).
 *
 * Frame (link.w A6,-0xf4):
 *   A6-0xF0  long  saved 0xE97294 + pid*0x0C base
 *   A6-0xDE  word  audit event flag: 1 = attempt, 0 = success
 *   A6-0xD8  36 B  ACL_$DATA.original_sids[pid] on entry
 *   A6-0xB4  36 B  ACL_$DATA.current_sids[pid] on entry
 *   A6-0x90  36 B  ACL_$DATA.saved_sids[pid] on entry
 *   A6-0x6C  36 B  ACL_$DATA.original_sids[pid] on exit
 *   A6-0x48  36 B  ACL_$DATA.current_sids[pid] on exit
 *   A6-0x24  36 B  ACL_$DATA.saved_sids[pid] on exit
 * The last six are the 0xD8-byte audit record; see acl_$set_res_sids_audit_t.
 *
 * Parameters (A6+0x08 .. A6+0x1C):
 *   new_original_sids - 36-byte SID block for ACL_$DATA.original_sids[pid]
 *   new_current_sids  - 36-byte SID block for ACL_$DATA.current_sids[pid]
 *   new_saved_sids    - 36-byte SID block for ACL_$DATA.saved_sids[pid]
 *   new_saved_proj    - 12-byte cell for ACL_$DATA.saved_proj[pid]
 *   new_current_proj  - 12-byte cell for ACL_$DATA.proj_lists[pid]
 *   status_ret        - status, 0x00230001 on refusal
 *
 * A Pascal procedure: D0 is left holding whatever the last compare or
 * AUDIT_$LOG_EVENT_S put there, and no caller reads it.
 */

#include "acl/acl_internal.h"
#include "audit/audit.h"

/*
 * 0x00E48790: the `pea (0x26,PC)` at 0x00E48768 pushes this word - the length
 * of the audit data block.  Image bytes: 00 d8.
 */
static const uint16_t acl_$set_res_sids_audit_len = 0x00D8;

void ACL_$SET_RES_ALL_SIDS(void *new_original_sids, void *new_current_sids,
                           void *new_saved_sids, void *new_saved_proj,
                           void *new_current_proj, status_$t *status_ret)
{
    const acl_sid_block_t *new_orig  = (const acl_sid_block_t *)new_original_sids;
    const acl_sid_block_t *new_curr  = (const acl_sid_block_t *)new_current_sids;
    const acl_sid_block_t *new_save  = (const acl_sid_block_t *)new_saved_sids;
    const acl_proj_list_t *new_sproj = (const acl_proj_list_t *)new_saved_proj;
    const acl_proj_list_t *new_cproj = (const acl_proj_list_t *)new_current_proj;

    acl_$set_res_sids_audit_t aud;
    uint16_t audit_flag;

    acl_sid_block_t *orig;
    acl_sid_block_t *curr;
    acl_sid_block_t *saved;
    acl_proj_list_t *sproj;
    acl_proj_list_t *cproj;

    /* 0x00E48570-0x00E4857A: refuse by default. */
    *status_ret = status_$no_right_to_perform_operation;

    /* 0x00E4857C-0x00E485D2: audit snapshot (AUDIT_$ENABLED at 0xE2E09E). */
    audit_flag = 0;
    if (AUDIT_$ENABLED < 0) {
        audit_flag = 1;                                          /* 0x00E48584 */
        aud.old_original = ACL_$DATA.original_sids[PROC1_$CURRENT];   /* 0x00E485A8 */
        aud.old_current  = ACL_$DATA.current_sids[PROC1_$CURRENT];    /* 0x00E485B2 */
        aud.old_saved    = ACL_$DATA.saved_sids[PROC1_$CURRENT];      /* 0x00E485C2 */
    }

    /* 0x00E485D2-0x00E4860A: the five per-process bases. */
    orig  = &ACL_$DATA.original_sids[PROC1_$CURRENT];   /* base - 0x6E84 */
    curr  = &ACL_$DATA.current_sids[PROC1_$CURRENT];    /* base - 0x6584 */
    saved = &ACL_$DATA.saved_sids[PROC1_$CURRENT];      /* base - 0x5C84 */
    sproj = &ACL_$DATA.saved_proj[PROC1_$CURRENT];      /* base - 0x536C */
    cproj = &ACL_$DATA.proj_lists[PROC1_$CURRENT];      /* base - 0x506C */

    /*
     * 0x00E4860C-0x00E4861E: super-user only.  The `bpl.w` goes to the audit
     * tail, not to an early return, so a refused attempt is still logged.
     */
    if (acl_$check_suser_pid(PROC1_$CURRENT) >= 0) {
        goto audit_tail;
    }

    /*
     * 0x00E48622-0x00E4867F: mirror a real group-SID change into the
     * project-UID row, with ACL_$UNWIRED_DATA.super_count[pid] bumped across the pair.
     */
    if (!acl_$uid_eq(&new_orig->group_sid, &orig->group_sid) &&    /* 0x00E48622 */
        !acl_$uid_eq(&new_orig->group_sid, &new_curr->group_sid)) {/* 0x00E48634 */
        ACL_$UNWIRED_DATA.super_count[PROC1_$CURRENT]++;                        /* 0x00E48648 */
        ACL_$DELETE_PROJ(&orig->group_sid, status_ret);            /* 0x00E4865E */
        ACL_$ADD_PROJ((uid_t *)&new_orig->group_sid, status_ret);  /* 0x00E4866A */
        ACL_$UNWIRED_DATA.super_count[PROC1_$CURRENT]--;                        /* 0x00E48670 */
    }

    /* 0x00E48680-0x00E486AF: three nine-longword copies. */
    *orig  = *new_orig;
    *curr  = *new_curr;
    *saved = *new_save;

    /* 0x00E486B0-0x00E486D1: two three-longword copies. */
    *sproj = *new_sproj;
    *cproj = *new_cproj;

    /* 0x00E486D2-0x00E486D8 */
    *status_ret = status_$ok;
    audit_flag = 0;

audit_tail:
    /* 0x00E486DA-0x00E48785 */
    if (AUDIT_$ENABLED < 0) {
        if (acl_$sid_block_eq(saved, &aud.old_saved) &&      /* 0x00E486E4 */
            acl_$sid_block_eq(curr, &aud.old_current) &&     /* 0x00E4870E */
            *status_ret == status_$ok &&                     /* 0x00E48720 */
            acl_$sid_block_eq(orig, &aud.old_original)) {    /* 0x00E48726 */
            return;                                          /* 0x00E48736 */
        }

        aud.new_original = *orig;                            /* 0x00E48738 */
        aud.new_current  = *curr;                            /* 0x00E48748 */
        aud.new_saved    = *saved;                           /* 0x00E48758 */

        /* 0x00E48768-0x00E48780: six arguments, pushed right to left. */
        AUDIT_$LOG_EVENT_S(&AUDIT_$SET_SID_EU, &audit_flag, &aud.old_current,
                           status_ret, (char *)&aud,
                           &acl_$set_res_sids_audit_len);
    }
}
