/*
 * PROC2_$DELIVER_FIM - Deliver Fault Interrupt Message
 *
 * Delivers a fault/signal to the current process. Handles the translation
 * from hardware faults to software signals and manages the signal masks
 * and delivery state.
 *
 * FIM = Fault Interrupt Message (Domain/OS terminology for exceptions)
 *
 * Parameters:
 *   signal_ret  - Returns the signal number being delivered
 *   status      - Status/fault info (high byte bit 7 used as loop flag)
 *   handler_addr_ret - Returns handler address (for certain signals)
 *   fault_param1 - Passed to XPD_$CAPTURE_FAULT
 *   fault_param2 - Passed to XPD_$CAPTURE_FAULT
 *   mask_ret    - Returns signal mask or handler address
 *   flag_ret    - Returns flag byte
 *
 * Returns:
 *   0xFF (-1) on success, 0 on failure/no signal
 *
 * Original address: 0x00e3edc0
 */

#include "proc2/proc2_internal.h"

/*
 * Signal masks for special handling
 * 0x3D9DFFFF - signals that bypass certain checks
 * 0xFFFFFF67 - signals that need special fault handling
 */
#define SIGNAL_BYPASS_MASK    0x3D9DFFFF
#define SIGNAL_FAULT_MASK     0xFFFFFF67

/*
 * Raw memory access macros for FIM-related fields
 */
#if defined(M68K)
    #define P2_FIM_BASE(idx)           ((uint8_t*)(0xEA551C + ((idx) * 0xE4)))

    /* Offset 0x4C - pending signals */
    #define P2_FIM_PENDING(idx)        (*(uint32_t*)(0xEA54A8 + (idx) * 0xE4))

    /* Offset 0x58 - blocked signals 2 */
    #define P2_FIM_BLOCKED2(idx)       (*(uint32_t*)(0xEA54B8 + (idx) * 0xE4))

    /* Offset 0x50 - signal mask field 1 */
    #define P2_FIM_MASK1(idx)          (*(uint32_t*)(0xEA54AC + (idx) * 0xE4))

    /* Offset 0x44 - signal mask field 2 (handler storage) */
    #define P2_FIM_MASK2(idx)          (*(uint32_t*)(0xEA54B0 + (idx) * 0xE4))

    /* Offset 0x54 - blocked signals 1 */
    #define P2_FIM_BLOCKED1(idx)       (*(uint32_t*)(0xEA54B4 + (idx) * 0xE4))

    /* Offset 0x68 - stored status */
    #define P2_FIM_STATUS(idx)         (*(uint32_t*)(0xEA54C8 + (idx) * 0xE4))

    /* Offset 0x2A - flags */
    #define P2_FIM_FLAGS(idx)          (*(uint16_t*)(0xEA5462 + (idx) * 0xE4))

    /* Offset 0x5E - debug/XPD field */
    #define P2_FIM_XPD(idx)            (*(int16_t*)(0xEA545E + (idx) * 0xE4))

    /* Offset 0x60 - alternate handler */
    #define P2_FIM_ALT_HANDLER(idx)    (*(uint32_t*)(0xEA54C0 + (idx) * 0xE4))

    /* Offset 0x64 - handler address */
    #define P2_FIM_HANDLER(idx)        (*(uint32_t*)(0xEA54C4 + (idx) * 0xE4))
#else
    static uint32_t p2_fim_dummy32;
    static int16_t p2_fim_dummy16;
    static uint16_t p2_fim_dummy_u16;
    #define P2_FIM_PENDING(idx)        (p2_fim_dummy32)
    #define P2_FIM_BLOCKED2(idx)       (p2_fim_dummy32)
    #define P2_FIM_MASK1(idx)          (p2_fim_dummy32)
    #define P2_FIM_MASK2(idx)          (p2_fim_dummy32)
    #define P2_FIM_BLOCKED1(idx)       (p2_fim_dummy32)
    #define P2_FIM_STATUS(idx)         (p2_fim_dummy32)
    #define P2_FIM_FLAGS(idx)          (p2_fim_dummy_u16)
    #define P2_FIM_XPD(idx)            (p2_fim_dummy16)
    #define P2_FIM_ALT_HANDLER(idx)    (p2_fim_dummy32)
    #define P2_FIM_HANDLER(idx)        (p2_fim_dummy32)
#endif

int8_t PROC2_$DELIVER_FIM(int16_t *signal_ret, status_$t *status,
                           uint32_t *handler_addr_ret,
                           void *fault_param1, void *fault_param2,
                           uint32_t *mask_ret, int8_t *flag_ret)
{
    int16_t cur_idx;
    int16_t signal;
    uint32_t sig_mask;
    uint16_t flags;
    int8_t result = -1;  /* 0xFF = success */
    proc2_info_t *info;

    /* Get current process index */
    cur_idx = P2_PID_TO_INDEX(PROC1_$CURRENT);

    ML_$LOCK(PROC2_LOCK_ID);

    info = P2_INFO_ENTRY(cur_idx);

    /* Loop while status high byte has bit 7 set */
    while (((uint8_t*)status)[1] < 0) {
        /* Get next pending signal */
        signal = PROC2_$GET_NEXT_PENDING_SIGNAL(info);
        *signal_ret = signal;

        if (signal == 0) {
            goto no_signal;
        }

        sig_mask = 1U << (((uint16_t)(signal - 1)) & 0x1F);

        /* Check if signal should bypass normal handling */
        if ((sig_mask & SIGNAL_BYPASS_MASK) != 0 ||
            (sig_mask & ~P2_FIM_PENDING(cur_idx)) == 0) {

            /* Special case for SIGSTOP (0x13 = 19) */
            if (*signal_ret == 0x13) {
                *status = P2_FIM_STATUS(cur_idx);
            } else {
                *status = 0;
            }

            /* Set bit 7 of status high byte */
            ((uint8_t*)status)[1] |= 0x80;
            goto handle_fault;
        }

        /* Clear signal from blocked mask 2 */
        P2_FIM_BLOCKED2(cur_idx) &= ~sig_mask;
    }

    /* Signal already specified in signal_ret */
    signal = *signal_ret;
    sig_mask = 1U << (((uint16_t)(signal - 1)) & 0x1F);

    /* Check if signal needs fault handling */
    if ((sig_mask & SIGNAL_FAULT_MASK) == 0) {
        /* Check mask2 field */
        uint32_t check1 = sig_mask & ~P2_FIM_MASK2(cur_idx);

        if (check1 == 0) {
            goto set_blocked;
        }

        /* Check mask1 field */
        if ((sig_mask & ~P2_FIM_MASK1(cur_idx)) != 0) {
            goto handle_fault;
        }

        if (check1 != 0) {
            goto no_signal;
        }

set_blocked:
        /* Set signal in blocked mask 2 */
        P2_FIM_BLOCKED2(cur_idx) |= sig_mask;
        goto no_signal;
    }

handle_fault:
    /* Check if XPD/debugger should capture this fault */
    if (P2_FIM_XPD(cur_idx) != 0) {
        XPD_$CAPTURE_FAULT(fault_param1, fault_param2, signal_ret, status);

        if (*signal_ret == 0) {
            result = 0;
            goto done;
        }

        /* Recompute signal mask */
        sig_mask = 1U << (((uint16_t)(*signal_ret - 1)) & 0x1F);
    }

    /* Set signal in blocked mask 2 */
    P2_FIM_BLOCKED2(cur_idx) |= sig_mask;

    flags = P2_FIM_FLAGS(cur_idx);

    /* Set flag_ret based on flag bit 10 (0x400) */
    *flag_ret = -((flags & 0x0400) != 0);

    /* Check blocked mask 1 and flag bit 10 */
    if ((sig_mask & ~P2_FIM_BLOCKED1(cur_idx)) == 0 &&
        (flags & 0x0400) == 0) {
        /* Return handler address */
        *handler_addr_ret = P2_FIM_HANDLER(cur_idx);
    }

    /* Check flag bit 14 (0x4000) for alternate handler */
    if ((flags & 0x4000) != 0) {
        *mask_ret = P2_FIM_ALT_HANDLER(cur_idx);
        /* Clear flag bit 6 */
        P2_FIM_FLAGS(cur_idx) &= 0xFFBF;
    } else {
        *mask_ret = P2_FIM_MASK2(cur_idx);
    }
    goto done;

no_signal:
    result = 0;
    FIM_$ADVANCE_SIGNAL_DELIVERY();

done:
    ML_$UNLOCK(PROC2_LOCK_ID);
    return result;
}
