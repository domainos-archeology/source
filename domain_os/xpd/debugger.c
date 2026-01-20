/*
 * XPD Debugger Registration Functions
 *
 * These functions manage debugger registration and the debugger/target
 * relationship. The debugger table supports up to 6 debugger processes.
 *
 * Original addresses:
 *   XPD_$SET_DEBUGGER:          0x00e5bbd8
 *   XPD_$FIND_DEBUGGER_INDEX:   0x00e5badc
 *   XPD_$REGISTER_DEBUGGER:     0x00e5bb1e
 *   XPD_$UNREGISTER_DEBUGGER:   0x00e74f7c
 */

#include "xpd/xpd.h"

/*
 * Debugger table layout:
 * Base: 0xEA5034 + 0x10 = 0xEA5044
 * Each entry: 16 bytes (0x10)
 *   Offset 0x00-0x0B: Eventcount (12 bytes)
 *   Offset 0x0C-0x0D: ASID of debugger (2 bytes, 0 = free slot)
 *   Offset 0x0E-0x0F: Padding (2 bytes)
 *
 * ASID is at: base + entry * 0x10 + 0x484 - 0x10 = base + entry * 0x10 + 0x474
 * Actually from the code: 0xEA5044 + 0x484 = 0xEA54C8 for entry 0's ASID
 * So ASID is at: 0xEA5044 + entry_idx * 0x10 + 0x484 - 0x10
 * Simplified: 0xEA5034 + 0x484 + (entry_idx - 1) * 0x10 + 0x0C
 */
#define XPD_DATA_BASE           0xEA5034
#define DEBUGGER_ASID_OFFSET    0x484   /* Offset from entry base to ASID */

/* Process table offsets */
#define PROC_TABLE_BASE         0xEA551C
#define PROC_ENTRY_SIZE         0xE4

/* Offset within target state for debugger index (3 bits at 0x10) */
#define TARGET_STATE_OFFSET     0x10

/* PROC2 UID array base */
#define PROC2_UID_BASE          0xE7BE94

/*
 * XPD_$FIND_DEBUGGER_INDEX - Find debugger slot index
 *
 * Searches the debugger table for a slot with matching ASID.
 * Returns the 1-based index (1-6) or 0 if not found.
 */
int16_t XPD_$FIND_DEBUGGER_INDEX(int16_t asid, status_$t *status_ret)
{
    int16_t i;
    int32_t addr;

    /* Search 6 entries (indices 1-6) */
    for (i = 1; i <= 6; i++) {
        addr = XPD_DATA_BASE + 0x10 + (i - 1) * 0x10 + DEBUGGER_ASID_OFFSET;

        if (*(int16_t *)addr == asid) {
            *status_ret = status_$ok;
            return i;
        }
    }

    *status_ret = status_$xpd_not_a_debugger;
    return 0;
}

/*
 * XPD_$REGISTER_DEBUGGER - Register as a debugger
 *
 * Allocates a debugger table slot for the given ASID.
 * Returns the 1-based index (1-6) or 0 if table is full or already registered.
 */
int16_t XPD_$REGISTER_DEBUGGER(int16_t asid, status_$t *status_ret)
{
    int16_t i;
    int16_t free_slot;
    int32_t addr;

    ML_$LOCK(XPD_LOCK_ID);

    free_slot = 0;

    /* Search for existing registration or free slot */
    for (i = 1; i <= 6; i++) {
        addr = XPD_DATA_BASE + 0x10 + (i - 1) * 0x10 + DEBUGGER_ASID_OFFSET;

        if (*(int16_t *)addr == 0) {
            /* Found a free slot - remember it if first one */
            if (free_slot == 0) {
                free_slot = i;
            }
        } else if (*(int16_t *)addr == asid) {
            /* Already registered */
            ML_$UNLOCK(XPD_LOCK_ID);
            *status_ret = status_$xpd_already_a_debugger;
            return i;
        }
    }

    if (free_slot == 0) {
        /* No free slots */
        ML_$UNLOCK(XPD_LOCK_ID);
        *status_ret = status_$xpd_debugger_table_full;
        return 0;
    }

    /* Allocate the slot */
    addr = XPD_DATA_BASE + 0x10 + (free_slot - 1) * 0x10;
    *(int16_t *)(addr + DEBUGGER_ASID_OFFSET) = asid;

    /* Clear the EC at this slot */
    *(uint32_t *)(addr + 0x478) = 0;    /* EC for this debugger's notifications */

    ML_$UNLOCK(XPD_LOCK_ID);
    *status_ret = status_$ok;
    return free_slot;
}

/*
 * XPD_$UNREGISTER_DEBUGGER - Unregister as a debugger
 *
 * Releases the debugger slot and continues all targets that were
 * being debugged by this process.
 */
void XPD_$UNREGISTER_DEBUGGER(int16_t asid, status_$t *status_ret)
{
    int16_t i;
    int16_t j;
    int16_t debugger_idx;
    int32_t addr;
    uint8_t *target_state;
    status_$t status;
    uid_t *proc_uid;

    ML_$LOCK(XPD_LOCK_ID);

    /* Find the debugger slot */
    for (i = 1; i <= 6; i++) {
        addr = XPD_DATA_BASE + 0x10 + (i - 1) * 0x10 + DEBUGGER_ASID_OFFSET;

        if (*(int16_t *)addr == asid) {
            /* Found the slot - clear it */
            *(int16_t *)addr = 0;

            /* Now iterate through all 57 target processes and release them */
            for (j = 1; j <= 57; j++) {
                target_state = (uint8_t *)(XPD_DATA_BASE + 0x14 + (j - 1) * 0x14 + TARGET_STATE_OFFSET);

                /* Check if this target is being debugged by us */
                debugger_idx = ((*target_state) & 0x0E) >> 1;

                if (debugger_idx == i) {
                    /* Clear the debugger bits (bits 1-3) */
                    *target_state = (*target_state) & 0xF1;

                    /* If target is suspended by debugger, continue it */
                    if ((*(uint16_t *)target_state & 0x1E0) != 0) {
                        proc_uid = (uid_t *)(PROC2_UID_BASE + j * 8);
                        XPD_$CONTINUE_PROC(proc_uid, 0, &status);
                    }
                }
            }

            ML_$UNLOCK(XPD_LOCK_ID);
            *status_ret = status_$ok;
            return;
        }
    }

    ML_$UNLOCK(XPD_LOCK_ID);
    *status_ret = status_$xpd_not_a_debugger;
}

/*
 * XPD_$SET_DEBUGGER - Set up debugger/target relationship
 *
 * This is the main entry point for establishing or removing a
 * debugging relationship between two processes.
 *
 * Cases:
 * 1. Both NIL: No operation
 * 2. Target NIL, debugger set: Register/unregister self as debugger
 * 3. Debugger NIL, target set: Remove debugger from target
 * 4. Both set, same UID: Self-debug setup (special case)
 * 5. Both set, different: Set debugger on target
 */
void XPD_$SET_DEBUGGER(uid_t *debugger_uid, uid_t *target_uid, status_$t *status_ret)
{
    uid_t local_debugger;
    uid_t local_target;
    int16_t debugger_asid;
    int16_t target_asid;
    int16_t debugger_idx;
    int32_t target_offset;
    status_$t status;
    uint8_t *target_state;

    /* Copy UIDs to locals */
    local_debugger = *debugger_uid;
    local_target = *target_uid;

    /* Case: Target is NIL */
    if (local_target.high == UID_$NIL.high && local_target.low == UID_$NIL.low) {
        /* Debugger is NIL too - nothing to do */
        if (local_debugger.high == UID_$NIL.high && local_debugger.low == UID_$NIL.low) {
            *status_ret = status_$ok;
            return;
        }

        /* Target is NIL, debugger is set - register/unregister as debugger */
        debugger_asid = PROC2_$FIND_ASID(&local_debugger, NULL, status_ret);
        if (*status_ret != status_$ok) {
            return;
        }

        /* Unregister as debugger (clears slot and releases all targets) */
        XPD_$UNREGISTER_DEBUGGER(debugger_asid, status_ret);
        return;
    }

    /* Target is not NIL - find its ASID */
    target_asid = PROC2_$FIND_ASID(&local_target, NULL, &status);
    if (status != status_$ok) {
        *status_ret = status;
        return;
    }

    /* Case: Debugger is NIL - remove debugger from target */
    if (local_debugger.high == UID_$NIL.high && local_debugger.low == UID_$NIL.low) {
        target_offset = target_asid * 0x14;

        ML_$LOCK(XPD_LOCK_ID);

        target_state = (uint8_t *)(XPD_DATA_BASE + 0x10 + target_offset);

        /* Check if target has a debugger */
        if (((*target_state) & 0x0E) != 0) {
            /* Clear debugger bits */
            *target_state = (*target_state) & 0xF1;

            /* If target is suspended, continue it */
            if ((*(uint16_t *)target_state & 0x1E0) != 0) {
                ML_$UNLOCK(XPD_LOCK_ID);
                XPD_$CONTINUE_PROC(&local_target, 0, status_ret);
                goto done_ok;
            }
        }

        ML_$UNLOCK(XPD_LOCK_ID);
        goto done_ok;
    }

    /* Both debugger and target are specified */

    /* Find debugger's ASID */
    debugger_asid = PROC2_$FIND_ASID(&local_debugger, NULL, &status);
    if (status != status_$ok) {
        *status_ret = status_$xpd_debugger_not_found;
        return;
    }

    /* Check if self-debugging (debugger == target) */
    if (local_debugger.high == local_target.high && local_debugger.low == local_target.low) {
        /* Self-debug - special handling via FUN_00e74f7c */
        XPD_$UNREGISTER_DEBUGGER(debugger_asid, status_ret);
        return;
    }

    /* Register as debugger if not already */
    debugger_idx = XPD_$REGISTER_DEBUGGER(debugger_asid, status_ret);
    if (debugger_idx == 0) {
        return;  /* Error already set */
    }

    /* Set up the target */
    ML_$LOCK(XPD_LOCK_ID);

    target_offset = target_asid * 0x14;
    target_state = (uint8_t *)(XPD_DATA_BASE + 0x10 + target_offset);

    /* Check if target already has a different debugger */
    if (((*target_state) & 0x0E) != 0) {
        /* Clear old debugger */
        *target_state = (*target_state) & 0xF1;

        /* Set new debugger index */
        *target_state = (debugger_idx << 1) | (*target_state);

        ML_$UNLOCK(XPD_LOCK_ID);
        *status_ret = status_$xpd_illegal_target_setup;
        return;
    }

    /* Set debugger index on target (bits 1-3) */
    *target_state = (*target_state) & 0xF1;
    *target_state = (debugger_idx << 1) | (*target_state);

    ML_$UNLOCK(XPD_LOCK_ID);

done_ok:
    *status_ret = status_$ok;
}
