/*
 * NAME_$INIT - Initialize the Naming Subsystem
 *
 * Initializes the naming subsystem during system boot. Sets up:
 *   - Root, node, node_data, and com directory UIDs
 *   - Per-ASID directory arrays
 *   - Mapped info structures
 *
 * Original addresses:
 *   NAME_$INIT:              0x00e31624 (630 bytes)
 *   name_$init_check_status: 0x00e31578 (122 bytes)
 */

#include "name/name_internal.h"
#include "misc/string.h"

/* External references */
extern int16_t CAL_$BOOT_VOLX;  /* Boot volume index */
extern uint32_t NODE_$ME;       /* This node's ID */

/* Forward declarations */
extern void ACL_$ENTER_SUPER(void);
extern void ACL_$EXIT_SUPER(void);
extern void DIR_$INIT(void);
extern void VTOC_$GET_NAME_DIRS(int16_t volx, uid_t *root_uid, uid_t *node_uid, status_$t *status);
extern void FILE_$SET_DIRPTR(uid_t *dir_uid, uid_t *target_uid, status_$t *status);
extern void VFMT_$FORMATN(char *format, char *output, void *param1, int16_t *out_len);
extern void ERROR_$PRINT(char *msg, void *params);
extern void TIME_$WAIT(void *param1, void *param2, status_$t *status);
extern void CRASH_SYSTEM(status_$t *status);

/* Global NAME data area at 0xE80264 */
uid_t NAME_$NODE_DATA_UID;      /* +0x00 */
/* NAME_$COM_MAPPED_INFO at +0x08 (16 bytes) */
uid_t NAME_$COM_UID;            /* +0x18 */
/* NAME_$NODE_MAPPED_INFO at +0x20 (16 bytes) */
uid_t NAME_$NODE_UID;           /* +0x30 */
uid_t NAME_$ROOT_UID;           /* +0x38 */

/* Canned root UID at separate location 0xE173E4 */
uid_t NAME_$CANNED_ROOT_UID;

/* Maximum number of ASIDs */
#define NAME_MAX_ASID  58  /* 0x3A */

/*
 * name_$init_check_status - Check initialization status and crash on error
 *
 * Called after each initialization step. If status is non-zero, prints
 * error messages and crashes the system.
 *
 * Parameters:
 *   msg    - Error message to print
 *   param1 - First parameter for error message
 *   param2 - Second parameter
 *
 * Note: This function accesses the caller's stack frame to get the status
 * value, which is a Pascal-style nested procedure pattern.
 *
 * Original address: 0x00e31578
 */
static void name_$init_check_status(char *msg, void *param1, int param2,
                                     status_$t *status)
{
    if (*status != status_$ok) {
        /* Print error messages */
        ERROR_$PRINT("               Unable to   ", NULL);
        ERROR_$PRINT(msg, NULL);
        /* TIME_$WAIT and CRASH_SYSTEM would be called here */
        CRASH_SYSTEM(status);
    }
}

/*
 * NAME_$INIT - Initialize the naming subsystem
 *
 * Called during system boot to set up naming services.
 *
 * Parameters:
 *   vol_root_uid - Root directory UID (or UID_$NIL to auto-detect from VTOC)
 *   vol_node_uid - Node directory UID (or UID_$NIL to auto-detect from VTOC)
 *
 * If both UIDs are NIL, retrieves them from the boot volume VTOC.
 */
void NAME_$INIT(uid_t *vol_root_uid, uid_t *vol_node_uid)
{
    uid_t root_uid;
    uid_t node_uid;
    status_$t status;
    int16_t path_len;
    char path_buffer[256];
    char *base = (char *)&NAME_$NODE_DATA_UID;
    int i;
    boolean use_provided_uids;

    ACL_$ENTER_SUPER();

    /* Initialize directory subsystem */
    DIR_$INIT();

    /* Check if UIDs were provided or if we need to get them from VTOC */
    use_provided_uids = !(vol_root_uid->high == UID_$NIL.high &&
                          vol_root_uid->low == UID_$NIL.low);

    if (use_provided_uids) {
        /* Use provided UIDs */
        root_uid.high = vol_root_uid->high;
        root_uid.low = vol_root_uid->low;
        node_uid.high = vol_node_uid->high;
        node_uid.low = vol_node_uid->low;
    } else {
        /* Get UIDs from boot volume VTOC */
        VTOC_$GET_NAME_DIRS(CAL_$BOOT_VOLX, &root_uid, &node_uid, &status);
        name_$init_check_status("get root directory uids from vtoc", NULL, 0, &status);
    }

    /* Set global root and node UIDs */
    NAME_$ROOT_UID.high = root_uid.high;
    NAME_$ROOT_UID.low = root_uid.low;
    NAME_$NODE_UID.high = node_uid.high;
    NAME_$NODE_UID.low = node_uid.low;

    /* Initialize per-ASID arrays */
    /* Each ASID gets:
     *   - Working directory UID at +0x950 + ASID*8
     *   - Naming directory UID at +0x3E0 + ASID*8
     *   - Working dir mapped info at +0x5B0 + ASID*16
     *   - Naming dir mapped info at +0x040 + ASID*16
     */
    for (i = 0; i <= NAME_MAX_ASID; i++) {
        uid_t *wdir_uid = (uid_t *)(base + 0x950 + i * 8);
        uid_t *ndir_uid = (uid_t *)(base + 0x3E0 + i * 8);
        uint8_t *wdir_mapped = (uint8_t *)(base + 0x5B0 + i * 16);
        uint8_t *ndir_mapped = (uint8_t *)(base + 0x040 + i * 16);

        /* Initialize UIDs to node UID */
        wdir_uid->high = NAME_$NODE_UID.high;
        wdir_uid->low = NAME_$NODE_UID.low;
        ndir_uid->high = NAME_$NODE_UID.high;
        ndir_uid->low = NAME_$NODE_UID.low;

        /* Clear mapped info flags */
        wdir_mapped[0] = 0;
        ndir_mapped[0] = 0;
    }

    /* Clear global mapped info flags */
    *(base + 0x20) = 0;  /* NODE_MAPPED_INFO flag */
    *(base + 0x08) = 0;  /* COM_MAPPED_INFO flag */

    /* Map the node directory */
    FUN_00e58488(&NAME_$NODE_UID, 0, base + 0x20, &status);
    name_$init_check_status("map    ", NULL, 0, &status);

    /* Build and resolve "/com" path */
    memcpy(path_buffer, "/com", 4);
    memset(path_buffer + 4, ' ', 252);  /* Fill rest with spaces */
    path_len = 4;

    NAME_$RESOLVE(path_buffer, &path_len, &NAME_$COM_UID, &status);

    /* If /com resolution fails, use node UID as fallback */
    if (status != status_$ok || FUN_00e58488(&NAME_$COM_UID, 0, base + 0x08, &status) >= 0) {
        NAME_$COM_UID.high = NAME_$NODE_UID.high;
        NAME_$COM_UID.low = NAME_$NODE_UID.low;
        /* Copy node mapped info to com mapped info */
        memcpy(base + 0x08, base + 0x20, 16);
    }

    /* Set canned root if not using provided UIDs */
    if (!use_provided_uids) {
        FILE_$SET_DIRPTR(&NAME_$NODE_UID, &NAME_$CANNED_ROOT_UID, &status);
        name_$init_check_status(" .lh  set    as fallback root", NULL, 0, &status);
        if (status != status_$ok) {
            CRASH_SYSTEM(&status);
        }
    }

    /* Format and resolve node_data path */
    /* The path is formatted using VFMT_$FORMATN with NODE_$ME */
    VFMT_$FORMATN("`node_data", path_buffer, &NODE_$ME, &path_len);

    if (use_provided_uids) {
        /* Convert lowercase to uppercase in the formatted path */
        /* (Original code has a character case conversion loop) */
        for (i = 16; i < path_len; i++) {
            if (path_buffer[i - 1] >= 'a' && path_buffer[i - 1] <= 'z') {
                path_buffer[i - 1] -= 32;  /* Convert to uppercase */
            }
        }
    } else {
        path_len = 14;  /* Fixed length for non-provided UIDs case */
    }

    /* Resolve the node_data path */
    NAME_$RESOLVE(path_buffer, &path_len, &NAME_$NODE_DATA_UID, &status);
    name_$init_check_status("resolve  ", path_buffer, path_len, &status);

    ACL_$EXIT_SUPER();
}
