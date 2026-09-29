/*
 * PROC2_$SET_ACCT_INFO - Record accounting info for the calling process
 *
 * Re-emitted from the image (0x00E41AC0..0x00E41B54, 150 bytes).
 *
 * Frame (link.w A6,-0xC; A5 = 0xE7BE84):
 *   (0x8,A6)  info       -> A2     (0xC,A6)  info_len ptr -> D2 = *ptr
 *   (0x10,A6) acct_uid   -> A3     (0x14,A6) status_ret (always status_$ok)
 *
 * The caller's entry is A0 = 0xEA551C + idx*0xE4 = entry + 0xE4:
 * (-0xB9,A3=A0+i) = +0x2B+i, so byte i (1-based) lands at acct_info[i-1];
 * (-0x90) = +0x54 acct_info_len, (-0x98) = +0x4C acct_uid, and the
 * closing bclr.b #3 on (-0xB9,A0) is bit 3 of the LOW byte of flags.
 *
 * Only reference: the SVC table entry at 0x00E7BA92.
 *
 * Original address: 0x00e41ac0
 */

#include "proc2/proc2_internal.h"

void PROC2_$SET_ACCT_INFO(uint8_t *info, int16_t *info_len, uid_t *acct_uid,
                          status_$t *status_ret)
{
    proc2_info_t *entry;         /* A0 (biased) */
    int16_t len;                 /* D2 */
    int16_t i;

    /* 0x00E41ACE-0x00E41ADE */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E41AE0-0x00E41B00 (mulu) */
    entry = P2_INFO_ENTRY((int16_t)PROC2_$DATA.pid_to_index[PROC1_$CURRENT]);

    /* 0x00E41AFE-0x00E41B0A: signed clamp to 32 (ble); negatives pass */
    len = *info_len;
    if (len > 0x20) {
        len = 0x20;
    }

    /* 0x00E41B0C-0x00E41B20: D0 = len - 1; bmi skips; dbf copies len bytes */
    for (i = 0; i < len; i++) {
        entry->acct_info[i] = (char)info[i];
    }

    /* 0x00E41B24: entry+0x54 = len (the clamped, possibly negative word) */
    entry->acct_info_len = (uint16_t)len;

    /* 0x00E41B28-0x00E41B30: entry+0x4C = *acct_uid */
    entry->acct_uid.high = acct_uid->high;
    entry->acct_uid.low = acct_uid->low;

    /* 0x00E41B34: bclr.b #0x3,(-0xb9,A0) -> flags &= ~0x0008 */
    entry->flags &= (uint16_t)~PROC2_FLAG_DEBUG;

    /* 0x00E41B3A-0x00E41B4A */
    ML_$UNLOCK(PROC2_LOCK_ID);
    *status_ret = status_$ok;
}
