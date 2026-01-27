/*
 * NAME ASID (Address Space ID) Management Functions
 *
 * Functions to initialize, copy (fork), and free naming state
 * for address spaces (processes).
 *
 * Each ASID has its own:
 *   - Working directory UID and mapped info (16 bytes at 0x950 + ASID*8 / 0x5B0 + ASID*16)
 *   - Naming directory UID and mapped info (16 bytes at 0x3E0 + ASID*8 / 0x040 + ASID*16)
 *
 * Original addresses:
 *   NAME_$INIT_ASID: 0x00e73cfc (318 bytes)
 *   NAME_$FORK:      0x00e73e44 (186 bytes)
 *   NAME_$FREE_ASID: 0x00e74da8 (130 bytes)
 */

#include "name/name_internal.h"

/* Internal helper to unmap directory */
extern void FUN_00e58560(int16_t asid, void *mapped_info);

/*
 * Per-ASID data offsets (relative to name_$data_base at 0xE80264)
 */
#define NAME_DATA_NDIR_UID_BASE_OFF         0x3E0
#define NAME_DATA_WDIR_UID_BASE_OFF         0x950
#define NAME_DATA_NDIR_MAPPED_INFO_BASE_OFF 0x040
#define NAME_DATA_WDIR_MAPPED_INFO_BASE_OFF 0x5B0

/* Mapped info structure size is 16 bytes */
#define MAPPED_INFO_SIZE  16

/*
 * NAME_$INIT_ASID - Initialize naming state for a new address space
 *
 * Called when creating a new process. Copies the current process's
 * working and naming directories to the new ASID, checking ACL access.
 *
 * Parameters:
 *   new_asid   - Pointer to the new address space ID
 *   status_ret - Output: status code
 *
 * Original address: 0x00e73cfc
 */
void NAME_$INIT_ASID(int16_t *new_asid, status_$t *status_ret)
{
    char *base = (char *)&NAME_$NODE_DATA_UID;
    int16_t src_uid_off = PROC1_$AS_ID << 3;
    int16_t dst_uid_off = *new_asid << 3;
    int16_t dst_mapped_off = *new_asid << 4;
    uid_t current_uid;
    uid_t *src_wdir = (uid_t *)(base + NAME_DATA_WDIR_UID_BASE_OFF + src_uid_off);
    uid_t *dst_wdir = (uid_t *)(base + NAME_DATA_WDIR_UID_BASE_OFF + dst_uid_off);
    uid_t *src_ndir = (uid_t *)(base + NAME_DATA_NDIR_UID_BASE_OFF + src_uid_off);
    uid_t *dst_ndir = (uid_t *)(base + NAME_DATA_NDIR_UID_BASE_OFF + dst_uid_off);

    ACL_$ENTER_SUPER();

    /* Copy and map working directory */
    current_uid.high = src_wdir->high;
    current_uid.low = src_wdir->low;

    /* Check ACL access for working directory */
    if (ACL_$RIGHTS(&current_uid, NULL, NULL, NULL, status_ret) != 0) {
        /* Has access - map the directory for the new ASID */
        FUN_00e58488(&current_uid, *new_asid,
                     base + NAME_DATA_WDIR_MAPPED_INFO_BASE_OFF + dst_mapped_off,
                     status_ret);

        if (*status_ret == status_$ok) {
            dst_wdir->high = current_uid.high;
            dst_wdir->low = current_uid.low;
            goto do_ndir;
        }
    } else {
        *status_ret = status_$ok;
do_ndir:
        /* Copy and map naming directory */
        current_uid.high = src_ndir->high;
        current_uid.low = src_ndir->low;

        /* Check ACL access for naming directory */
        if (ACL_$RIGHTS(&current_uid, NULL, NULL, NULL, status_ret) != 0) {
            FUN_00e58488(&current_uid, *new_asid,
                         base + NAME_DATA_NDIR_MAPPED_INFO_BASE_OFF + dst_mapped_off,
                         status_ret);

            if (*status_ret == status_$ok) {
                dst_ndir->high = current_uid.high;
                dst_ndir->low = current_uid.low;
                goto done;
            }
        } else {
            *status_ret = status_$ok;
            goto done;
        }
    }

    /* Set high bit to indicate error */
    *(uint8_t *)status_ret |= 0x80;

done:
    ACL_$EXIT_SUPER();
}

/*
 * NAME_$FORK - Copy naming state from parent to child during fork
 *
 * Copies the working directory, naming directory, and their mapped info
 * structures from the parent ASID to the child ASID.
 *
 * Parameters:
 *   parent_asid - Pointer to parent address space ID
 *   child_asid  - Pointer to child address space ID
 *
 * Original address: 0x00e73e44
 */
void NAME_$FORK(int16_t *parent_asid, int16_t *child_asid)
{
    char *base = (char *)&NAME_$NODE_DATA_UID;
    int16_t parent_uid_off = *parent_asid << 3;
    int16_t child_uid_off = *child_asid << 3;
    int16_t parent_mapped_off = *parent_asid << 4;
    int16_t child_mapped_off = *child_asid << 4;

    uid_t *parent_wdir = (uid_t *)(base + NAME_DATA_WDIR_UID_BASE_OFF + parent_uid_off);
    uid_t *child_wdir = (uid_t *)(base + NAME_DATA_WDIR_UID_BASE_OFF + child_uid_off);
    uid_t *parent_ndir = (uid_t *)(base + NAME_DATA_NDIR_UID_BASE_OFF + parent_uid_off);
    uid_t *child_ndir = (uid_t *)(base + NAME_DATA_NDIR_UID_BASE_OFF + child_uid_off);

    /* Copy working directory UID */
    child_wdir->high = parent_wdir->high;
    child_wdir->low = parent_wdir->low;

    /* Copy naming directory UID */
    child_ndir->high = parent_ndir->high;
    child_ndir->low = parent_ndir->low;

    /* Copy working directory mapped info (16 bytes) */
    memcpy(base + NAME_DATA_WDIR_MAPPED_INFO_BASE_OFF + child_mapped_off,
           base + NAME_DATA_WDIR_MAPPED_INFO_BASE_OFF + parent_mapped_off,
           MAPPED_INFO_SIZE);

    /* Copy naming directory mapped info (16 bytes) */
    memcpy(base + NAME_DATA_NDIR_MAPPED_INFO_BASE_OFF + child_mapped_off,
           base + NAME_DATA_NDIR_MAPPED_INFO_BASE_OFF + parent_mapped_off,
           MAPPED_INFO_SIZE);

    /* The decompiled code shows it copies twice - this appears to be
     * for redundancy or there may be two separate mapped info structures.
     * Replicating the behavior here. */
    memcpy(base + NAME_DATA_WDIR_MAPPED_INFO_BASE_OFF + child_mapped_off,
           base + NAME_DATA_WDIR_MAPPED_INFO_BASE_OFF + parent_mapped_off,
           MAPPED_INFO_SIZE);

    memcpy(base + NAME_DATA_NDIR_MAPPED_INFO_BASE_OFF + child_mapped_off,
           base + NAME_DATA_NDIR_MAPPED_INFO_BASE_OFF + parent_mapped_off,
           MAPPED_INFO_SIZE);
}

/*
 * NAME_$FREE_ASID - Free naming state for an address space
 *
 * Called when a process terminates. Unmaps the directories and
 * resets the UIDs to the node directory UID.
 *
 * Parameters:
 *   asid - Pointer to the address space ID to free
 *
 * Original address: 0x00e74da8
 */
void NAME_$FREE_ASID(int16_t *asid)
{
    char *base = (char *)&NAME_$NODE_DATA_UID;
    int16_t uid_off = *asid << 3;
    int16_t mapped_off = *asid << 4;

    uid_t *wdir = (uid_t *)(base + NAME_DATA_WDIR_UID_BASE_OFF + uid_off);
    uid_t *ndir = (uid_t *)(base + NAME_DATA_NDIR_UID_BASE_OFF + uid_off);

    ACL_$ENTER_SUPER();

    /* Unmap working directory */
    FUN_00e58560(*asid, base + NAME_DATA_WDIR_MAPPED_INFO_BASE_OFF + mapped_off);

    /* Unmap naming directory */
    FUN_00e58560(*asid, base + NAME_DATA_NDIR_MAPPED_INFO_BASE_OFF + mapped_off);

    /* Reset both UIDs to node directory */
    wdir->high = NAME_$NODE_UID.high;
    wdir->low = NAME_$NODE_UID.low;
    ndir->high = NAME_$NODE_UID.high;
    ndir->low = NAME_$NODE_UID.low;

    ACL_$EXIT_SUPER();
}
