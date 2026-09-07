/*
 * control.c - AUDIT_$CONTROL
 *
 * Administrative interface for controlling the audit subsystem.
 *
 * Original address: 0x00E71534
 *
 * Assembly:
 *   00e7153c  lea (0xe854d8).l,A5             ; A5 = &AUDIT_$DATA
 *   00e7154a  cmpi.w #0x6,(A2) / bne          ; the IS_ENABLED fast path
 *   00e71550  tst.b AUDIT_$ENABLED / bpl
 *   00e71558..00e71560  tst.w (-0x2,A5,cur*2) ; suspend_count[cur - 1]
 *   00e71566  0x300011 "audit logging is enabled"
 *   00e71570  0x300004 "event logging is disabled"
 *   00e7157e  bsr AUDIT_$ADMINISTRATOR / tst.b D0b / bmi
 *   00e71592  jsr ACL_$GET_RE_SIDS(&sids, &saved_sids, status)
 *   00e7159c  lea (-0x10,A6),A0               ; = saved_sids + 0x18
 *   00e715a0  movea.l #0xe1742c,A1            ; RGYC_$G_LOGIN_UID
 *   00e715a8  cmpm.l (A1)+,(A0)+ x2           ; an 8-byte UID compare
 *   00e715b0  cmpi.w #0x1,PROC1_$CURRENT
 *   00e715c6  0x300007 "invalid action code"
 *   00e715d4  suspend_count[cur - 1]++
 *   00e715de  jsr ACL_$ENTER_SUPER
 *   00e715e6  cmpi.w #0x6,D0w / bcc 0x00e7167a ; unsigned range check
 *   00e715f0  jump table at 0x00e715f8
 *   00e7167a  jsr ACL_$EXIT_SUPER
 *   00e71688  suspend_count[cur - 1]--
 */

#include "audit/audit_internal.h"
#include "acl/acl.h"
#include "rgyc/rgyc.h"

void AUDIT_$CONTROL(int16_t *command, status_$t *status_ret)
{
    int8_t is_admin;
    const uid_t *login_uid;

    /*
     * ACL_$GET_RE_SIDS (0x00E488B6) writes nine longwords into each of its
     * two buffers; the original frame leaves 0x28 bytes for each.
     */
    uint8_t sids[0x28];
    uint8_t saved_sids[0x28];

    /* 0xE7154A: IS_ENABLED needs no privilege and takes no lock */
    if (*command == AUDIT_CTRL_IS_ENABLED) {
        if (AUDIT_$ENABLED < 0 &&
            AUDIT_$DATA.suspend_count[PROC1_$CURRENT - 1] == 0) {
            *status_ret = status_$audit_logging_is_enabled;   /* 0x300011 */
        } else {
            *status_ret = status_$audit_event_logging_is_disabled; /* 0x300004 */
        }
        return;
    }

    /* 0xE7157A */
    *status_ret = status_$ok;
    is_admin = AUDIT_$ADMINISTRATOR(status_ret);

    /* 0xE71586: a Domain boolean, so "not administrator" is >= 0 */
    if (is_admin >= 0) {
        ACL_$GET_RE_SIDS(sids, saved_sids, status_ret);

        /* 0xE7159C: the SECOND buffer, at offset 0x18 */
        login_uid = (const uid_t *)(saved_sids + 0x18);

        /* Allow the login SID, or PID 1 */
        if ((login_uid->high != RGYC_$G_LOGIN_UID.high ||
             login_uid->low != RGYC_$G_LOGIN_UID.low) &&
            PROC1_$CURRENT != 1) {
            *status_ret = status_$audit_permission_denied;
        }
    }

    /* 0xE715C0 */
    if (*status_ret != status_$ok) {
        return;
    }

    /* 0xE715C6: the default answer for anything the table does not cover */
    *status_ret = status_$audit_invalid_action_code;

    /* 0xE715D4: suspend auditing of this process while the command runs */
    AUDIT_$DATA.suspend_count[PROC1_$CURRENT - 1]++;

    /* 0xE715DE */
    ACL_$ENTER_SUPER();

    /* 0xE715E6: an unsigned bound, so negative codes fall through too */
    switch ((uint16_t)*command) {
    case AUDIT_CTRL_LOAD_LIST:                  /* 0xE71604 */
        audit_$load_list(status_ret);
        break;

    case AUDIT_CTRL_FLUSH:                      /* 0xE7160C */
        ML_$EXCLUSION_START((ml_$exclusion_t *)((char *)AUDIT_$DATA.event_count + 0x0C));
        audit_$close_log(status_ret);   /* 0xE7185E */
        audit_$open_log(status_ret);    /* 0xE716CC */
        ML_$EXCLUSION_STOP((ml_$exclusion_t *)((char *)AUDIT_$DATA.event_count + 0x0C));
        break;

    case AUDIT_CTRL_START:                      /* 0xE7166A */
        audit_$start_logging(status_ret);
        break;

    case AUDIT_CTRL_STOP:                       /* 0xE71672 */
        audit_$stop_logging(status_ret);
        break;

    case AUDIT_CTRL_RESUME_SELF:                /* 0xE7163C */
        /*
         * Undo the entry increment as well as one level of suspension, but
         * only when there is a level to undo (cmpi.w #0x1 / ble).
         */
        if (AUDIT_$DATA.suspend_count[PROC1_$CURRENT - 1] > 1) {
            AUDIT_$DATA.suspend_count[PROC1_$CURRENT - 1]--;
        }
        *status_ret = status_$ok;
        break;

    case AUDIT_CTRL_SUSPEND_SELF:               /* 0xE71654 */
        AUDIT_$DATA.suspend_count[PROC1_$CURRENT - 1]++;
        *status_ret = status_$ok;
        break;

    default:
        /* 0xE715EA: falls straight to the common exit */
        break;
    }

    /* 0xE7167A */
    ACL_$EXIT_SUPER();

    /* 0xE71688 */
    AUDIT_$DATA.suspend_count[PROC1_$CURRENT - 1]--;
}
