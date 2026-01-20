/*
 * XPD Event Handling Functions
 *
 * These functions manage debug events, eventcounts, and process
 * continuation for the XPD debugging subsystem.
 *
 * Original addresses:
 *   XPD_$CAPTURE_FAULT:        0x00e5b1ee
 *   XPD_$GET_EVENT_AND_DATA:   0x00e5be28
 *   XPD_$GET_EC:               0x00e5bdc2
 *   XPD_$CONTINUE_PROC:        0x00e5bed8
 *   XPD_$SET_ENABLE:           0x00e5bf50
 */

#include "xpd/xpd.h"
#include "proc1/proc1.h"
#include "proc2/proc2.h"
#include "ec/ec.h"
#include "ec2/ec2.h"
#include "fim/fim.h"
#include "ml/ml.h"

/* Process table offsets */
#define PROC_TABLE_BASE         0xEA551C
#define PROC_ENTRY_SIZE         0xE4

/* Current process index offset */
#define CURRENT_TO_INDEX_OFFSET 0xEA93D2

/* XPD data base */
#define XPD_DATA_BASE           0xEA5034

/* Target state offsets */
#define TARGET_STATE_BASE       0xEA5044     /* First target state entry */
#define TARGET_STATE_SIZE       0x14         /* Size per target entry */

/* Process entry field offsets (negative from entry base) */
#define EVENT_STATUS_OFFSET     (-0x22)      /* Event status value */
#define EVENT_SIGNAL_OFFSET     (-0x50)      /* Event signal info */
#define PROC1_PID_OFFSET        (-0x4A)      /* PROC1 process ID */
#define TRACE_ASID_OFFSET       (-0x4E)      /* Trace ASID */
#define DEBUG_FLAGS_OFFSET      (-0xB9)      /* Debug flags byte */
#define DEBUGGER_IDX_OFFSET     (-0xBE)      /* Debugger index */
#define LAST_PC_OFFSET          (-0x1A)      /* Last stopped PC */
#define STATE_PTR_OFFSET        (-0x1E)      /* Saved state pointer */
#define PTRACE_FLAGS_OFFSET     (-0x0A)      /* Ptrace flags */
#define SIGNAL_MASK_OFFSET      (-0x16)      /* Signal mask */
#define TRACE_RANGE_LO_OFFSET   (-0x12)      /* Trace range low */
#define TRACE_RANGE_HI_OFFSET   (-0x0E)      /* Trace range high */
#define PENDING_SIGNALS_OFFSET  (-0x70)      /* Pending signals mask */
#define DELIVERED_SIGNALS_OFFSET (-0x6C)     /* Delivered signals mask */
#define BLOCKED_SIGNALS_OFFSET  (-0x64)      /* Blocked signals mask */

/* PROC2 UID array */
#define PROC2_UID_BASE          0xE7BE94

/* UID_$NIL address */
#define UID_NIL_ADDR            0xE1737C

/* FIM trace status */
#define FIM_TRACE_STS_BASE      0xE223A2
#define FIM_QUIT_INH_BASE       0xE2248A

/* AS creation record for EC addresses */
#define AS_CR_REC_BASE          0xE2B978

/* Event types encoded in target state (bits 5-8 = event code) */
#define EVENT_CODE_SHIFT        5
#define EVENT_CODE_MASK         0x1E0

/* Target state flag bits */
#define TARGET_FLAG_ENABLED     0x80    /* Bit 7: debugging enabled */
#define TARGET_FLAG_PROCESSED   0x40    /* Bit 6: event was processed */
#define TARGET_DEBUGGER_MASK    0x0E    /* Bits 1-3: debugger index << 1 */

/*
 * Internal helper to check if fork/exec/invoke options allow capture
 * Returns negative if capture should proceed, non-negative to skip
 */
static int8_t XPD_$CHECK_FORK_EXEC_OPTS(int32_t proc_offset)
{
    uint8_t *flags = (uint8_t *)(proc_offset + PROC_TABLE_BASE + PTRACE_FLAGS_OFFSET);

    /* Check if trace following is enabled (bit for fork/exec tracing) */
    /* Original checks bit patterns in ptrace options */
    return (*flags & 0x80) ? -1 : 0;
}

/*
 * XPD_$CAPTURE_FAULT - Capture a fault/event in a debug target
 *
 * This is called when a debug event occurs (fault, signal, fork, exec, etc.)
 * to notify the debugger and suspend the target until it's handled.
 *
 * Parameters:
 *   saved_state  - Pointer to saved processor state
 *   signal       - Signal/event type (modified on return)
 *   status_ret   - Input: event status, Output: debugger response status
 */
void XPD_$CAPTURE_FAULT(int32_t **saved_state, int16_t *signal, status_$t *status_ret)
{
    int16_t index;
    int32_t proc_offset;
    int16_t event_type;
    status_$t input_status;
    uint8_t *flags_ptr;
    int32_t pc_value;
    uint8_t fp_state[256];
    uint8_t fp_regs[240];
    int32_t *local_state;
    int32_t local_saved;
    int8_t fp_modified;
    int16_t proc1_pid;
    int16_t debugger_idx;
    ec_$eventcount_t *ec;

    /* Copy parameters to locals */
    local_saved = (int32_t)*saved_state;
    local_state = (int32_t *)local_saved;
    fp_modified = 0;
    input_status = *status_ret;

    /* Get current process offset */
    index = *(int16_t *)(CURRENT_TO_INDEX_OFFSET + (PROC1_$CURRENT * 2));
    proc_offset = index * PROC_ENTRY_SIZE;
    flags_ptr = (uint8_t *)(PROC_TABLE_BASE + proc_offset + PTRACE_FLAGS_OFFSET);

    /* Dispatch based on event type */
    switch (input_status) {
    case status_$xpd_target_is_forking:
        /* Fork - check if fork tracing is enabled (bit 1) */
        event_type = 1;
        if ((*flags_ptr & 0x02) == 0) {
            goto skip_capture;
        }
        *signal = 0;
        break;

    case 0x160017:  /* Additional fork-related status */
        event_type = 1;
        if ((*flags_ptr & (1 << event_type)) == 0) {
            goto skip_capture;
        }
        *signal = 1;
        goto do_capture;

    case status_$xpd_target_is_execing:
        /* Exec - check options and call helper */
        event_type = 2;
        *signal = 0;
        if (XPD_$CHECK_FORK_EXEC_OPTS(proc_offset) >= 0) {
            goto skip_capture;
        }
        goto do_capture;

    case status_$xpd_target_is_invoking:
        /* Invoke - similar to exec */
        event_type = 2;
        *signal = 1;
        if (XPD_$CHECK_FORK_EXEC_OPTS(proc_offset) >= 0) {
            goto skip_capture;
        }
        goto do_capture;

    case status_$xpd_target_is_exiting:
        /* Exit - check if exit tracing is enabled (bit 4) */
        event_type = 4;
        if ((*flags_ptr & (1 << event_type)) == 0) {
            goto skip_capture;
        }
        *signal = 0;
        goto do_capture;

    case status_$xpd_target_is_loading_exec_image:
        /* Load exec image - check bit 5 */
        event_type = 5;
        if ((*flags_ptr & (1 << event_type)) == 0) {
            goto skip_capture;
        }
        *signal = 0;
        goto do_capture;

    case 0x160019:  /* Additional load-related status */
        event_type = 5;
        if ((*flags_ptr & (1 << event_type)) == 0) {
            goto skip_capture;
        }
        *signal = 1;
        goto do_capture;

    case status_$fault_single_step_completed:
        /* Single step completed - check trace range */
        pc_value = *(int32_t *)(local_saved + 2);  /* PC from exception frame */

        if ((*flags_ptr & 0x40) != 0) {
            /* Trace range exclusion mode */
            if (pc_value >= *(int32_t *)(PROC_TABLE_BASE + proc_offset + TRACE_RANGE_LO_OFFSET) &&
                pc_value <= *(int32_t *)(PROC_TABLE_BASE + proc_offset + TRACE_RANGE_HI_OFFSET)) {
                /* Inside exclusion range - deliver trace fault and continue */
                goto deliver_trace_and_continue;
            }
            event_type = 6;
        } else {
            /* Trace range inclusion mode or no range */
            if ((int8_t)*flags_ptr < 0) {
                /* Debug enabled with range */
                if (pc_value < *(int32_t *)(PROC_TABLE_BASE + proc_offset + TRACE_RANGE_LO_OFFSET) ||
                    pc_value > *(int32_t *)(PROC_TABLE_BASE + proc_offset + TRACE_RANGE_HI_OFFSET)) {
                    /* Outside range - deliver and continue */
                    goto deliver_trace_and_continue;
                }
                event_type = 7;
            } else {
                /* Check signal mask */
                if ((*flags_ptr & 0x01) == 0) {
                    goto skip_capture;
                }
                if ((uint16_t)(*signal - 1) > 0x1F) {
                    goto skip_capture;
                }
                if ((*(uint32_t *)(PROC_TABLE_BASE + proc_offset + SIGNAL_MASK_OFFSET) &
                     (1 << ((*signal - 1) & 0x1F))) == 0) {
                    goto skip_capture;
                }
                event_type = 0;
            }
        }
        goto do_capture;

    case status_$fault_process_BLAST:
        /* Process blast - just return */
        return;

    default:
        /* Other faults/signals - check signal mask */
        if ((*flags_ptr & 0x01) == 0) {
            return;
        }
        if ((uint16_t)(*signal - 1) > 0x1F) {
            return;
        }
        if ((*(uint32_t *)(PROC_TABLE_BASE + proc_offset + SIGNAL_MASK_OFFSET) &
             (1 << ((*signal - 1) & 0x1F))) == 0) {
            return;
        }
        event_type = 0;
        goto do_capture;
    }

do_capture:
    /* Save FP state */
    XPD_$FP_GET_STATE((int16_t)(intptr_t)fp_state, (int16_t)(intptr_t)fp_regs);

    /* Store state pointer in process entry */
    *(int32_t **)(PROC_TABLE_BASE + proc_offset + STATE_PTR_OFFSET) = &local_state;

    /* Set state_saved flag */
    *(uint8_t *)(PROC_TABLE_BASE + proc_offset + DEBUG_FLAGS_OFFSET) |= XPD_FLAG_STATE_SAVED;

    /* Store event type and signal */
    *(uint16_t *)(PROC_TABLE_BASE + proc_offset + EVENT_SIGNAL_OFFSET) =
        (event_type << 8) | (*signal & 0xFF);

    /* Store event status */
    *(status_$t *)(PROC_TABLE_BASE + proc_offset + EVENT_STATUS_OFFSET) = input_status;

    /* Notify debugger - check if async or synchronous */
    flags_ptr = (uint8_t *)(PROC_TABLE_BASE + proc_offset + DEBUG_FLAGS_OFFSET - 1);

    if ((*(flags_ptr + 1) & 0x10) == 0) {
        /* Synchronous - awaken guardian */
        PROC2_$AWAKEN_GUARDIAN(PROC_TABLE_BASE + proc_offset - 0xC8);
    } else {
        /* Async - advance debugger's EC */
        debugger_idx = *(int16_t *)(PROC_TABLE_BASE + proc_offset + DEBUGGER_IDX_OFFSET);
        ec = (ec_$eventcount_t *)(AS_CR_REC_BASE + (debugger_idx * 0x18) - 0x0C);
        EC_$ADVANCE(ec);
    }

    /* Wait for debugger to handle the event */
    proc1_pid = *(int16_t *)(PROC_TABLE_BASE + proc_offset + PROC1_PID_OFFSET);

    while ((*(uint8_t *)(PROC_TABLE_BASE + proc_offset + DEBUG_FLAGS_OFFSET) &
            XPD_FLAG_STATE_SAVED) != 0) {
        int32_t wait_state[8];
        PROC1_$SUSPEND(proc1_pid, wait_state);
        ML_$UNLOCK(PROC2_LOCK_ID);
        ML_$LOCK(PROC2_LOCK_ID);
    }

    /* Clear state pointer */
    *(int32_t **)(PROC_TABLE_BASE + proc_offset + STATE_PTR_OFFSET) = NULL;

    /* Restore FP state if it was modified */
    if (fp_modified < 0) {
        XPD_$FP_PUT_STATE((int16_t)(intptr_t)fp_state, (int16_t)(intptr_t)fp_regs);
    }

    /* Return updated status and signal */
    *status_ret = *(status_$t *)(PROC_TABLE_BASE + proc_offset + EVENT_STATUS_OFFSET);
    *signal = *(uint16_t *)(PROC_TABLE_BASE + proc_offset + EVENT_SIGNAL_OFFSET) & 0xFF;

    /* Store PC for later reference */
    *(int32_t *)(PROC_TABLE_BASE + proc_offset + LAST_PC_OFFSET) = *(int32_t *)(local_saved + 2);

    /* Clear suspended flag */
    *(uint8_t *)(PROC_TABLE_BASE + proc_offset + DEBUG_FLAGS_OFFSET) &= ~0x20;

    /* Clear blocked signals mask for this signal */
    *(int32_t *)(PROC_TABLE_BASE + proc_offset + BLOCKED_SIGNALS_OFFSET) = 0;

    /* Clear trace fault */
    FIM_$CLEAR_TRACE_FAULT(*(int16_t *)(PROC_TABLE_BASE + proc_offset + TRACE_ASID_OFFSET));

    /* Check if trace should be re-delivered */
    if ((*(uint8_t *)(PROC_TABLE_BASE + proc_offset + DEBUG_FLAGS_OFFSET) & 0x02) != 0) {
        *(int32_t *)(FIM_TRACE_STS_BASE +
            (*(int16_t *)(PROC_TABLE_BASE + proc_offset + TRACE_ASID_OFFSET) << 2)) =
            status_$fault_single_step_completed;
        FIM_$DELIVER_TRACE_FAULT(*(int16_t *)(PROC_TABLE_BASE + proc_offset + TRACE_ASID_OFFSET));
    }

    /* Check signal delivery status */
    if (*signal != 0) {
        uint32_t sig_bit = (*signal - 1) & 0x1F;

        if ((*(uint32_t *)(PROC_TABLE_BASE + proc_offset + DELIVERED_SIGNALS_OFFSET) &
             (1 << sig_bit)) != 0) {
            /* Signal should be blocked */
            *(uint32_t *)(PROC_TABLE_BASE + proc_offset + BLOCKED_SIGNALS_OFFSET) |= (1 << sig_bit);
        } else if ((*(uint32_t *)(PROC_TABLE_BASE + proc_offset + PENDING_SIGNALS_OFFSET) &
                    (1 << sig_bit)) == 0) {
            /* Signal not pending - return normally */
            return;
        }
    }

skip_capture:
    /* Clear quit inhibit and signal */
    *(uint8_t *)(FIM_QUIT_INH_BASE + PROC1_$AS_ID) = 0;
    *signal = 0;
    return;

deliver_trace_and_continue:
    /* Set trace status and deliver fault */
    *(int32_t *)(FIM_TRACE_STS_BASE +
        (*(int16_t *)(PROC_TABLE_BASE + proc_offset + TRACE_ASID_OFFSET) << 2)) =
        status_$fault_single_step_completed;
    FIM_$DELIVER_TRACE_FAULT(*(int16_t *)(PROC_TABLE_BASE + proc_offset + TRACE_ASID_OFFSET));

    /* Store current PC */
    *(int32_t *)(PROC_TABLE_BASE + proc_offset + LAST_PC_OFFSET) = pc_value;

    goto skip_capture;
}

/*
 * XPD_$GET_EVENT_AND_DATA - Get pending event from a debug target
 *
 * Scans the target table looking for a target that:
 * 1. Is being debugged by the caller
 * 2. Has a pending event that hasn't been processed
 * 3. Is in debug mode (enabled)
 *
 * Returns the target's UID, event code, and status.
 */
void XPD_$GET_EVENT_AND_DATA(uid_t *proc_uid, uint16_t *event_code, status_$t *status_ret)
{
    int16_t debugger_idx;
    int16_t i;
    uint8_t *target_state;
    uint16_t state_word;
    uint16_t code;
    status_$t status;

    /* Find our debugger index */
    debugger_idx = XPD_$FIND_DEBUGGER_INDEX(PROC1_$AS_ID, &status);

    if (debugger_idx == 0) {
        /* Not a debugger - return NIL */
        goto not_found;
    }

    /* Scan all 57 target entries */
    target_state = (uint8_t *)(XPD_DATA_BASE + 0x14 + 0x10);  /* First target's state */

    for (i = 1; i <= 57; i++) {
        /* Check if this target is ours */
        if (((target_state[0] & TARGET_DEBUGGER_MASK) >> 1) != debugger_idx) {
            goto next_target;
        }

        /* Check if already processed (bit 6) */
        state_word = *(uint16_t *)target_state;
        if ((state_word & 0x4000) != 0) {
            goto next_target;
        }

        /* Check if enabled (bit 15 set = negative) */
        if ((int16_t)state_word >= 0) {
            goto next_target;
        }

        /* Check if there's a pending event (bits 5-8) */
        code = (state_word & EVENT_CODE_MASK) >> EVENT_CODE_SHIFT;
        if (code == 0) {
            goto next_target;
        }

        /* Found a target with pending event */
        /* Get UID from PROC2 UID array */
        proc_uid->high = *(int32_t *)(PROC2_UID_BASE + (i * 8));
        proc_uid->low = *(int32_t *)(PROC2_UID_BASE + (i * 8) + 4);

        *event_code = code;

        /* Get status from target entry */
        *status_ret = *(status_$t *)(target_state - 0x10 + 0x0C);

        /* Mark as processed */
        target_state[0] |= 0x40;

        return;

    next_target:
        target_state += TARGET_STATE_SIZE;
    }

not_found:
    *event_code = 0;
    proc_uid->high = UID_$NIL.high;
    proc_uid->low = UID_$NIL.low;
    *status_ret = status_$ok;
}

/*
 * XPD_$GET_EC - Get eventcount for debugger notifications
 *
 * Returns a registered eventcount that can be used to wait for
 * events from debug targets. Only valid key is 0.
 */
void XPD_$GET_EC(int16_t *key, ec_$eventcount_t **ec_ret, status_$t *status_ret)
{
    int16_t debugger_idx;
    int32_t ec_addr;

    /* Find our debugger index */
    debugger_idx = XPD_$FIND_DEBUGGER_INDEX(PROC1_$AS_ID, status_ret);

    if (debugger_idx == 0) {
        return;  /* Not a debugger - status already set */
    }

    if (*key != 0) {
        *status_ret = status_$xpd_invalid_ec_key;
        return;
    }

    /* Calculate EC address for this debugger slot */
    ec_addr = XPD_DATA_BASE + (debugger_idx * 0x10) + 0x478;

    /* Register with EC2 to get user-space accessible EC */
    *ec_ret = EC2_$REGISTER_EC1((ec_$eventcount_t *)ec_addr, status_ret);

    if (*status_ret != status_$ok) {
        /* Set high bit to indicate registration issue */
        *(uint8_t *)status_ret |= 0x80;
    }
}

/*
 * XPD_$CONTINUE_PROC - Continue a suspended debug target
 *
 * Resumes a target that was suspended due to a debug event.
 * The response parameter controls how the target continues.
 */
void XPD_$CONTINUE_PROC(uid_t *proc_uid, xpd_$response_t *response, status_$t *status_ret)
{
    int16_t asid;
    int32_t target_offset;
    uint16_t *target_state;
    uint16_t state_word;

    /* Find target's ASID */
    asid = PROC2_$FIND_ASID(proc_uid, NULL, status_ret);

    if (asid == 0) {
        return;  /* Not found - status already set */
    }

    /* Calculate target state offset */
    target_offset = asid * TARGET_STATE_SIZE;
    target_state = (uint16_t *)(TARGET_STATE_BASE + target_offset);

    /* Check if target is actually suspended */
    state_word = *target_state;
    if ((state_word & EVENT_CODE_MASK) == 0) {
        *status_ret = status_$xpd_target_not_suspended;
        return;
    }

    /* Clear current response bits (4-5) and set new response */
    *(uint8_t *)target_state &= 0xCF;  /* Clear bits 4-5 */
    *(uint8_t *)target_state |= (((uint8_t *)response)[1] << 4);  /* Set response bits */

    /* Clear event code (bits 5-8) */
    *target_state &= ~EVENT_CODE_MASK;

    /* Advance target's EC to wake it up */
    EC_$ADVANCE((ec_$eventcount_t *)(XPD_DATA_BASE + target_offset));
}

/*
 * XPD_$SET_ENABLE - Enable or disable debug events for a target
 *
 * When disabled, the target will not generate debug events.
 * When enabled, if there are pending events the target is continued.
 */
void XPD_$SET_ENABLE(uid_t *proc_uid, uint8_t *enable_flag, status_$t *status_ret)
{
    int16_t asid;
    int32_t target_offset;
    uint8_t *target_state;
    uint16_t state_word;

    /* Find target's ASID */
    asid = PROC2_$FIND_ASID(proc_uid, NULL, status_ret);

    ML_$LOCK(XPD_LOCK_ID);

    if (asid == 0) {
        goto done;  /* Not found - status already set */
    }

    /* Calculate target state offset */
    target_offset = asid * TARGET_STATE_SIZE;
    target_state = (uint8_t *)(TARGET_STATE_BASE + target_offset);

    /* Clear current enable bit and set new value */
    target_state[0] &= 0x7F;  /* Clear bit 7 */
    target_state[0] |= (*enable_flag & 0x80);  /* Set if requested */

    if ((int8_t)*enable_flag < 0) {
        /* Disabling - clear event code */
        *(uint16_t *)target_state &= ~EVENT_CODE_MASK;
    } else {
        /* Enabling - check if there are pending events */
        state_word = *(uint16_t *)target_state;
        if ((state_word & EVENT_CODE_MASK) != 0) {
            /* Continue the process */
            uid_t *target_uid = (uid_t *)(PROC2_UID_BASE + (asid << 3));
            xpd_$response_t dummy_response = {0};
            XPD_$CONTINUE_PROC(target_uid, &dummy_response, status_ret);
        }
    }

done:
    ML_$UNLOCK(XPD_LOCK_ID);
}
