/*
 * NAME UID Getter Functions
 *
 * Functions to retrieve various well-known UIDs from the NAME subsystem.
 * These include global UIDs (root, node, node_data, com) and per-ASID
 * UIDs (working directory, naming directory).
 *
 * Data Layout at 0xE80264 (name_$data_base):
 *   +0x000: NAME_$NODE_DATA_UID (8 bytes)
 *   +0x008: NAME_$COM_MAPPED_INFO (16 bytes)
 *   +0x018: NAME_$COM_UID (8 bytes)
 *   +0x020: NAME_$NODE_MAPPED_INFO (16 bytes)
 *   +0x030: NAME_$NODE_UID (8 bytes)
 *   +0x038: NAME_$ROOT_UID (8 bytes)
 *   +0x3E0 + (ASID * 8): NAME_$NDIR_UID[ASID] (per-process naming dir)
 *   +0x950 + (ASID * 8): NAME_$WDIR_UID[ASID] (per-process working dir)
 */

#include "name/name_internal.h"

/* External reference to current address space ID */
extern int16_t PROC1_$AS_ID;   /* 0x00e2060a */

/*
 * NAME data area offsets
 */
#define NAME_DATA_NODE_DATA_UID_OFF  0x000
#define NAME_DATA_COM_UID_OFF        0x018
#define NAME_DATA_NODE_UID_OFF       0x030
#define NAME_DATA_ROOT_UID_OFF       0x038
#define NAME_DATA_NDIR_UID_BASE_OFF  0x3E0
#define NAME_DATA_WDIR_UID_BASE_OFF  0x950

/*
 * NAME_$GET_WDIR_UID - Get current process's working directory UID
 *
 * Retrieves the working directory UID for the current address space.
 * The working directory is used for resolving relative pathnames.
 *
 * Parameters:
 *   uidp - Output: UID of working directory
 *
 * Original address: 0x00e58960
 * Original size: 46 bytes
 */
void NAME_$GET_WDIR_UID(uid_t *uidp)
{
    int16_t offset = PROC1_$AS_ID << 3;  /* Each UID is 8 bytes */
    uid_t *wdir_ptr = (uid_t *)((char *)&NAME_$NODE_DATA_UID +
                                NAME_DATA_WDIR_UID_BASE_OFF + offset);

    uidp->high = wdir_ptr->high;
    uidp->low = wdir_ptr->low;
}

/*
 * NAME_$GET_NDIR_UID - Get current process's naming directory UID
 *
 * Retrieves the naming directory UID for the current address space.
 * The naming directory is used for process-specific name resolution.
 *
 * Parameters:
 *   uidp - Output: UID of naming directory
 *
 * Original address: 0x00e5898e
 * Original size: 46 bytes
 */
void NAME_$GET_NDIR_UID(uid_t *uidp)
{
    int16_t offset = PROC1_$AS_ID << 3;  /* Each UID is 8 bytes */
    uid_t *ndir_ptr = (uid_t *)((char *)&NAME_$NODE_DATA_UID +
                                NAME_DATA_NDIR_UID_BASE_OFF + offset);

    uidp->high = ndir_ptr->high;
    uidp->low = ndir_ptr->low;
}

/*
 * NAME_$GET_ROOT_UID - Get filesystem root directory UID
 *
 * Returns the UID of the filesystem root directory.
 *
 * Parameters:
 *   uidp - Output: UID of root directory
 *
 * Original address: 0x00e589bc
 * Original size: 34 bytes
 */
void NAME_$GET_ROOT_UID(uid_t *uidp)
{
    uidp->high = NAME_$ROOT_UID.high;
    uidp->low = NAME_$ROOT_UID.low;
}

/*
 * NAME_$GET_NODE_UID - Get this node's directory UID
 *
 * Returns the UID of this node's main directory in the network.
 *
 * Parameters:
 *   uidp - Output: UID of node directory
 *
 * Original address: 0x00e589de
 * Original size: 34 bytes
 */
void NAME_$GET_NODE_UID(uid_t *uidp)
{
    uidp->high = NAME_$NODE_UID.high;
    uidp->low = NAME_$NODE_UID.low;
}

/*
 * NAME_$GET_NODE_DATA_UID - Get node data directory UID
 *
 * Returns the UID of the node data directory (`node_data).
 * This directory contains node-specific data and configuration.
 *
 * Parameters:
 *   uidp - Output: UID of node data directory
 *
 * Original address: 0x00e58a00
 * Original size: 32 bytes
 */
void NAME_$GET_NODE_DATA_UID(uid_t *uidp)
{
    uidp->high = NAME_$NODE_DATA_UID.high;
    uidp->low = NAME_$NODE_DATA_UID.low;
}

/*
 * NAME_$GET_CANNED_ROOT_UID - Get canned root UID
 *
 * Returns the "canned" root UID. This is a fallback root UID
 * used when the normal root cannot be determined.
 *
 * Parameters:
 *   uidp - Output: canned root UID
 *
 * Original address: 0x00e58a20
 * Original size: 24 bytes
 */
void NAME_$GET_CANNED_ROOT_UID(uid_t *uidp)
{
    uidp->high = NAME_$CANNED_ROOT_UID.high;
    uidp->low = NAME_$CANNED_ROOT_UID.low;
}
