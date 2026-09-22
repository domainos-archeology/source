/*
 * TIME_$SET_CPU_LIMIT_CALLBACK - Callback for the per-process CPU time limit
 *
 * Fires through TIME_$Q_SCAN_QUEUE's deferred path when a process's
 * accumulated CPU time reaches the limit TIME_$SET_CPU_LIMIT installed.  The
 * argument is the address of a cell holding the address of the 4 bytes DXM
 * copied out of the element's callback_arg - the address-space id as a
 * longword - and `move.w (0x2,A2),D0w` is its LOW word.
 *
 * Original address: 0x00e58af8, 90 bytes
 *
 *   00e58afe  movea.l (0x8,A6),A0 / movea.l (A0),A2   ; -> the copied longword
 *   00e58b04  move.w (0x2,A2),D0w                     ; as_id
 *   00e58b08..00e58b18  A0 = 0xE29198 + (as_id*32 - as_id*4), a sign-extended
 *                       word index                    ; TIME_$CPU_LIMIT_DB[as_id]
 *   00e58b1c  tst.l (0xc,A0) / bne; tst.w (0x10,A0) / beq -> return
 *                                                     ; the element's EXPIRY
 *   00e58b28  pea (-0x8,A6)                           ; status (never read)
 *   00e58b2c  pea (0x26,PC)                           ; 0xE58B54 status cell
 *   00e58b30  pea (0x20,PC)                           ; 0xE58B52 signal cell
 *   00e58b34  move.w (0x2,A2),D1w / lsl.w #3 / pea (0xe7be94,D1w)  ; &PROC2_$UID[as_id]
 *   00e58b44  jsr PROC2_$SIGNAL_OS                    ; args reclaimed by unlk
 *
 * Like the itimer callbacks (bead source-e4a2), the words tested are the
 * expiry at element offsets 0x0C/0x10, not the interval.
 */

#include "time/time_internal.h"

/*
 * `gsk read 0x00E58B4C`:
 *   00e58b52: 00 1b            signal number
 *   00e58b54: 00 0d 00 0b      status_$t "OS / time manager: cpu time limit
 *                              exceeded"
 * The same two cells are passed by TIME_$SET_CPU_LIMIT's own signal path
 * (0x00E59044 pea (-0x4f2,PC) -> 0xE58B54, 0x00E59048 pea (-0x4f8,PC) ->
 * 0xE58B52), hence the non-static linkage.
 */
const int16_t time_$c_cpu_limit_signal = 0x001B;      /* 0x00E58B52 */
const status_$t time_$c_cpu_limit_fault = 0x000D000B; /* 0x00E58B54 */

void TIME_$SET_CPU_LIMIT_CALLBACK(time_$callback_arg_t arg)
{
    uint32_t *callback_arg;
    uint16_t as_id;
    int16_t as_offset;
    time_queue_elem_t *entry;
    status_$t status;           /* A6-0x8 */

    /* 0x00E58AFE..0x00E58B04 */
    callback_arg = *arg;
    as_id = (uint16_t)*callback_arg;

    /* 0x00E58B08..0x00E58B18: id*0x1C as a sign-extended word */
    as_offset = (int16_t)(as_id * CPU_LIMIT_DB_ENTRY_SIZE);
    entry = (time_queue_elem_t *)ARCH_VA_TO_PTR(CPU_LIMIT_DB_BASE + as_offset);

    /* 0x00E58B1C..0x00E58B26 */
    if (entry->expire_high != 0 || entry->expire_low != 0) {
        /* 0x00E58B28..0x00E58B44 */
        PROC2_$SIGNAL_OS(&PROC2_$UID[as_id],
                         (int16_t *)&time_$c_cpu_limit_signal,
                         (uint32_t *)&time_$c_cpu_limit_fault,
                         &status);
    }
}
