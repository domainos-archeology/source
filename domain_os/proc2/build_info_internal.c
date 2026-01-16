/*
 * PROC2_$BUILD_INFO_INTERNAL - Build combined process info structure
 *
 * Builds a combined PROC1+PROC2 info structure (0xE4 bytes total).
 * Called by GET_INFO and INFO functions.
 *
 * Parameters:
 *   proc2_index - Index in PROC2 table (0 for no PROC2 info)
 *   proc1_pid   - PROC1 process ID (0 for no PROC1 info)
 *   info        - Buffer to receive combined info (0xE4 bytes)
 *   status_ret  - Pointer to receive status
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$proc2_zombie - Process is zombie (partial info returned)
 *   status_$proc2_not_level_2_process - Not a valid PROC2 process
 *   status_$proc2_request_is_for_current_process - Querying self
 *
 * Original address: 0x00e4094c
 */

#include "proc2/proc2_internal.h"
#include "misc/string.h"

/* Status codes */
#define status_$proc2_not_level_2_process           0x00190002
#define status_$proc2_request_is_for_current_process 0x00190004

/*
 * Combined process info structure layout (0xE4 bytes):
 * 0x00-0x07: Parent UID (from PROC2 entry offset -0xDC)
 * 0x08: cr_rec pointer (from PROC2 entry)
 * 0x0C-0x23: PROC1 info (from PROC1_$GET_INFO)
 * 0x24-0x4B: ACL SIDs
 * 0x48-0x4F: Process UID (from helper at FUN_00e421de)
 * 0x50: Server flag
 * 0x52-0x53: Priority info
 * 0x54-0x55: Additional priority
 * 0x56-0x65: CPU timing from global tables
 * 0x66-0x6D: Pgroup UID
 * 0x6E-0x6F: Pgroup flags
 * 0x70-0x71: UPID
 * 0x72-0x73: Parent UPID
 * 0x74-0x77: Pgroup info
 * 0x78-0x79: ASID
 * 0x7A-0x83: TTY UID
 * 0x84-0xA3: Signal masks
 * 0xA4-0xA5: Name length
 * 0xA6-0xC5: Process name (32 bytes)
 * 0xC6-0xCD: Accounting UID
 * 0xD0-0xE3: CPU usage
 */
typedef struct proc_info_combined_t {
    uid_t      parent_uid;     /* 0x00 */
    uint32_t    cr_rec;         /* 0x08 */
    uint8_t     proc1_info[24]; /* 0x0C: PROC1 info */
    uid_t      sid[4];         /* 0x24: ACL SIDs */
    uid_t      proc_uid_2;     /* 0x48 */
    uint8_t     server_flag;    /* 0x50 */
    uint8_t     pad_51;         /* 0x51 */
    uint16_t    min_priority;   /* 0x52 */
    uint16_t    max_priority;   /* 0x54 */
    uint32_t    cpu_time[4];    /* 0x56: CPU timing */
    uid_t      pgroup_uid;     /* 0x66 */
    uint16_t    pgroup_flags;   /* 0x6E */
    uint16_t    upid;           /* 0x70 */
    uint16_t    parent_upid;    /* 0x72 */
    uint16_t    pgroup_info;    /* 0x74 */
    uint16_t    session_upid;   /* 0x76 */
    uint16_t    asid;           /* 0x78 */
    uid_t      tty_uid;        /* 0x7A: 10 bytes actually */
    uint8_t     sig_masks[32];  /* 0x84 */
    uint16_t    name_len;       /* 0xA4 */
    char        name[32];       /* 0xA6 */
    uid_t      acct_uid;       /* 0xC6 */
    uint8_t     pad_ce[2];      /* 0xCE */
    uint32_t    cpu_usage[5];   /* 0xD0 */
} proc_info_combined_t;

/*
 * Helper function prototypes (internal to this module)
 * These convert indices/pointers to UIDs
 */
static void get_pgroup_uid(proc2_info_t *entry, uid_t *uid_ret);
static void get_pgroup_info(proc2_info_t *entry, uint16_t *info_ret);

void PROC2_$BUILD_INFO_INTERNAL(int16_t proc2_index, int16_t proc1_pid,
                                 void *info, status_$t *status_ret)
{
    proc_info_combined_t *out = (proc_info_combined_t *)info;
    proc2_info_t *entry;
    uint16_t flags;
    int16_t parent_idx;

    *status_ret = status_$ok;

    /* Handle PROC1 info portion */
    if (proc1_pid == 0) {
        /* No PROC1 info - zero out those fields */
        memset(&out->proc1_info, 0, sizeof(out->proc1_info));
        out->min_priority = 0;
        out->max_priority = 0;

        /* Set SIDs to nil */
        out->sid[0] = UID_$NIL;
        out->sid[1] = UID_$NIL;
        out->sid[2] = UID_$NIL;
        out->sid[3] = UID_$NIL;

        /* Zero CPU times */
        memset(out->cpu_time, 0, sizeof(out->cpu_time));
        memset(out->cpu_usage, 0, sizeof(out->cpu_usage));
    } else {
        /* Get PROC1 info */
        {
            int16_t pid_inout = proc1_pid;
            PROC1_$GET_INFO(&pid_inout, (proc1_$info_t *)out->proc1_info, status_ret);
        }
        if ((*status_ret & 0xFFFF) != 0) {
            *status_ret |= 0x80000000;  /* Set high bit on error */
            return;
        }

        /* Get priority info */
        PROC1_$SET_PRIORITY((uint16_t)proc1_pid, 0, &out->min_priority, &out->max_priority);

        /* Get ACL SIDs */
        ACL_$GET_PID_SID(proc1_pid, out->sid, status_ret);
        if ((*status_ret & 0xFFFF) != 0) {
            *status_ret |= 0x80000000;
            return;
        }

        /* Get CPU times from global table (indexed by PID * 16) */
        /* TODO: Copy from DAT_00e25d10 + pid * 16 */

        /* Get CPU usage */
        PROC1_$GET_ANY_CPU_USAGE(&proc1_pid, &out->cpu_usage[0],
                                  &out->cpu_usage[3], &out->cpu_usage[2]);
        out->cpu_usage[4] = 0x411c;  /* Constant */

        /* Check if querying current process */
        if (proc1_pid == PROC1_$CURRENT) {
            *status_ret = status_$proc2_request_is_for_current_process;
        }
    }

    /* Handle PROC2 info portion */
    if (proc2_index == 0) {
        return;
    }

    entry = P2_INFO_ENTRY(proc2_index);
    flags = entry->flags;

    /* Check if process is valid (0x100) or zombie (0x2000) */
    if ((flags & 0x0100) == 0 && (flags & PROC2_FLAG_ZOMBIE) == 0) {
        /* Not a valid PROC2 process */
        *status_ret = status_$proc2_not_level_2_process;

        /* Fill with nil UIDs */
        out->parent_uid = UID_$NIL;
        out->pgroup_uid = UID_$NIL;
        out->proc_uid_2 = UID_$NIL;
        out->cr_rec = 0;
        out->pgroup_flags = 0;
        out->server_flag = 0;
        out->upid = 0;
        out->parent_upid = 0;
        out->session_upid = 0;
        out->asid = 0;
        out->name_len = 0;
        out->acct_uid = UID_$NIL;
        return;
    }

    if ((flags & 0x0100) == 0) {
        /* Zombie process - partial info */
        out->parent_uid = UID_$NIL;
        out->cr_rec = 0;
        out->asid = 0;
        out->pgroup_uid = UID_$NIL;
        out->proc_uid_2 = UID_$NIL;
        out->pgroup_flags = 0;
        out->upid = 0;
        out->acct_uid = UID_$NIL;

        /* Copy CPU usage from zombie data */
        /* TODO: Copy from entry zombie fields */

        out->name_len = 0;
        *status_ret = status_$proc2_zombie;
    } else {
        /* Valid process - full info */

        /* Copy parent UID from offset -0xDC (relative to table pointer arithmetic) */
        /* This maps to fields at offset 0x08 in entry */
        out->parent_uid.high = *(uint32_t *)((char *)entry + 0x08);
        out->parent_uid.low = *(uint32_t *)((char *)entry + 0x0C);

        /* Copy cr_rec */
        out->cr_rec = entry->cr_rec;

        /* Copy ASID */
        out->asid = entry->asid;

        /* Get pgroup UID using helper */
        get_pgroup_uid(entry, &out->pgroup_uid);
        out->proc_uid_2 = out->pgroup_uid;  /* Same for both fields */

        /* Get pgroup info */
        get_pgroup_info(entry, &out->pgroup_flags);

        /* Copy accounting UID from entry */
        out->acct_uid.high = *(uint32_t *)((char *)entry + 0x78);
        out->acct_uid.low = *(uint32_t *)((char *)entry + 0x7C);

        /* Copy name */
        if (entry->name_len == 0x21) {  /* '!' = no name */
            out->name_len = 0;
        } else if (entry->name_len == 0x22) {  /* '"' = special */
            out->name_len = 0xFFFF;
        } else {
            out->name_len = entry->name_len;
        }
        memcpy(out->name, entry->name, 32);
    }

    /* Common fields for valid and zombie */
    out->server_flag = (flags & 0x0200) ? 0xFF : 0x00;
    out->upid = entry->upid;

    /* Get parent UPID */
    parent_idx = entry->debugger_idx;  /* Field at offset 0x26 used for parent tracking */
    if (parent_idx == 0) {
        out->parent_upid = 1;  /* System process */
    } else {
        proc2_info_t *parent = P2_INFO_ENTRY(parent_idx);
        out->parent_upid = parent->upid;
    }

    /* Get session UPID */
    if (entry->session_id == 0) {
        out->session_upid = 0;
    } else {
        /* TODO: Look up session process's UPID */
        out->session_upid = 0;
    }

    /* Copy TTY UID */
    out->tty_uid = entry->tty_uid;

    /* Copy signal masks */
    memcpy(out->sig_masks, &entry->sig_pending, 32);
}

/*
 * Helper to get pgroup UID from entry
 */
static void get_pgroup_uid(proc2_info_t *entry, uid_t *uid_ret)
{
    /* TODO: Implement using FUN_00e421de logic */
    *uid_ret = entry->pgroup_uid;
}

/*
 * Helper to get pgroup info flags
 */
static void get_pgroup_info(proc2_info_t *entry, uint16_t *info_ret)
{
    /* TODO: Implement using FUN_00e421aa logic */
    *info_ret = entry->pgroup_table_idx;
}
