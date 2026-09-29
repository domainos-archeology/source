/*
 * PROC2_$SET_TTY - Record the calling process's controlling TTY
 *
 * Re-emitted from the image (0x00E41C04..0x00E41C40, 62 bytes) and
 * verified; the previous body was faithful.
 *
 *   00e41c10  move.w PROC1_$CURRENT,D0w ; add.w D0w,D0w
 *   00e41c22  move.w (0x3eb6,A1),D0w      ; PROC2_$DATA.pid_to_index[PROC1_$CURRENT]
 *   00e41c26  mulu.w #0xe4,D0
 *   00e41c2e  movea.l (0x8,A6),A0         ; tty_uid
 *   00e41c32  move.l (A0)+,(-0x84,A1)     ; entry+0x60
 *   00e41c36  move.l (A0)+,(-0x80,A1)     ; entry+0x64
 *
 * One parameter, no lock, no status.  Only reference: SVC table 0x00E7B446.
 *
 * Original address: 0x00e41c04
 */

#include "proc2/proc2_internal.h"

void PROC2_$SET_TTY(uid_t *tty_uid)
{
    proc2_info_t *entry = P2_INFO_ENTRY((int16_t)PROC2_$DATA.pid_to_index[PROC1_$CURRENT]);

    /* 0x00E41C32-0x00E41C36 */
    entry->tty_uid.high = tty_uid->high;
    entry->tty_uid.low = tty_uid->low;
}
