/*
 * PROC2_$WAIT_TRY_ZOMBIE - Try to collect status from a zombie child
 *
 * Checks if a zombie process can be reaped and collects its status.
 * For zombies:
 * - If traced (negative flags), calls PROC2_$WAIT_REAP_CHILD
 * - If zombie bit set but not traced, clears debug and copies exit info
 * - If stopped but not reported, returns stop status
 *
 * Parameters:
 *   zombie_idx   - Index of zombie process to check
 *   options      - Wait options (unused in this function)
 *   found        - Output: set to -1 if status collected
 *   result       - Pointer to result buffer
 *   pid_ret      - Pointer to receive zombie's UPID if found
 *
 * Original address: 0x00e3fd06
 */

#include "proc2/proc2_internal.h"

/*
 * Raw memory access macros for zombie-related fields
 */
#if defined(ARCH_M68K)
    #define P2_BASE                 0xEA551C

    /* Flags word at offset 0x2A */
    #define P2_ZW_FLAGS(idx)        (*(int16_t*)(P2_BASE + (idx) * 0xE4 - 0xBA))

    /* Flag byte at offset 0x2B */
    #define P2_ZW_FLAG_BYTE(idx)    (*(uint8_t*)(P2_BASE + (idx) * 0xE4 - 0xB9))

    /* Self index at offset 0x1C */
    #define P2_ZW_SELF_IDX(idx)     (*(int16_t*)(P2_BASE + (idx) * 0xE4 - 0xC8))

    /* UPID at offset 0x32 */
    #define P2_ZW_UPID(idx)         (*(int16_t*)(P2_BASE + (idx) * 0xE4 - 0xCE))

    /* Stop signal at offset 0x50 */
    #define P2_ZW_STOP_SIG(idx)     (*(int16_t*)(P2_BASE + (idx) * 0xE4 - 0x50))

    /* Exit status (2 longs) at offset 0x98 */
    #define P2_ZW_EXIT_STATUS(idx)  ((uint32_t*)(P2_BASE + (idx) * 0xE4 - 0x4C))

    /* Exit info at offset 0xDE */
    #define P2_ZW_EXIT_INFO(idx)    ((uint32_t*)(P2_BASE + (idx) * 0xE4 - 0x22))

    /* UID (2 longs) at offset 0x08 */
    #define P2_ZW_UID(idx)          ((uint32_t*)(P2_BASE + (idx) * 0xE4 - 0xE4))
#else
    static int16_t p2_zw_dummy16;
    static uint8_t p2_zw_dummy8;
    static uint32_t p2_zw_dummy32[2];
    #define P2_ZW_FLAGS(idx)        (p2_zw_dummy16)
    #define P2_ZW_FLAG_BYTE(idx)    (p2_zw_dummy8)
    #define P2_ZW_SELF_IDX(idx)     (p2_zw_dummy16)
    #define P2_ZW_UPID(idx)         (p2_zw_dummy16)
    #define P2_ZW_STOP_SIG(idx)     (p2_zw_dummy16)
    #define P2_ZW_EXIT_STATUS(idx)  (p2_zw_dummy32)
    #define P2_ZW_EXIT_INFO(idx)    (p2_zw_dummy32)
    #define P2_ZW_UID(idx)          (p2_zw_dummy32)
#endif

/* Flag bit definitions */
#define FLAG_ZOMBIE     0x2000  /* Bit 13: Process is a zombie */
#define FLAG_STOPPED    0x10    /* Bit 4: Process is stopped */
#define FLAG_REPORTED   0x20    /* Bit 5: Stop already reported */

void PROC2_$WAIT_TRY_ZOMBIE(int16_t zombie_idx, uint16_t options,
                             int8_t *found, uint32_t *result,
                             int16_t *pid_ret)
{
    int16_t flags;

    *found = 0;

    flags = P2_ZW_FLAGS(zombie_idx);

    /* Check if this is actually a zombie (bit 13 set) */
    if ((flags & FLAG_ZOMBIE) == 0) {
        /*
         * Not a zombie - check if stopped.
         * This handles the case where process stopped but not zombie.
         */
        if ((flags & FLAG_STOPPED) == 0) {
            return;
        }
        if ((flags & FLAG_REPORTED) != 0) {
            /* Already reported to parent */
            return;
        }

        /* Mark as reported and return stop status */
        P2_ZW_FLAG_BYTE(zombie_idx) |= FLAG_REPORTED;

        /* Build stop status: (signal << 8) | 0x7F */
        int32_t stop_status = (int32_t)P2_ZW_STOP_SIG(zombie_idx);
        stop_status = (stop_status << 8) | 0x7F;
        result[0x12] = stop_status;  /* offset 0x48 */

        /* Copy exit info to result offset 0x4C */
        result[0x13] = P2_ZW_EXIT_INFO(zombie_idx)[0];
        ((uint8_t*)result)[0x4D] &= 0x7F;  /* Clear high bit */

        /* Check if negative flag set in exit info */
        if ((int8_t)((uint8_t*)P2_ZW_EXIT_INFO(zombie_idx))[1] < 0) {
            ((uint8_t*)result)[0x64] = 0xFF;
        }

        /* Copy UID to result offset 0x40 */
        result[0x10] = P2_ZW_UID(zombie_idx)[0];
        result[0x11] = P2_ZW_UID(zombie_idx)[1];

        *pid_ret = P2_ZW_UPID(zombie_idx);
        *found = -1;
        return;
    }

    /* It's a zombie - check if traced (negative flags word) */
    if (flags < 0) {
        /* Traced zombie - use full reap function */
        PROC2_$WAIT_REAP_CHILD(zombie_idx, 0, 0, result, pid_ret);
        *found = -1;
        return;
    }

    /* Non-traced zombie - clear debug state and copy exit info directly */
    DEBUG_CLEAR_INTERNAL(P2_ZW_SELF_IDX(zombie_idx), 0);

    /* Copy exit status (2 longs) to result offset 0x48 */
    result[0x12] = P2_ZW_EXIT_STATUS(zombie_idx)[0];
    result[0x13] = P2_ZW_EXIT_STATUS(zombie_idx)[1];

    /* Set UID to NIL */
    result[0x0E] = UID_$NIL.high;
    result[0x0F] = UID_$NIL.low;

    *pid_ret = P2_ZW_UPID(zombie_idx);
    *found = -1;
}
