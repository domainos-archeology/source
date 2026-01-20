/*
 * XPD Register Access Functions
 *
 * These functions provide access to target process registers
 * including general-purpose, exception frame, floating-point,
 * and debug state information.
 *
 * Original addresses:
 *   XPD_$GET_REGISTERS: 0x00e5c1a4
 *   XPD_$PUT_REGISTERS: 0x00e5c33c
 *   XPD_$GET_FP:        0x00e5bffc
 *   XPD_$PUT_FP:        0x00e5c094
 *   XPD_$GET_FP_INT:    0x00e5c55a
 *   XPD_$PUT_FP_INT:    0x00e5c58e
 *   XPD_$FP_GET_STATE:  0x00e5c50e
 *   XPD_$FP_PUT_STATE:  0x00e5c4d0
 *   XPD_$GET_TARGET_INFO: 0x00e5c12c
 */

#include "xpd/xpd.h"
#include "fim/fim.h"
#include "fp/fp.h"
#include "peb/peb.h"

/* Process table offsets */
#define PROC_TABLE_BASE     0xEA551C
#define PROC_ENTRY_SIZE     0xE4

/* Current process index offset */
#define CURRENT_TO_INDEX_OFFSET 0xEA93D2

/* XPD data base */
#define XPD_DATA_BASE       0xEA5034

/* Process entry field offsets */
#define STATE_PTR_OFFSET    (-0x1E)     /* Pointer to saved state */
#define DEBUG_FLAGS_OFFSET  (-0xB9)     /* Debug flags byte */
#define EVENT_STATUS_OFFSET (-0x22)     /* Event status */
#define EVENT_SIGNAL_OFFSET (-0x50)     /* Event signal info */
#define FAULT_MASK_OFFSET   (-0x64)     /* Fault mask */
#define LAST_PC_OFFSET      (-0x1A)     /* Last traced PC */

/* Debugger table offsets */
#define DEBUGGER_ASID_OFFSET 0x484

/* FPU detection flags */
extern char DAT_00e24c98;  /* MC68881/68882 presence flag */
extern char DAT_00e24c92;  /* Peripheral board FPU flag */

/*
 * XPD_$GET_REGISTERS - Get target process registers
 *
 * Retrieves register state from a suspended debug target.
 * The mode parameter selects which register set to retrieve:
 *   0 = General registers (D0-D7, A0-A7)
 *   1 = Exception frame
 *   2 = Floating-point state
 *   3 = Debug state info
 */
void XPD_$GET_REGISTERS(uid_t *proc_uid, int16_t *mode, void *regs, status_$t *status_ret)
{
    int16_t index;
    int32_t proc_offset;
    int32_t **state_ptr;
    int32_t *reg_array;
    uint16_t *sr_ptr;
    uint32_t *exc_frame;
    uint32_t exc_size;
    status_$t status;
    uid_t local_uid;
    int16_t i;
    uint32_t *out = (uint32_t *)regs;

    local_uid = *proc_uid;

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

    switch (*mode) {
    case XPD_REG_MODE_GENERAL:
        /*
         * General registers: 16 longwords (D0-D7, A0-A7)
         * Stored at state_ptr[1]
         */
        reg_array = (int32_t *)(*state_ptr)[1];
        for (i = 0; i < 16; i++) {
            out[i] = reg_array[i];
        }
        break;

    case XPD_REG_MODE_EXCEPTION:
        /*
         * Exception frame info
         * out[0] = frame size
         * out[1] = SR (status register)
         * out[2] = PC (program counter)
         * out[3+] = additional frame data
         */
        exc_frame = (uint32_t *)(*state_ptr)[3];
        out[0] = 0x0C;  /* Default size */
        sr_ptr = (uint16_t *)(*state_ptr)[0];
        out[1] = (uint32_t)*sr_ptr;
        out[2] = *(int32_t *)(sr_ptr + 1);

        /* Check if extended frame (size > 4 bytes) */
        if (*exc_frame > 4) {
            out[0] = *exc_frame + 0x10;
            out[3] = 0;
            out[4] = *exc_frame;

            /* Copy additional frame words */
            exc_size = *exc_frame - 4;
            if ((int32_t)exc_size < 0) {
                exc_size = *exc_frame - 1;
            }
            exc_size = exc_size >> 2;

            for (i = 0; i < (int16_t)exc_size; i++) {
                out[3] += exc_frame[i + 1];  /* Accumulate checksum */
                out[5 + i] = exc_frame[i + 1];
            }
        }
        break;

    case XPD_REG_MODE_FP_STATE:
        /*
         * Floating-point state
         * Stored at state_ptr[2]
         */
        reg_array = (int32_t *)(*state_ptr)[2];
        out[0] = reg_array[0];  /* FP state size */

        exc_size = reg_array[0] - 4;
        if ((int32_t)exc_size < 0) {
            exc_size = reg_array[0] - 1;
        }
        exc_size = exc_size >> 2;

        for (i = 0; i < (int16_t)exc_size; i++) {
            out[i + 1] = reg_array[i + 1];
        }
        break;

    case XPD_REG_MODE_DEBUG_STATE:
        /*
         * Debug state info
         * out[0] = event status
         * out[1] = signal number
         * out[2] = faulting PC (if single-step)
         * out[3] = fault mask
         */
        out[0] = *(int32_t *)(PROC_TABLE_BASE + proc_offset + EVENT_STATUS_OFFSET);
        out[0] &= 0x7FFFFFFF;  /* Clear high bit */

        out[1] = *(int16_t *)(PROC_TABLE_BASE + proc_offset + EVENT_SIGNAL_OFFSET) & 0xFF;

        if (out[0] == status_$fault_single_step_completed) {
            out[2] = *(int32_t *)(PROC_TABLE_BASE + proc_offset + LAST_PC_OFFSET);
        } else {
            out[2] = 0;
        }

        out[3] = *(int32_t *)(PROC_TABLE_BASE + proc_offset + FAULT_MASK_OFFSET);
        break;

    default:
        status = status_$xpd_invalid_option;
        break;
    }

    *status_ret = status;
}

/*
 * XPD_$PUT_REGISTERS - Set target process registers
 *
 * Sets register state in a suspended debug target.
 */
void XPD_$PUT_REGISTERS(uid_t *proc_uid, int16_t *mode, void *regs, status_$t *status_ret)
{
    int16_t index;
    int32_t proc_offset;
    int32_t **state_ptr;
    int32_t *reg_array;
    uint16_t *sr_ptr;
    uint32_t *exc_frame;
    uint32_t frame_size;
    uint32_t checksum;
    status_$t status;
    uid_t local_uid;
    int16_t i;
    int16_t word_count;
    uint32_t *in = (uint32_t *)regs;

    local_uid = *proc_uid;

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

    switch (*mode) {
    case XPD_REG_MODE_GENERAL:
        /*
         * General registers: copy 16 longwords
         */
        reg_array = (int32_t *)(*state_ptr)[1];
        for (i = 0; i < 16; i++) {
            reg_array[i] = in[i];
        }
        break;

    case XPD_REG_MODE_EXCEPTION:
        /*
         * Exception frame
         * in[0] = total size
         * in[1..2] = SR, PC (at byte offsets 6, 8)
         * in[3] = checksum
         * in[4] = frame size
         * in[5+] = frame data
         */
        if (in[0] < 0x0D) {
            /* Small frame - just clear it */
            exc_frame = (uint32_t *)(*state_ptr)[3];
            *exc_frame = 0;
            ((uint8_t *)(state_ptr + 4))[0] = 0xFF;  /* Mark as modified */
        } else {
            /* Validate checksum */
            frame_size = in[4] - 4;
            if ((int32_t)frame_size < 0) {
                frame_size = in[4] - 1;
            }
            word_count = frame_size >> 2;

            checksum = 0;
            for (i = 0; i < word_count; i++) {
                checksum += in[5 + i];
            }

            if (checksum != in[3] || word_count > 0xD4) {
                *status_ret = status_$xpd_invalid_state_argument;
                return;
            }

            /* Copy frame data */
            exc_frame = (uint32_t *)(*state_ptr)[3];
            for (i = 0; i < word_count; i++) {
                exc_frame[i + 1] = in[5 + i];
            }
            *exc_frame = in[4];
            ((uint8_t *)(state_ptr + 4))[0] = 0xFF;
        }

        /* Update SR and PC */
        sr_ptr = (uint16_t *)(*state_ptr)[0];
        *(uint32_t *)sr_ptr = *(uint32_t *)((char *)in + 6);
        *(uint16_t *)(sr_ptr + 2) = *(uint16_t *)((char *)in + 10);
        break;

    case XPD_REG_MODE_FP_STATE:
        /*
         * Floating-point state
         */
        exc_frame = (uint32_t *)(*state_ptr)[2];
        *exc_frame = in[0];

        frame_size = in[0] - 4;
        if ((int32_t)frame_size < 0) {
            frame_size = in[0] - 1;
        }
        word_count = frame_size >> 2;

        if (word_count > 0x6C) {
            *status_ret = status_$xpd_invalid_state_argument;
            return;
        }

        for (i = 0; i < word_count; i++) {
            exc_frame[i + 1] = in[i + 1];
        }
        ((uint8_t *)(state_ptr + 4))[0] = 0xFF;
        break;

    default:
        status = status_$xpd_invalid_option;
        break;
    }

    *status_ret = status;
}

/*
 * XPD_$FP_GET_STATE - Get floating-point state
 *
 * Saves the current FPU state to a buffer. Handles both
 * MC68881/68882 and peripheral board FPUs.
 */
void XPD_$FP_GET_STATE(void *fp_buf, void *fp_format)
{
    uint32_t *buf = (uint32_t *)fp_buf;
    uint32_t *fmt = (uint32_t *)fp_format;

    *fmt = 0;

    if (DAT_00e24c98 < 0) {
        /* MC68881/68882 present */
        FIM_$FP_GET_STATE(fp_buf, fp_format);
    } else if (DAT_00e24c92 < 0) {
        /* Peripheral board FPU */
        PEB_$UNLOAD_REGS((uint32_t *)fp_buf + 1);
        *buf = 0x20;
        *fmt = 4;
    }
}

/*
 * XPD_$FP_PUT_STATE - Set floating-point state
 *
 * Restores FPU state from a buffer.
 */
void XPD_$FP_PUT_STATE(void *fp_buf, void *fp_format)
{
    if (DAT_00e24c98 < 0) {
        /* MC68881/68882 present */
        FIM_$FP_PUT_STATE(fp_buf, fp_format);
    } else if (DAT_00e24c92 < 0) {
        /* Peripheral board FPU */
        PEB_$LOAD_REGS((uint32_t *)fp_buf + 1);
    }
}

/*
 * XPD_$GET_FP_INT - Get FP registers for target (internal)
 */
void XPD_$GET_FP_INT(int16_t *asid, status_$t *status_ret)
{
    *status_ret = status_$ok;

    if (DAT_00e24c98 < 0) {
        FP_$GET_FP(*asid);
    } else {
        PEB_$GET_FP(asid);
    }
}

/*
 * XPD_$PUT_FP_INT - Set FP registers for target (internal)
 */
void XPD_$PUT_FP_INT(int16_t *asid, status_$t *status_ret)
{
    *status_ret = status_$ok;

    if (DAT_00e24c98 < 0) {
        FP_$PUT_FP(*asid);
    } else {
        PEB_$PUT_FP(asid);
    }
}

/*
 * XPD_$GET_FP - Get floating-point registers for target
 *
 * High-level interface to get FP state for a debug target.
 */
void XPD_$GET_FP(uid_t *proc_uid, status_$t *status_ret)
{
    int16_t asid;
    int32_t proc_offset;
    int32_t target_offset;
    uint8_t flags;
    int16_t debugger_idx;
    int16_t debugger_asid;
    status_$t status;

    asid = PROC2_$FIND_ASID(proc_uid, NULL, status_ret);
    if (asid == 0) {
        return;
    }

    proc_offset = asid * 0x14;
    target_offset = XPD_DATA_BASE + 0x10 + proc_offset;

    /* Get debugger index for this target */
    flags = *(uint8_t *)(target_offset + 0x10);
    debugger_idx = (flags & 0x0E) >> 1;

    /* Get debugger's ASID */
    debugger_asid = *(int16_t *)(XPD_DATA_BASE + 0x10 + (debugger_idx - 1) * 0x10 + DEBUGGER_ASID_OFFSET);

    /* Verify debugger and that target is suspended */
    if (debugger_asid != PROC1_$AS_ID ||
        (*(uint16_t *)(target_offset + 0x10) & 0x1E0) == 0 ||
        *(int16_t *)(target_offset + 0x10) >= 0 ||
        debugger_idx == 0) {
        *status_ret = status_$xpd_target_not_suspended;
        return;
    }

    XPD_$GET_FP_INT(&asid, status_ret);
}

/*
 * XPD_$PUT_FP - Set floating-point registers for target
 *
 * High-level interface to set FP state for a debug target.
 */
void XPD_$PUT_FP(uid_t *proc_uid, status_$t *status_ret)
{
    int16_t asid;
    int32_t proc_offset;
    int32_t target_offset;
    uint8_t flags;
    int16_t debugger_idx;
    int16_t debugger_asid;
    status_$t status;

    asid = PROC2_$FIND_ASID(proc_uid, NULL, status_ret);
    if (asid == 0) {
        return;
    }

    proc_offset = asid * 0x14;
    target_offset = XPD_DATA_BASE + 0x10 + proc_offset;

    /* Get debugger index for this target */
    flags = *(uint8_t *)(target_offset + 0x10);
    debugger_idx = (flags & 0x0E) >> 1;

    /* Get debugger's ASID */
    debugger_asid = *(int16_t *)(XPD_DATA_BASE + 0x10 + (debugger_idx - 1) * 0x10 + DEBUGGER_ASID_OFFSET);

    /* Verify debugger and that target is suspended */
    if (debugger_asid != PROC1_$AS_ID ||
        (*(uint16_t *)(target_offset + 0x10) & 0x1E0) == 0 ||
        *(int16_t *)(target_offset + 0x10) >= 0 ||
        debugger_idx == 0) {
        *status_ret = status_$xpd_target_not_suspended;
        return;
    }

    XPD_$PUT_FP_INT(&asid, status_ret);
}

/*
 * XPD_$GET_TARGET_INFO - Get target debug info
 *
 * Returns flags indicating whether a process is a valid debug target
 * and whether it is currently suspended.
 */
void XPD_$GET_TARGET_INFO(uid_t *proc_uid, int8_t *is_target, int8_t *is_suspended,
                          status_$t *status_ret)
{
    int16_t asid;
    int32_t target_offset;
    uint16_t flags;
    uint8_t target_flag;
    uint8_t suspended_flag;

    asid = PROC2_$FIND_ASID(proc_uid, NULL, status_ret);
    if (asid == 0) {
        return;
    }

    target_offset = XPD_DATA_BASE + 0x10 + asid * 0x14 + 0x10;
    flags = *(uint16_t *)target_offset;

    /* Check if this is a valid target (has debugger and is in debug mode) */
    target_flag = 0;
    if ((flags & 0x0E) != 0 && (int16_t)flags < 0) {
        target_flag = (int8_t)-1;  /* 0xFF = is target */
    }

    *is_target = target_flag;

    /* Check if target is suspended */
    if (target_flag < 0) {
        if ((flags & 0x1E0) != 0) {
            *is_suspended = (int8_t)-1;  /* 0xFF = is suspended */
        } else {
            *is_suspended = 0;
        }
    } else {
        *is_suspended = 0;
    }
}
