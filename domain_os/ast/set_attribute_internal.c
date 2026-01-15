/*
 * ast_$set_attribute_internal - Internal attribute setting function
 *
 * Sets attributes on an object, handling both local and remote objects.
 * For remote objects, calls the remote file service. For local objects,
 * performs the operation directly if allowed.
 *
 * Parameters:
 *   uid - Object UID
 *   attr_type - Attribute type to set
 *   value - Pointer to attribute value
 *   wait_flag - Wait for completion (negative = wait)
 *   param_5 - Reserved/unused
 *   param_6 - Additional parameters (clock info)
 *   status - Output status
 *
 * Original address: 0x00e05214
 */

#include "ast/ast_internal.h"

/* External function prototypes */
extern void FUN_00e04b00(void);  /* Attribute setting helper */
extern void REM_FILE_$SET_ATTRIBUTE(void *net_info, uid_t *uid, uint16_t attr_type,
                                     void *value, status_$t *status);

/* Process type table */
#if defined(M68K)
#define PROC1_$TYPE        ((int16_t *)0xE2612C)
#define PROC1_$CURRENT     (*(uint16_t *)0xE20608)
#else
extern int16_t proc1_$type[];
extern uint16_t proc1_$current;
#define PROC1_$TYPE        proc1_$type
#define PROC1_$CURRENT     proc1_$current
#endif

/* Nil UID for comparison */
#if defined(M68K)
#define UID_$NIL           (*(uid_t *)0xE1737C)
#else
extern uid_t uid_$nil;
#define UID_$NIL           uid_$nil
#endif

/* Status codes */
#define status_$file_object_is_remote       0x000F0002
#define status_$os_only_local_access_allowed 0x0003000A

void ast_$set_attribute_internal(uid_t *uid, uint16_t attr_type, void *value,
                                  int8_t wait_flag, void *param_5,
                                  clock_t *clock_info, status_$t *status)
{
    aote_t *aote;
    int8_t is_os_process;
    int16_t proc_type;
    uint16_t aote_flags;
    int16_t local_value;
    void *net_info;

    /* Check for nil UID */
    if (uid->high == UID_$NIL.high && uid->low == UID_$NIL.low) {
        *status = ast_$validate_uid(uid, 0x30F01);
        return;
    }

    /* Check if caller is OS process (type 8 or 9) */
    proc_type = PROC1_$TYPE[PROC1_$CURRENT];
    is_os_process = (proc_type == 8) ? -1 : 0;
    if (proc_type == 9) {
        is_os_process = -1;
    }

    ML_$LOCK(AST_LOCK_ID);

    /* Look up AOTE by UID */
    aote = ast_$lookup_aote_by_uid(uid);

    if (aote == NULL) {
        /* Not in cache - force activate */
        aote = ast_$force_activate_segment(uid, 0, status, is_os_process);
        if (aote == NULL) {
            goto unlock_and_return;
        }
    } else {
        /* Found in cache - set touched flag */
        *((uint8_t *)((char *)aote + 0xBF)) |= 0x40;
    }

    /* Check if remote object */
    if (*((int8_t *)((char *)aote + 0xB9)) < 0) {
        /* Remote object */
        /* Check if attribute type is in the local-only set (bits 6, 7, 11 = 0x8C0) */
        if ((((uint32_t)1 << attr_type) & 0x8C0) != 0) {
            *status = status_$file_object_is_remote;
            goto unlock_and_return;
        }

        /* Get network info from AOTE */
        net_info = (char *)aote + 0xAC;

        /* Get AOTE flags */
        aote_flags = *((uint16_t *)((char *)aote + 0x0E));

        /* Call attribute helper */
        FUN_00e04b00();

        if (*status != status_$ok) {
            return;  /* Note: lock still held - intentional? */
        }

        if (wait_flag >= 0) {
            return;  /* Non-blocking, done */
        }

        /* Adjust value for certain attributes if object is executable */
        if ((aote_flags & 1) && attr_type == 8) {
            /* Executable object, attr type 8: adjust value */
            local_value = *((int16_t *)value) - 1;
            value = &local_value;
        }

        /* Call remote file service */
        REM_FILE_$SET_ATTRIBUTE(net_info, uid, attr_type, value, status);
        return;
    } else {
        /* Local object */
        /* Check if OS-only access is required (byte at 0x71 negative) */
        if (*((int8_t *)((char *)aote + 0x71)) < 0 && is_os_process >= 0) {
            *status = status_$os_only_local_access_allowed;
            goto unlock_and_return;
        }

        /* Call attribute helper */
        FUN_00e04b00();
        return;
    }

unlock_and_return:
    ML_$UNLOCK(AST_LOCK_ID);
}
