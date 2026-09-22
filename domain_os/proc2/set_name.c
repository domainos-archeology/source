/*
 * PROC2_$SET_NAME - Name a process
 *
 * Re-emitted from the image (0x00E3EA2C..0x00E3EACE, 164 bytes).
 *
 * Frame (link.w A6,-0x10; A5 = 0xE7BE84):
 *   (0x8,A6)  name -> A2    (0xC,A6)  name_len ptr -> A3
 *   (0x10,A6) proc_uid (pushed by value to FIND_INDEX)   (0x14,A6) status_ret
 *   A6-0x4 status
 *
 * Entry fields: (-0x26,A0) = +0xBE name_len byte, (-0x47,A0,D0) with D0 =
 * 1..len = +0x9E..: the name.  A length of 0 stores 0x22 ('"'); otherwise
 * the LOW byte of the length word (`move.b (0x1,A3)`) is stored.
 *
 * Only reference: the SVC table entry at 0x00E7B9D6.
 *
 * Original address: 0x00e3ea2c
 */

#include "proc2/proc2_internal.h"

void PROC2_$SET_NAME(char *name, int16_t *name_len, uid_t *proc_uid, status_$t *status_ret)
{
    int16_t index;               /* D0 */
    proc2_info_t *entry;         /* A0 (biased) */
    status_$t status;            /* A6-0x4 */
    int16_t len;                 /* D1 */
    int16_t i;

    /* 0x00E3EA3E-0x00E3EA4E */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E3EA50-0x00E3EA5C */
    index = PROC2_$FIND_INDEX(proc_uid, &status);

    /* 0x00E3EA5E: tst.l (-0x4,A6) / bne -> unlock */
    if (status == status_$ok) {
        entry = P2_INFO_ENTRY(index);                        /* 0x00E3EA64-0x00E3EA70 */
        len = *name_len;                                     /* 0x00E3EA74 */

        /* 0x00E3EA76-0x00E3EA7C: bmi / cmpi #0x20 ble */
        if (len < 0 || len > 0x20) {
            status = status_$proc2_invalid_process_name;     /* 0x00E3EA7E */
        } else if (len == 0) {
            entry->name_len = 0x22;                          /* 0x00E3EA8C */
        } else {
            /* 0x00E3EA94-0x00E3EAA8: dbf copies len bytes */
            for (i = 0; i < len; i++) {
                entry->name[i] = name[i];
            }
            /* 0x00E3EAAC: move.b (0x1,A3),(-0x26,A0) -- low byte of *name_len */
            entry->name_len = (uint8_t)(*name_len & 0xFF);
        }
    }

    /* 0x00E3EAB2-0x00E3EAC2 */
    ML_$UNLOCK(PROC2_LOCK_ID);
    *status_ret = status;
}
