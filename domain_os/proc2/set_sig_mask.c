/*
 * PROC2_$SET_SIG_MASK - Set signal mask
 *
 * Sets the process signal mask using a combination of clear and set masks.
 * This function provides fine-grained control over multiple signal mask
 * fields in the process info structure.
 *
 * The operation is: new_mask = (old_mask & ~clear_mask) | set_mask
 *
 * Parameters:
 *   priority_delta - Priority adjustment value
 *   clear_mask     - Pointer to 32-byte clear mask structure
 *   set_mask       - Pointer to 32-byte set mask structure
 *   result         - Returns old mask value and flag
 *
 * Clear/Set mask structure (32 bytes / 8 uint32_t):
 *   [0] - mask1 field (offset 0x50)
 *   [1] - mask2 field (offset 0x44) - handler storage
 *   [2] - pending field (offset 0x4C)
 *   [3] - mask3 field (offset 0x60)
 *   [4] - blocked2 clear mask (offset 0x58)
 *   [5] - blocked1 set mask (offset 0x54)
 *   [6] - handler address (if non-zero in clear_mask[6])
 *   [7] - flags (bytes 0x1C-0x1F)
 *
 * Original address: 0x00e3f7de
 */

#include "proc2/proc2_internal.h"

/*
 * Raw memory access macros for signal mask fields
 */
#if defined(ARCH_M68K)
    #define P2_SM_BASE(idx)            ((uint8_t*)(0xEA551C + ((idx) * 0xE4)))

    /* Signal mask fields */
    #define P2_SM_MASK1(idx)           (*(uint32_t*)(0xEA54AC + (idx) * 0xE4))
    #define P2_SM_MASK2(idx)           (*(uint32_t*)(0xEA54B0 + (idx) * 0xE4))
    #define P2_SM_PENDING(idx)         (*(uint32_t*)(0xEA54A8 + (idx) * 0xE4))
    #define P2_SM_MASK3(idx)           (*(uint32_t*)(0xEA54BC + (idx) * 0xE4))
    #define P2_SM_BLOCKED2(idx)        (*(uint32_t*)(0xEA54B8 + (idx) * 0xE4))
    #define P2_SM_BLOCKED1(idx)        (*(uint32_t*)(0xEA54B4 + (idx) * 0xE4))
    #define P2_SM_HANDLER(idx)         (*(uint32_t*)(0xEA54C4 + (idx) * 0xE4))

    /* Flag bytes */
    #define P2_SM_FLAGS_B0(idx)        (*(uint8_t*)(0xEA5462 + (idx) * 0xE4))
    #define P2_SM_FLAGS_B1(idx)        (*(uint8_t*)(0xEA5463 + (idx) * 0xE4))
    #define P2_SM_FLAGS_W(idx)         (*(uint16_t*)(0xEA5462 + (idx) * 0xE4))

    /* Priority/child list fields */
    #define P2_SM_PRIORITY(idx)        (*(int16_t*)(0xEA5450 + (idx) * 0xE4))
    #define P2_SM_CHILD_LIST(idx)      (*(int16_t*)(0xEA5458 + (idx) * 0xE4))
    #define P2_SM_CHILD_NEXT(idx)      (*(int16_t*)(0xEA545A + (idx) * 0xE4))
    #define P2_SM_CHILD_PRIO(idx)      (*(int16_t*)(0xEA5452 + (idx) * 0xE4))
    #define P2_SM_SELF_IDX(idx)        (*(int16_t*)(0xEA5454 + (idx) * 0xE4))
#else
    static uint32_t p2_sm_dummy32;
    static int16_t p2_sm_dummy16;
    static uint8_t p2_sm_dummy8;
    static uint16_t p2_sm_dummy_u16;
    #define P2_SM_MASK1(idx)           (p2_sm_dummy32)
    #define P2_SM_MASK2(idx)           (p2_sm_dummy32)
    #define P2_SM_PENDING(idx)         (p2_sm_dummy32)
    #define P2_SM_MASK3(idx)           (p2_sm_dummy32)
    #define P2_SM_BLOCKED2(idx)        (p2_sm_dummy32)
    #define P2_SM_BLOCKED1(idx)        (p2_sm_dummy32)
    #define P2_SM_HANDLER(idx)         (p2_sm_dummy32)
    #define P2_SM_FLAGS_B0(idx)        (p2_sm_dummy8)
    #define P2_SM_FLAGS_B1(idx)        (p2_sm_dummy8)
    #define P2_SM_FLAGS_W(idx)         (p2_sm_dummy_u16)
    #define P2_SM_PRIORITY(idx)        (p2_sm_dummy16)
    #define P2_SM_CHILD_LIST(idx)      (p2_sm_dummy16)
    #define P2_SM_CHILD_NEXT(idx)      (p2_sm_dummy16)
    #define P2_SM_CHILD_PRIO(idx)      (p2_sm_dummy16)
    #define P2_SM_SELF_IDX(idx)        (p2_sm_dummy16)
#endif

void PROC2_$SET_SIG_MASK(int16_t *priority_delta, uint32_t *clear_mask,
                          uint32_t *set_mask, uint32_t *result)
{
    int16_t cur_idx;
    int16_t prio_delta;
    int16_t new_prio;
    int16_t child_idx;
    int16_t prev_idx;

    /* Local copies of masks (8 uint32_t each = 32 bytes) */
    uint32_t local_clear[8];
    uint32_t local_set[8];
    int i;

    /* Copy mask structures */
    for (i = 0; i < 8; i++) {
        local_clear[i] = clear_mask[i];
        local_set[i] = set_mask[i];
    }

    prio_delta = *priority_delta;

    /* Get current process index */
    cur_idx = P2_PID_TO_INDEX(PROC1_$CURRENT);

    ML_$LOCK(PROC2_LOCK_ID);

    /* Apply masks to signal fields: new = (old & ~clear) | set */
    P2_SM_MASK1(cur_idx) = (P2_SM_MASK1(cur_idx) & ~local_clear[0]) | local_set[0];
    P2_SM_MASK2(cur_idx) = (P2_SM_MASK2(cur_idx) & ~local_clear[1]) | local_set[1];
    P2_SM_PENDING(cur_idx) = (P2_SM_PENDING(cur_idx) & ~local_clear[2]) | local_set[2];
    P2_SM_MASK3(cur_idx) = (P2_SM_MASK3(cur_idx) & ~local_clear[3]) | local_set[3];

    /* Clear blocked2 using mask from local_clear[4] */
    P2_SM_BLOCKED2(cur_idx) &= ~local_clear[4];

    /* Apply blocked1 mask */
    P2_SM_BLOCKED1(cur_idx) = (P2_SM_BLOCKED1(cur_idx) & ~local_clear[5]) | local_set[5];

    /* Handle flag bytes from local_set[6] (bytes 24-27) */
    uint8_t *clear_bytes = (uint8_t*)&local_clear[6];
    uint8_t *set_bytes = (uint8_t*)&local_set[6];

    /* Clear flag bit 2 if byte 0 of clear_bytes has high bit set */
    if ((int8_t)clear_bytes[0] < 0) {
        P2_SM_FLAGS_B0(cur_idx) &= 0xFB;
    }

    /* Set flag bit 2 if byte 0 of set_bytes has high bit set */
    if ((int8_t)set_bytes[0] < 0) {
        P2_SM_FLAGS_B0(cur_idx) |= 0x04;
    }

    /* Set handler address if local_clear[6] non-zero */
    if (local_clear[6] != 0) {
        P2_SM_HANDLER(cur_idx) = local_set[6];
    }

    /* Handle flag byte 1 */
    if ((int8_t)clear_bytes[1] < 0) {
        P2_SM_FLAGS_B1(cur_idx) &= 0xFB;
    }

    if ((int8_t)set_bytes[1] < 0) {
        P2_SM_FLAGS_B1(cur_idx) |= 0x04;
    }

    /* Handle priority adjustment */
    if (prio_delta != 0) {
        new_prio = P2_SM_PRIORITY(cur_idx) + prio_delta;

        /* Only adjust if new priority is lower but still positive */
        if (new_prio < P2_SM_PRIORITY(cur_idx) && new_prio > 0) {
            /* Iterate child list and detach those with higher priority */
            child_idx = P2_SM_CHILD_LIST(cur_idx);
            prev_idx = 0;

            while (child_idx != 0) {
                if (new_prio < P2_SM_CHILD_PRIO(child_idx)) {
                    int16_t next = P2_SM_CHILD_NEXT(child_idx);
                    PROC2_$DETACH_FROM_PARENT(child_idx, prev_idx);
                    child_idx = next;
                } else {
                    prev_idx = child_idx;
                    child_idx = P2_SM_CHILD_NEXT(child_idx);
                }
            }
        }

        P2_SM_PRIORITY(cur_idx) = new_prio;
    }

    /* Handle SIGCONT (bit 17 = 0x20000) special case */
    if ((P2_SM_BLOCKED2(cur_idx) & 0x20000) == 0) {
        if ((P2_SM_PENDING(cur_idx) & 0x20000) != 0 ||
            (P2_SM_MASK1(cur_idx) & 0x20000) != 0) {

            child_idx = P2_SM_CHILD_LIST(cur_idx);
            prev_idx = 0;

            while (child_idx != 0) {
                /* Check if zombie and same priority */
                if ((P2_SM_FLAGS_W(child_idx) & PROC2_FLAG_ZOMBIE) != 0 &&
                    P2_SM_CHILD_PRIO(child_idx) == P2_SM_PRIORITY(cur_idx)) {

                    if ((P2_SM_MASK1(cur_idx) & 0x20000) == 0) {
                        /* Set flag bit 1 */
                        P2_SM_FLAGS_B1(cur_idx) |= 0x02;
                        break;
                    }

                    int16_t next = P2_SM_CHILD_NEXT(child_idx);
                    PROC2_$DETACH_FROM_PARENT(child_idx, prev_idx);
                    child_idx = next;
                } else {
                    prev_idx = child_idx;
                    child_idx = P2_SM_CHILD_NEXT(child_idx);
                }
            }
        }
    }

    /* Deliver pending signals if any are unblocked */
    if ((P2_SM_BLOCKED2(cur_idx) & ~P2_SM_MASK2(cur_idx)) != 0) {
        PROC2_$DELIVER_PENDING_INTERNAL(P2_SM_SELF_IDX(cur_idx));
    }

    ML_$UNLOCK(PROC2_LOCK_ID);

    /* Return old mask value and flag */
    result[0] = P2_SM_MASK2(cur_idx);

    if ((P2_SM_FLAGS_W(cur_idx) & 0x0400) != 0) {
        result[1] = 1;
    } else {
        result[1] = 0;
    }
}
