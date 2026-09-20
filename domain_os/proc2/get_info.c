/*
 * PROC2_$GET_INFO - Build the combined info record for a process UID
 *
 * Re-emitted from the image (0x00E4086E..0x00E4094A, 222 bytes).
 *
 * Frame (link.w A6,-0x1EC; A5 = 0xE7BE84):
 *   (0x8,A6)  proc_uid    copied to A6-0x1D8
 *   (0xC,A6)  info        destination buffer
 *   (0x10,A6) info_len    pointer to the caller's length word -> D2
 *   (0x14,A6) status_ret  <- A6-0x1E0
 *   A6-0xE8   0xE4-byte local record
 *
 * Callers: ASKNODE_$INTERNET_INFO 0x00E64A4A and the SVC table entry at
 * 0x00E7B97E.
 *
 * Original address: 0x00e4086e
 */

#include "proc2/proc2_internal.h"

void PROC2_$GET_INFO(uid_t *proc_uid, void *info, uint16_t *info_len, status_$t *status_ret)
{
    uint8_t local_info[0xE4];    /* A6-0xE8 */
    uid_t uid;                   /* A6-0x1D8 */
    status_$t status;            /* A6-0x1E0 */
    uint16_t len;                /* D2 */
    int16_t proc2_idx;           /* D3 */
    int16_t proc1_pid;
    uint16_t n;                  /* D1 */
    uint16_t i;

    /* 0x00E4087C-0x00E4088A */
    len = *info_len;
    uid.high = proc_uid->high;
    uid.low = proc_uid->low;

    /* 0x00E4088E-0x00E4089A */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E4089C-0x00E408AA */
    proc2_idx = PROC2_$FIND_INDEX(&uid, &status);

    /*
     * 0x00E408AC: tst.w (-0x1de,A6) -- the LOW word of the status; zero
     * means found.  0x00E408B2: otherwise only status_$proc2_zombie
     * (0x0019000E) is allowed through; anything else exits at 0x00E4092E.
     */
    if ((status & 0xFFFF) == 0 || status == status_$proc2_zombie) {
        /* 0x00E408BC-0x00E408EA */
        if (status == status_$proc2_zombie) {
            proc1_pid = 0;                                   /* 0x00E408CE: clr.w */
        } else {
            /* 0x00E408DA-0x00E408E6: entry+0x9A (level1_pid) */
            proc1_pid = (int16_t)P2_INFO_ENTRY(proc2_idx)->level1_pid;
        }

        /* 0x00E408EC-0x00E408EE */
        PROC2_$BUILD_INFO_INTERNAL(proc2_idx, proc1_pid, local_info, &status);

        /* 0x00E408F2-0x00E408FE */
        ML_$UNLOCK(PROC2_LOCK_ID);

        /* 0x00E40900-0x00E4090E: n = min(len, 0xE4) (unsigned, bcc) */
        n = (len < 0xE4) ? len : 0xE4;

        /* 0x00E40912-0x00E40928: D0 = n - 1; bmi skips; dbf copies n bytes */
        for (i = 0; i < n; i++) {
            ((uint8_t *)info)[i] = local_info[i];
        }
    } else {
        /* 0x00E4092E-0x00E40934 */
        ML_$UNLOCK(PROC2_LOCK_ID);
    }

    /* 0x00E4093A-0x00E4093E */
    *status_ret = status;
}
