/*
 * XPD_$RESTART - Restart a suspended debug target
 *
 * This function resumes execution of a suspended debug target
 * with the specified mode (continue, single-step, etc.).
 *
 * Original address: 0x00e5b54a
 */

#include "xpd/xpd.h"

/* Process table offsets */
#define PROC_TABLE_BASE         0xEA551C
#define PROC_ENTRY_SIZE         0xE4

/* Current process index offset */
#define CURRENT_TO_INDEX_OFFSET 0xEA93D2

/* XPD data base */
#define XPD_DATA_BASE           0xEA5034

/* Process entry field offsets */
#define STATE_PTR_OFFSET        (-0x1E)     /* Pointer to saved state */
#define DEBUG_FLAGS_OFFSET      (-0xB9)     /* Debug flags byte */
#define EVENT_STATUS_OFFSET     (-0x22)     /* Event status */
#define EVENT_SIGNAL_OFFSET     (-0x50)     /* Event signal info */
#define PROC1_PID_OFFSET        (-0x4A)     /* PROC1 process ID */
#define DEBUGGER_IDX_OFFSET     (-0xBE)     /* Debugger index */

/* Debugger table offsets */
#define DEBUGGER_ASID_OFFSET    0x484

/* AS creation record base for EC access */
#define AS_CR_REC_BASE          0xE2B978

/*
 * XPD_$RESTART - Restart a suspended debug target
 *
 * Modes:
 *   0 - No-op (do nothing, don't resume)
 *   1 - Continue execution normally
 *   2 - Single step with trace enabled
 *   3 - Continue execution, clear single step
 *
 * Parameters:
 *   proc_uid   - UID of target process
 *   mode       - Restart mode pointer
 *   pc         - New PC value (or 1 to keep current)
 *   signal     - New signal value to deliver
 *   status_val - New status value (or 0 to compute from signal)
 *   status_ret - Status return
 */
void XPD_$RESTART(uid_t *proc_uid, uint16_t *mode, int32_t *pc,
                  int16_t *signal, int32_t *status_val, status_$t *status_ret)
{
    int16_t index;
    int32_t proc_offset;
    int32_t **state_ptr;
    int16_t current_signal;
    int16_t debugger_idx;
    int32_t target_offset;
    ec_$eventcount_t *ec;
    ec_$eventcount_t *ecs[3];
    int32_t wait_val;
    int16_t proc1_pid;
    uint8_t *flags_ptr;
    uint16_t flags;
    status_$t status;
    uid_t local_uid;

    local_uid = *proc_uid;

    /* Lock and find the target */
    ML_$LOCK(PROC2_LOCK_ID);
    index = XPD_$FIND_INDEX(&local_uid, &status);
    ML_$UNLOCK(PROC2_LOCK_ID);

    if (status != status_$ok) {
        *status_ret = status;
        return;
    }

    proc_offset = index * PROC_ENTRY_SIZE;

    /* Get pointer to saved state */
    state_ptr = (int32_t **)(PROC_TABLE_BASE + proc_offset + STATE_PTR_OFFSET);

    if (*state_ptr == NULL) {
        *status_ret = status_$xpd_state_unavailable_for_this_event;
        return;
    }

    /* Update PC if requested (not 1 = keep current) */
    if (*pc != 1) {
        int32_t *frame_ptr = (int32_t *)(*(*state_ptr));
        *(frame_ptr + 2) = *pc;  /* PC is at offset 2 in exception frame */
    }

    /* Get current signal from event info */
    current_signal = *(int16_t *)(PROC_TABLE_BASE + proc_offset + EVENT_SIGNAL_OFFSET);

    /* Update status and signal if different */
    if (current_signal != *signal || (current_signal == 0x13 && *status_val != 0)) {
        *(int32_t *)(PROC_TABLE_BASE + proc_offset + EVENT_STATUS_OFFSET) = *status_val;
        /* Set high bit to indicate status was modified */
        *(uint8_t *)(PROC_TABLE_BASE + proc_offset + EVENT_STATUS_OFFSET + 1) |= 0x80;
    }

    /* Update signal in event info */
    *(int16_t *)(PROC_TABLE_BASE + proc_offset + EVENT_SIGNAL_OFFSET) = *signal;

    /* Process restart mode */
    switch (*mode) {
    case 0:
        /* No-op - don't resume, just update state */
        goto check_async_debugger;

    case 1:
        /* Continue - clear trace flags */
        flags_ptr = (uint8_t *)(PROC_TABLE_BASE + proc_offset + DEBUG_FLAGS_OFFSET - 1);
        *(uint16_t *)flags_ptr &= 0xFFED;  /* Clear bits 1 and 4 (0x12) */
        break;

    case 2:
        /* Single step with trace - set trace flag */
        flags_ptr = (uint8_t *)(PROC_TABLE_BASE + proc_offset + DEBUG_FLAGS_OFFSET);
        *flags_ptr |= XPD_FLAG_TRACE_PENDING;
        /* Fall through to case 3 */

    case 3:
        /* Continue with single step - clear suspended flag */
        flags_ptr = (uint8_t *)(PROC_TABLE_BASE + proc_offset + DEBUG_FLAGS_OFFSET);
        *flags_ptr &= ~XPD_FLAG_SUSPENDED;
        break;

    default:
        goto check_async_debugger;
    }

    /* Resume the target process at PROC1 level */
    proc1_pid = *(int16_t *)(PROC_TABLE_BASE + proc_offset + PROC1_PID_OFFSET);
    PROC1_$RESUME(proc1_pid, &status);

check_async_debugger:
    /*
     * If this is an async debugger (uses eventcount notifications),
     * wait for the target to stop again.
     */
    flags_ptr = (uint8_t *)(PROC_TABLE_BASE + proc_offset + DEBUG_FLAGS_OFFSET - 1);
    if ((*(flags_ptr + 1) & 0x10) == 0) {
        /* Not async - done */
        *status_ret = status;
        return;
    }

    /* Get debugger index and compute EC address */
    debugger_idx = *(int16_t *)(PROC_TABLE_BASE + proc_offset + DEBUGGER_IDX_OFFSET);

    while (1) {
        /* Calculate EC address for this debugger's notification EC */
        target_offset = debugger_idx * 24;  /* 0x18 bytes per entry */
        ec = (ec_$eventcount_t *)(AS_CR_REC_BASE + target_offset - 0x0C);

        /* Read current value and compute wait value */
        wait_val = EC_$READ(ec) + 1;

        /* Check if target is suspended again or exited */
        flags_ptr = (uint8_t *)(PROC_TABLE_BASE + proc_offset + DEBUG_FLAGS_OFFSET);
        if ((*flags_ptr & XPD_FLAG_SUSPENDED) != 0 || (int8_t)*flags_ptr >= 0) {
            break;
        }

        /* Wait for notification */
        ecs[0] = ec;
        EC_$WAITN(ecs, &wait_val, 1);

        /* Get updated debugger index */
        debugger_idx = *(int16_t *)(PROC_TABLE_BASE + proc_offset + DEBUGGER_IDX_OFFSET);
    }

    /* Check final state */
    flags_ptr = (uint8_t *)(PROC_TABLE_BASE + proc_offset + DEBUG_FLAGS_OFFSET);

    if ((int8_t)*flags_ptr < 0) {
        /* Target is still in debug mode - set state_saved flag and return event info */
        *flags_ptr |= XPD_FLAG_STATE_SAVED;

        status = *(status_$t *)(PROC_TABLE_BASE + proc_offset + EVENT_STATUS_OFFSET);
        if (status == status_$ok) {
            /* Compute status from signal */
            status = (*(int16_t *)(PROC_TABLE_BASE + proc_offset + EVENT_SIGNAL_OFFSET)) + 0x9010000;
        }
    } else {
        /* Target exited */
        status = status_$proc2_uid_not_found;
    }

    *status_ret = status;
}
