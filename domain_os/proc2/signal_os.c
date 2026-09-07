/*
 * PROC2_$SIGNAL_OS - Send a signal to a process (OS internal, no permission
 *                    check)
 *
 * Sends a signal to a process without checking permissions.  Used internally
 * by the OS when it needs to signal a process.
 *
 * Parameters:
 *   proc_uid   - Pointer to target process UID
 *   signal     - Pointer to signal number
 *   param      - Pointer to signal parameter
 *   status_ret - Returns status (0 on success)
 *
 * Original address: 0x00E3F0A6 (138 bytes)
 *
 * Full instruction trace:
 *   00e3f0a6  link.w A6,-0xc
 *   00e3f0aa  movem.l {A5 A3 A2},-(SP)
 *   00e3f0ae  lea (0xe7be84).l,A5       ; PROC2 module data base (unused here)
 *   00e3f0b4  movea.l (0xc,A6),A3       ; signal
 *   00e3f0b8  movea.l (0x10,A6),A2      ; param
 *   00e3f0bc  move.l (A2),(-0x4,A6)     ; param_copy = *param
 *   00e3f0c0  subq.l #0x2,SP / move.w #0x4,-(SP) / jsr 0x00e20b12.l ; ML_$LOCK
 *   00e3f0ce  pea (-0x8,A6)             ; &status
 *   00e3f0d2  move.l (0x8,A6),-(SP)     ; proc_uid
 *   00e3f0d6  bsr.w 0x00e4068e          ; PROC2_$FIND_INDEX -> D0
 *   00e3f0dc  move.w D0w,(-0xa,A6)      ; index
 *   00e3f0e0  tst.l (-0x8,A6)
 *   00e3f0e4  bne.b 0x00e3f0fa
 *   00e3f0e6  pea (-0x8,A6)             ; &status
 *   00e3f0ea  move.l (-0x4,A6),-(SP)    ; param_copy
 *   00e3f0ee  move.w (A3),-(SP)         ; *signal
 *   00e3f0f0  move.w D0w,-(SP)          ; index
 *   00e3f0f2  bsr.w 0x00e3eb8c          ; PROC2_$DELIVER_SIGNAL_INTERNAL
 *   00e3f0f6  lea (0xc,SP),SP
 *   00e3f0fa  subq.l #0x2,SP / move.w #0x4,-(SP) / jsr 0x00e20b62.l ; ML_$UNLOCK
 *   00e3f108  movea.l (0x14,A6),A0
 *   00e3f10c  move.l (-0x8,A6),(A0)     ; *status_ret = status
 *   00e3f110  subq.l #0x2,SP            ; Pascal function result slot
 *   00e3f112  move.l (-0x8,A6),-(SP)    ; status              (arg 5)
 *   00e3f116  move.l (A2),-(SP)         ; *param, re-read     (arg 4)
 *   00e3f118  move.w (A3),-(SP)         ; *signal             (arg 3)
 *   00e3f11a  move.w (-0xa,A6),-(SP)    ; index               (arg 2)
 *   00e3f11e  move.w #0x1,-(SP)         ; event_type = 1      (arg 1)
 *   00e3f122  bsr.w 0x00e3e748          ; PROC2_$LOG_SIGNAL_EVENT
 *   00e3f126  movem.l (-0x18,A6),{A2 A3 A5} / unlk A6 / rts
 *
 * The audit call is made AFTER the lock is dropped and after *status_ret has
 * been written, and its arguments are never popped -- the `unlk` discards
 * them along with the unused Pascal result slot.  Note that the parameter is
 * re-read through A2 for the log record rather than reusing param_copy.
 */

#include "proc2/proc2_internal.h"

void PROC2_$SIGNAL_OS(uid_t *proc_uid, int16_t *signal, uint32_t *param,
                      status_$t *status_ret)
{
    int16_t index;
    status_$t status;
    uint32_t param_copy;

    /* 0x00E3F0BC: the parameter is copied into the frame before the lock */
    param_copy = *param;

    /* 0x00E3F0C6 */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E3F0D6 */
    index = PROC2_$FIND_INDEX(proc_uid, &status);

    /* 0x00E3F0E0 */
    if (status == status_$ok) {
        /* 0x00E3F0F2: delivered without any permission check */
        PROC2_$DELIVER_SIGNAL_INTERNAL(index, *signal, param_copy, &status);
    }

    /* 0x00E3F100 */
    ML_$UNLOCK(PROC2_LOCK_ID);

    /* 0x00E3F10C */
    *status_ret = status;

    /* 0x00E3F122: event type 1 = "signal to a single process" */
    PROC2_$LOG_SIGNAL_EVENT(1, index, (uint16_t)*signal, *param, status);
}
