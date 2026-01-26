/*
 * flop/mount.c - Floppy mount and error check helpers
 *
 * Internal functions for floppy disk boot operations.
 *
 * Original addresses:
 *   flop_$mount_floppy: 0x00E323E6 (338 bytes)
 *   flop_$boot_errchk:  0x00E323A8 (38 bytes)
 */

#include "flop/flop_internal.h"

/*
 * String constants for error messages
 */
static const char trying_normal_shell[] = "Trying normal shell";
static const int16_t trying_normal_shell_len = sizeof(trying_normal_shell) - 1;

/* Mount point name */
static const char flp_name[] = "flp";
static const int16_t flp_name_len = 3;

/* Full path for resolution check */
static const char flp_path[] = "/flp";
static const int16_t flp_path_len = 4;

/*
 * flop_$boot_errchk - Check boot status and report error
 *
 * This function checks if a boot operation succeeded.
 * If not, it calls OS_$BOOT_ERRCHK with the error message
 * and a fallback message of "Trying normal shell".
 *
 * The status is obtained from the caller's caller stack frame
 * (the function that called the boot step function).
 *
 * Parameters:
 *   msg - Error message to display if boot step failed
 *
 * Assembly (0x00E323A8):
 *   link.w  A6,#-0x4
 *   pea     (A2)                     ; Save A2
 *   movea.l (A6),A2                  ; A2 = caller's A6 (frame pointer)
 *   move.l  (0xc,A2),-(SP)           ; Push status_ret from caller's frame
 *   pea     trying_normal_shell_len  ; Push fallback msg length
 *   pea     trying_normal_shell      ; Push fallback msg
 *   move.l  (0x8,A6),-(SP)           ; Push error msg
 *   jsr     OS_$BOOT_ERRCHK
 *   movea.l (-0x8,A6),A2             ; Restore A2
 *   unlk    A6
 *   rts
 */
void flop_$boot_errchk(const char *msg)
{
    /*
     * NOTE: The original code accesses the status_ret parameter from
     * the caller's stack frame by following the A6 link chain.
     * In C, we can't easily replicate this, so we use a different
     * approach - the caller should check the status directly.
     *
     * For now, this is implemented as a wrapper that would need
     * assembly support for the stack frame traversal.
     */

    /*
     * The assembly does:
     *   A2 = *(A6)                   ; Get caller's frame pointer
     *   status_ret = *(A2 + 0xC)     ; Get status_ret from caller's params
     *   OS_$BOOT_ERRCHK(msg, trying_normal_shell, &trying_normal_shell_len, status_ret)
     *
     * This relies on the calling convention and stack layout.
     * A proper implementation would need inline assembly.
     */

    /* Placeholder - in real implementation this would traverse stack frames.
     * The actual assembly implementation should be used.
     * OS_$BOOT_ERRCHK is declared in os/os.h (included via flop_internal.h) */
}

/*
 * flop_$mount_floppy - Mount floppy volume and add to namespace
 *
 * Mounts the floppy disk volume and adds it to the namespace
 * as "/flp". Handles the case where "/flp" already exists.
 *
 * Parameters:
 *   status_ret - Status return
 *
 * The mount process:
 * 1. Call VOLX_$MOUNT to mount the floppy volume
 * 2. Get the current node's UID
 * 3. Add "/flp" directory entry pointing to the mounted volume
 * 4. If "/flp" already exists, verify it points to same UID
 * 5. Set the directory's parent (DAD)
 * 6. On any error after mount, dismount and remove directory
 *
 * Assembly (0x00E323E6):
 *   link.w  A6,#-0x34
 *   movem.l {A2 D2},-(SP)
 *   movea.l (0x8,A6),A2              ; A2 = status_ret
 *   pea     local_uid               ; Push output UID
 *   pea     local_uid2              ; Push unused
 *   move.l  #UID_$NIL,-(SP)         ; Push UID_$NIL
 *   ...                              ; Mount parameters
 *   jsr     VOLX_$MOUNT
 *   ...
 */
void flop_$mount_floppy(status_$t *status_ret)
{
    uid_t mount_uid;           /* UID of mounted volume */
    uid_t node_uid;            /* UID of current node */
    uid_t existing_uid;        /* UID from existing /flp if present */
    status_$t mount_status;     /* Status from mount operation */
    status_$t local_status;     /* Local status for cleanup operations */
    int8_t added_dir = 0;       /* Flag: did we add the directory? */

    /* Step 1: Mount the floppy volume */
    {
        /* Mount parameters - values from data section */
        int16_t vol_type = 0;
        int16_t flags = 0;

        VOLX_$MOUNT(&vol_type, &flags, &flags, &vol_type, &flags, &flags,
                    &UID_$NIL, &mount_uid, &mount_status);
    }

    /* Check mount status - OK or "already mounted" (0x14ffff) are acceptable */
    if (mount_status != status_$ok) {
        mount_status &= 0x7FFFFFFF;  /* Clear high bit */
        if (mount_status != 0x14ffff) {  /* status_$already_mounted */
            *status_ret = mount_status;
            return;
        }
    }

    /* Step 2: Get current node's UID */
    NAME_$GET_NODE_UID(&node_uid);

    /* Step 3: Add /flp directory entry */
    DIR_$ADDU(&node_uid, flp_name, (int16_t *)&flp_name_len, &mount_uid, status_ret);

    if (*status_ret == status_$ok) {
        added_dir = -1;  /* We added the directory */
    } else if (*status_ret == status_$name_already_exists) {
        /* /flp already exists - verify it points to same volume */
        added_dir = 0;
        NAME_$RESOLVE(flp_path, (int16_t *)&flp_path_len, &existing_uid, &local_status);

        if (local_status == status_$ok &&
            mount_uid.high == existing_uid.high &&
            mount_uid.low == existing_uid.low) {
            /* Same UID, we can proceed */
            *status_ret = status_$ok;
        }
        /* else status_ret remains as error */
    }

    if (*status_ret != status_$ok) {
        goto cleanup;
    }

    /* Step 4: Set the directory's parent */
    DIR_$SET_DAD(&mount_uid, &node_uid, status_ret);
    *status_ret &= 0x7FFFFFFF;  /* Clear high bit */

    /* Ignore write-protected error - floppy may be read-only */
    if (*status_ret == status_$disk_write_protected) {
        *status_ret = status_$ok;
    }

    if (*status_ret == status_$ok) {
        /* Success - return the mount status (may be "already mounted") */
        *status_ret = mount_status;
        return;
    }

cleanup:
    /* Error occurred - clean up */
    {
        int16_t flags = 0;
        VOLX_$DISMOUNT(&flags, &flags, &flags, &flags, &mount_uid, &flags, &local_status);
    }

    if (added_dir < 0) {
        /* We added the directory, so remove it */
        DIR_$DROPU(&node_uid, flp_name, (int16_t *)&flp_name_len, &mount_uid, &local_status);
    }
}
