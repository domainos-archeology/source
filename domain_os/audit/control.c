/*
 * control.c - AUDIT_$CONTROL
 *
 * Administrative interface for controlling the audit subsystem.
 *
 * Original address: 0x00E71534
 */

#include "audit/audit_internal.h"
#include "acl/acl.h"
#include "rgyc/rgyc.h"

/* Forward declaration for function not yet in acl.h */
extern void ACL_$GET_RE_SIDS(void *sid_buffer, void *saved_sid, status_$t *status_ret);

void AUDIT_$CONTROL(int16_t *command, status_$t *status_ret)
{
    int8_t is_admin;
    int16_t pid;
    uint8_t sid_buffer[40];
    uint8_t saved_sid[24];
    uid_t login_uid;

    /* Handle IS_ENABLED command specially (no admin check needed) */
    if (*command == AUDIT_CTRL_IS_ENABLED) {
        if (AUDIT_$ENABLED < 0 && AUDIT_$DATA.suspend_count[PROC1_$CURRENT] == 0) {
            /* Auditing is enabled and process is not suspended */
            *status_ret = status_$audit_not_enabled;  /* 0x00300011 */
        } else {
            /* Auditing is disabled or process is suspended */
            *status_ret = 0x00300004;
        }
        return;
    }

    /* Check administrator privileges */
    *status_ret = status_$ok;
    is_admin = AUDIT_$ADMINISTRATOR(status_ret);

    if (is_admin >= 0) {
        /* Not an administrator - check if it's the login process */
        ACL_$GET_RE_SIDS(sid_buffer, saved_sid, status_ret);

        /* Extract login UID from SID buffer (at offset 0x18) */
        login_uid.high = *(uint32_t *)(sid_buffer + 0x18);
        login_uid.low = *(uint32_t *)(sid_buffer + 0x1C);

        /* Allow if login UID matches system login UID, or if PID 1 */
        if ((login_uid.high != RGYC_$G_LOGIN_UID.high ||
             login_uid.low != RGYC_$G_LOGIN_UID.low) &&
            PROC1_$CURRENT != 1) {
            *status_ret = status_$audit_not_administrator;
            return;
        }
    }

    if (*status_ret != status_$ok) {
        return;
    }

    /* Process command */
    *status_ret = status_$audit_invalid_command;

    /* Suspend auditing for the caller while processing */
    pid = PROC1_$CURRENT;
    AUDIT_$DATA.suspend_count[pid]++;

    /* Enter super mode for file access */
    ACL_$ENTER_SUPER();

    switch (*command) {
    case AUDIT_CTRL_LOAD_LIST:
        /* Reload audit list from file */
        audit_$load_list(status_ret);
        break;

    case AUDIT_CTRL_FLUSH:
        /* Flush audit buffer */
        ML_$EXCLUSION_START((ml_$exclusion_t *)((char *)AUDIT_$DATA.event_count + 0x0C));
        audit_$close_log(status_ret);
        audit_$open_log(status_ret);
        ML_$EXCLUSION_STOP((ml_$exclusion_t *)((char *)AUDIT_$DATA.event_count + 0x0C));
        break;

    case AUDIT_CTRL_START:
        /* Start audit logging */
        audit_$start_logging(status_ret);
        break;

    case AUDIT_CTRL_STOP:
        /* Stop audit logging */
        audit_$stop_logging(status_ret);
        break;

    case AUDIT_CTRL_SUSPEND_SELF:
        /* Suspend auditing for caller (extra increment) */
        if (AUDIT_$DATA.suspend_count[pid] > 1) {
            AUDIT_$DATA.suspend_count[pid]--;
        }
        *status_ret = status_$ok;
        break;

    case AUDIT_CTRL_RESUME_SELF:
        /* Resume auditing for caller */
        AUDIT_$DATA.suspend_count[pid]++;
        *status_ret = status_$ok;
        break;

    default:
        /* Invalid command - status already set */
        break;
    }

    /* Exit super mode */
    ACL_$EXIT_SUPER();

    /* Resume auditing for caller */
    AUDIT_$DATA.suspend_count[pid]--;
}
