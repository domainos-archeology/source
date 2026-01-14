/*
 * PROC2_$SET_ACCT_INFO - Set accounting information for current process
 *
 * Sets the accounting info string and accounting UID for the current process.
 * The info string is stored in the process info structure (max 32 bytes).
 *
 * Parameters:
 *   info       - Pointer to accounting info string
 *   info_len   - Pointer to length of info string (max 32)
 *   acct_uid   - Pointer to accounting UID (8 bytes)
 *   status_ret - Returns status (always 0)
 *
 * Original address: 0x00e41ac0
 */

#include "proc2.h"

/*
 * Raw memory access macros for accounting fields
 * These are within proc2_info_t but not fully documented
 */
#if defined(M68K)
    #define P2_ACCT_BASE(idx)          ((uint8_t*)(0xEA551C + ((idx) * 0xE4)))

    /* Offset 0x2B - flags byte 1 */
    #define P2_ACCT_FLAGS_B1(idx)      (*(uint8_t*)(0xEA5463 + (idx) * 0xE4))

    /* Offset 0x64-0x6B - accounting string area (starts at byte 1) */
    #define P2_ACCT_STRING(idx)        ((uint8_t*)(0xEA5464 + (idx) * 0xE4))

    /* Offset 0x70 - accounting string length */
    #define P2_ACCT_LEN(idx)           (*(int16_t*)(0xEA548C + (idx) * 0xE4))

    /* Offset 0x68 - accounting UID (8 bytes) */
    #define P2_ACCT_UID_HI(idx)        (*(uint32_t*)(0xEA5484 + (idx) * 0xE4))
    #define P2_ACCT_UID_LO(idx)        (*(uint32_t*)(0xEA5488 + (idx) * 0xE4))
#else
    static uint8_t p2_acct_dummy8;
    static uint8_t p2_acct_string_dummy[32];
    static int16_t p2_acct_len_dummy;
    static uint32_t p2_acct_uid_dummy;
    #define P2_ACCT_FLAGS_B1(idx)      (p2_acct_dummy8)
    #define P2_ACCT_STRING(idx)        (p2_acct_string_dummy)
    #define P2_ACCT_LEN(idx)           (p2_acct_len_dummy)
    #define P2_ACCT_UID_HI(idx)        (p2_acct_uid_dummy)
    #define P2_ACCT_UID_LO(idx)        (p2_acct_uid_dummy)
#endif

void PROC2_$SET_ACCT_INFO(uint8_t *info, int16_t *info_len, uid_t *acct_uid,
                          status_$t *status_ret)
{
    int16_t cur_idx;
    int16_t len;
    int16_t i;

    ML_$LOCK(PROC2_LOCK_ID);

    /* Get current process index */
    cur_idx = P2_PID_TO_INDEX(PROC1_$CURRENT);

    /* Clamp length to max 32 */
    len = *info_len;
    if (len > 32) {
        len = 32;
    }

    /* Copy accounting info string (bytes 1 through len) */
    for (i = 0; i < len; i++) {
        P2_ACCT_STRING(cur_idx)[i] = info[i];
    }

    /* Store length */
    P2_ACCT_LEN(cur_idx) = len;

    /* Store accounting UID (8 bytes) */
    P2_ACCT_UID_HI(cur_idx) = acct_uid->high;
    P2_ACCT_UID_LO(cur_idx) = acct_uid->low;

    /* Clear flag bit 3 in flags byte 1 */
    P2_ACCT_FLAGS_B1(cur_idx) &= 0xF7;

    ML_$UNLOCK(PROC2_LOCK_ID);

    *status_ret = 0;
}
