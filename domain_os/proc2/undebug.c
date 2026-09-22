/*
 * PROC2_$UNDEBUG - Detach the caller from a process it is debugging
 *
 * Re-emitted from the image (0x00E41810..0x00E418AE, 160 bytes) and
 * verified; the previous body was faithful.
 *
 * Frame (link.w A6,-0x14; A5 = 0xE7BE84): (0x8,A6) proc_uid copied to
 * A6-0x8, (0xC,A6) status_ret <- A6-0xC (cleared at 0x00E4181E).
 *
 *   00e41844  PROC2_$FIND_INDEX(&uid, &status) -> D2; tst.l / bne unlock
 *   00e41874  move.w (-0xbe,A0),D0w           ; target+0x26 debugger_idx
 *   00e41878  cmp.w (0x3eb6,A1),D0w           ; == P2_PID_TO_INDEX[PROC1_$CURRENT]
 *   00e4187e  status_$proc2_proc_not_debug_target (0x190010) when not
 *   00e41888  st ; move.w D2w ; bsr DEBUG_CLEAR_INTERNAL(idx, TRUE)
 *
 * Only reference: the SVC table entry at 0x00E7B612.
 *
 * Original address: 0x00e41810
 */

#include "proc2/proc2_internal.h"

void PROC2_$UNDEBUG(uid_t *proc_uid, status_$t *status_ret)
{
    uid_t uid;                   /* A6-0x8 */
    status_$t status;            /* A6-0xC */
    int16_t proc_idx;            /* D2 */
    proc2_info_t *entry;         /* A0 (biased) */

    /* 0x00E4181E-0x00E4182A */
    status = status_$ok;
    uid.high = proc_uid->high;
    uid.low = proc_uid->low;

    /* 0x00E4182E-0x00E4183A */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E4183C-0x00E4184A */
    proc_idx = PROC2_$FIND_INDEX(&uid, &status);

    /* 0x00E4184C: tst.l / bne */
    if (status == status_$ok) {
        entry = P2_INFO_ENTRY(proc_idx);                     /* 0x00E41852-0x00E4185E */
        /* 0x00E41862-0x00E4187C */
        if (entry->debugger_idx == P2_PID_TO_INDEX(PROC1_$CURRENT)) {
            DEBUG_CLEAR_INTERNAL(proc_idx, (int8_t)0xFF);    /* 0x00E41888-0x00E4188C */
        } else {
            status = status_$proc2_proc_not_debug_target;    /* 0x00E4187E */
        }
    }

    /* 0x00E41892-0x00E418A2 */
    ML_$UNLOCK(PROC2_LOCK_ID);
    *status_ret = status;
}
