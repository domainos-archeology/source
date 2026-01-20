/*
 * XPD Ptrace Options Functions
 *
 * These functions manage process trace options which control how
 * the debugger receives notifications about target process events.
 *
 * Original addresses:
 *   XPD_$SET_PTRACE_OPTS:       0x00e5af9e
 *   XPD_$INQ_PTRACE_OPTS:       0x00e5b076
 *   XPD_$RESET_PTRACE_OPTS:     0x00e5b156
 *   XPD_$INHERIT_PTRACE_OPTIONS: 0x00e5b174
 */

#include "xpd/xpd.h"

/*
 * Process table addresses and offsets
 */
#define PROC_TABLE_BASE     0xEA551C
#define PROC_ENTRY_SIZE     0xE4

/* Offset from current process index to debugger mapping */
#define CURRENT_TO_INDEX_OFFSET 0xEA93D2

/* Offsets within process entry for ptrace options */
#define PTRACE_OPTS_OFFSET  (-0x16)     /* 0xEA5506 relative to entry base */
#define DEBUGGER_IDX_OFFSET (-0xBE)     /* Debugger index */
#define PARENT_IDX_OFFSET   (-0xC8)     /* Parent process index */

/*
 * XPD_$SET_PTRACE_OPTS - Set process trace options
 *
 * Sets trace options for a target process. If proc_uid is NIL,
 * operates on the current process. Otherwise, the caller must be
 * either the debugger or the parent of the target process.
 */
void XPD_$SET_PTRACE_OPTS(uid_t *proc_uid, xpd_$ptrace_opts_t *opts, status_$t *status_ret)
{
    int16_t index;
    int32_t proc_offset;
    status_$t status;
    uid_t local_uid;
    xpd_$ptrace_opts_t local_opts;
    int16_t current_idx;
    int16_t debugger_idx;
    int16_t parent_idx;

    /* Copy parameters to locals */
    local_uid = *proc_uid;
    local_opts = *opts;
    status = status_$ok;

    /* Lock the PROC2 data */
    ML_$LOCK(PROC2_LOCK_ID);

    /* If UID is NIL, use the current process */
    if (local_uid.high == UID_$NIL.high && local_uid.low == UID_$NIL.low) {
        /* Get current process's index directly from the mapping table */
        index = *(int16_t *)(CURRENT_TO_INDEX_OFFSET + (PROC1_$CURRENT * 2));
    } else {
        /* Find the process by UID */
        index = PROC2_$FIND_INDEX(&local_uid, &status);
    }

    if (status == status_$ok) {
        proc_offset = index * PROC_ENTRY_SIZE;

        /* Get current process index */
        current_idx = *(int16_t *)(CURRENT_TO_INDEX_OFFSET + (PROC1_$CURRENT * 2));

        /* Get target's debugger and parent indices */
        debugger_idx = *(int16_t *)(PROC_TABLE_BASE + proc_offset + DEBUGGER_IDX_OFFSET);
        parent_idx = *(int16_t *)(PROC_TABLE_BASE + proc_offset + PARENT_IDX_OFFSET);

        /* Verify caller is either the debugger or the parent */
        if (current_idx == debugger_idx || current_idx == parent_idx) {
            /* Copy ptrace options to the process entry (14 bytes) */
            uint8_t *dst = (uint8_t *)(PROC_TABLE_BASE + proc_offset + PTRACE_OPTS_OFFSET);
            uint8_t *src = (uint8_t *)&local_opts;

            /* Copy 14 bytes (3 uint32_t + 2 bytes) */
            *(uint32_t *)(dst + 0) = *(uint32_t *)(src + 0);
            *(uint32_t *)(dst + 4) = *(uint32_t *)(src + 4);
            *(uint32_t *)(dst + 8) = *(uint32_t *)(src + 8);
            *(uint16_t *)(dst + 12) = *(uint16_t *)(src + 12);
        } else {
            status = status_$xpd_proc_not_debug_target;
        }
    }

    ML_$UNLOCK(PROC2_LOCK_ID);

    *status_ret = status;
}

/*
 * XPD_$INQ_PTRACE_OPTS - Inquire process trace options
 *
 * Retrieves the current trace options for a target process.
 */
void XPD_$INQ_PTRACE_OPTS(uid_t *proc_uid, xpd_$ptrace_opts_t *opts, status_$t *status_ret)
{
    int16_t index;
    int32_t proc_offset;
    status_$t status;
    uid_t local_uid;
    xpd_$ptrace_opts_t local_opts;
    int16_t current_idx;
    int16_t debugger_idx;
    int16_t parent_idx;

    /* Copy UID to local */
    local_uid = *proc_uid;
    status = status_$ok;

    /* Lock the PROC2 data */
    ML_$LOCK(PROC2_LOCK_ID);

    /* If UID is NIL, use the current process */
    if (local_uid.high == UID_$NIL.high && local_uid.low == UID_$NIL.low) {
        index = *(int16_t *)(CURRENT_TO_INDEX_OFFSET + (PROC1_$CURRENT * 2));
    } else {
        index = PROC2_$FIND_INDEX(&local_uid, &status);
    }

    if (status == status_$ok) {
        proc_offset = index * PROC_ENTRY_SIZE;

        /* Get current process index */
        current_idx = *(int16_t *)(CURRENT_TO_INDEX_OFFSET + (PROC1_$CURRENT * 2));

        /* Get target's debugger and parent indices */
        parent_idx = *(int16_t *)(PROC_TABLE_BASE + proc_offset + PARENT_IDX_OFFSET);
        debugger_idx = *(int16_t *)(PROC_TABLE_BASE + proc_offset + DEBUGGER_IDX_OFFSET);

        /* Verify caller is either the parent or debugger */
        if (current_idx == parent_idx || current_idx == debugger_idx) {
            /* Copy ptrace options from the process entry (14 bytes) */
            uint8_t *src = (uint8_t *)(PROC_TABLE_BASE + proc_offset + PTRACE_OPTS_OFFSET);
            uint8_t *dst = (uint8_t *)&local_opts;

            *(uint32_t *)(dst + 0) = *(uint32_t *)(src + 0);
            *(uint32_t *)(dst + 4) = *(uint32_t *)(src + 4);
            *(uint32_t *)(dst + 8) = *(uint32_t *)(src + 8);
            *(uint16_t *)(dst + 12) = *(uint16_t *)(src + 12);
        } else {
            status = status_$xpd_proc_not_debug_target;
        }
    }

    ML_$UNLOCK(PROC2_LOCK_ID);

    *status_ret = status;

    /* Only copy options out if status is OK */
    if (status == status_$ok) {
        *opts = local_opts;
    }
}

/*
 * XPD_$RESET_PTRACE_OPTS - Reset ptrace options to defaults
 *
 * Clears all fields in the ptrace options structure to zero.
 */
void XPD_$RESET_PTRACE_OPTS(xpd_$ptrace_opts_t *opts)
{
    /* Clear all fields */
    opts->flags = 0;
    opts->signal_mask = 0;
    opts->flags2 = 0;
    opts->trace_range_lo = 0;
    opts->trace_range_hi = 0;
}

/*
 * XPD_$INHERIT_PTRACE_OPTIONS - Check if ptrace options should inherit
 *
 * Checks if bit 3 (0x08) of the flags2 field is set, which indicates
 * that ptrace options should be inherited by child processes on fork.
 *
 * Returns -1 (0xFF) if inherit flag is set, 0 otherwise.
 */
int8_t XPD_$INHERIT_PTRACE_OPTIONS(xpd_$ptrace_opts_t *opts)
{
    /*
     * Check bit 3 of flags2 (at offset 0x0D in the structure)
     * The btst.b #0x3,(0xd,A0) instruction checks this bit
     * sne D0b sets D0 to 0xFF if bit is set, 0 if not
     */
    if ((opts->flags2 & 0x08) != 0) {
        return (int8_t)-1;  /* 0xFF = inherit */
    }
    return 0;               /* 0x00 = don't inherit */
}
