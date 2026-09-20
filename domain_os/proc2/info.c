/*
 * PROC2_$INFO - Build the combined info record for an ASID / PROC1 pid
 *
 * Re-emitted from the image (0x00E407B4..0x00E4086C, 186 bytes).
 *
 * Frame (link.w A6,-0x1E4; A5 = 0xE7BE84):
 *   (0x8,A6)  scan_key    pointer to a word compared with each allocated
 *                         entry's ASID (+0x96); 0 selects "no PROC2 entry"
 *   (0xC,A6)  pid         pointer to the PROC1 pid word handed to
 *                         PROC2_$BUILD_INFO_INTERNAL by value
 *   (0x10,A6) info        destination buffer
 *   (0x14,A6) info_len    pointer to the caller's length word -> D2
 *   (0x18,A6) status_ret
 *   A6-0xE8   0xE4-byte local record, A6-0x1D8 status
 *
 * The allocated-list walk (0x00E407D0..0x00E407F6) happens BEFORE the lock
 * is taken.  Sole caller: ASKNODE_$INTERNET_INFO 0x00E64BDA.
 *
 * Original address: 0x00e407b4
 */

#include "proc2/proc2_internal.h"

void PROC2_$INFO(int16_t *scan_key, int16_t *pid, void *info,
                 uint16_t *info_len, status_$t *status_ret)
{
    uint8_t local_info[0xE4];    /* A6-0xE8 */
    status_$t status;            /* A6-0x1D8 */
    uint16_t len;                /* D2 */
    int16_t proc2_idx;           /* D3 */
    uint16_t n;                  /* D1 */
    uint16_t i;

    /* 0x00E407CA */
    len = *info_len;

    /* 0x00E407CC: tst.w (A0) / beq 0x00E407F8 (D3 = 0) */
    if (*scan_key == 0) {
        proc2_idx = 0;                                       /* 0x00E407F8 */
    } else {
        /* 0x00E407D0: D3 = P2_INFO_ALLOC_PTR (0x1E0,A5); beq -> lock with 0 */
        proc2_idx = (int16_t)P2_INFO_ALLOC_PTR;
        while (proc2_idx != 0) {
            proc2_info_t *entry = P2_INFO_ENTRY(proc2_idx);  /* 0x00E407D8-0x00E407E0 */
            /* 0x00E407E4-0x00E407EA: entry+0x96 == *scan_key -> found */
            if (entry->asid == (uint16_t)*scan_key) {
                break;
            }
            /* 0x00E407EC-0x00E407F4: D3 = entry+0x12; bne loop (else D3 = 0) */
            proc2_idx = (int16_t)entry->next_index;
        }
    }

    /* 0x00E407FA-0x00E40806 */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E40808-0x00E4081C: BUILD_INFO(proc2_idx, *pid, &local, &status) */
    PROC2_$BUILD_INFO_INTERNAL(proc2_idx, *pid, local_info, &status);

    /* 0x00E40820-0x00E4082C */
    ML_$UNLOCK(PROC2_LOCK_ID);

    /* 0x00E4082E-0x00E4083C: n = min(len, 0xE4) (unsigned compare, bcc) */
    n = (len < 0xE4) ? len : 0xE4;

    /*
     * 0x00E40840-0x00E40858: D0 = n - 1; bmi skips; dbf copies n bytes
     * from local_info to info.
     */
    for (i = 0; i < n; i++) {
        ((uint8_t *)info)[i] = local_info[i];
    }

    /* 0x00E4085C-0x00E40860 */
    *status_ret = status;
}
