/*
 * TIME_$SET_ITIMER_REAL_CALLBACK - Callback for the real interval timer
 *
 * Fires from TIME_$Q_SCAN_QUEUE's deferred path when a process's real
 * interval timer expires.  If the entry still carries a non-zero interval
 * the process is signalled.
 *
 * Original address: 0x00e58a38
 *
 * Assembly:
 *   00e58a3e  movea.l (0x8,A6),A0        ; arg
 *   00e58a42  movea.l (A0),A2            ; A2 = &elem->callback_arg
 *   00e58a44  move.w (0x2,A2),D0w        ; as_id = low word of callback_arg
 *   00e58a48..00e58a58                   ; entry = 0xE297F0 + as_id*0x1C
 *   00e58a5c  tst.l (0xc,A0) / bne
 *   00e58a62  tst.w (0x10,A0) / beq -> return
 *   00e58a6c  pea (0x26,PC)              ; -> 0xE58A94, status cell
 *   00e58a70  pea (0x20,PC)              ; -> 0xE58A92, signal-number cell
 *   00e58a80  pea (0x0,A1,D1w*0x1)       ; &PROC2_$UID[as_id]
 *   00e58a84  jsr PROC2_$SIGNAL_OS
 */

#include "time/time_internal.h"

/*
 * Pascal by-reference constants living in the code region behind the rts.
 * Values read with `gsk read 0x00E58A8C`:
 *   00e58a92: 00 0e            signal number
 *   00e58a94: 00 0d 00 07      status_$t "OS / time manager: real interval
 *                              timer fault"
 * NOTE: 0x0E agrees with proc2/proc2.h's SIGALRM, but the sibling callbacks
 * use 0x1D and 0x1B where that table says 26 and 24, so the numbers are
 * reproduced literally rather than mapped through it.
 */
static const int16_t time_$c_itimer_real_signal = 0x000E;   /* 0x00E58A92 */
static const status_$t time_$c_itimer_real_fault = 0x000D0007; /* 0x00E58A94 */

void TIME_$SET_ITIMER_REAL_CALLBACK(time_$callback_arg_t arg)
{
    uint32_t *callback_arg;
    uint16_t as_id;
    int16_t as_offset;
    uint8_t *itimer_entry;
    status_$t status;

    callback_arg = *arg;
    as_id = (uint16_t)*callback_arg;      /* 0xE58A44: move.w (0x2,A2),D0w */

    /* 0xE58A48..0xE58A58: id*32 - id*4 == id*28, sign-extended word index */
    as_offset = (int16_t)(as_id * ITIMER_DB_ENTRY_SIZE);
    itimer_entry = (uint8_t *)ARCH_VA_TO_PTR(ITIMER_DB_BASE + as_offset);

    if (*(uint32_t *)(itimer_entry + ITIMER_REAL_INTERVAL_HIGH) != 0 ||
        *(uint16_t *)(itimer_entry + ITIMER_REAL_INTERVAL_LOW) != 0) {
        PROC2_$SIGNAL_OS(&PROC2_$UID[as_id],   /* 0xE7BE94 + as_id*8 */
                         (int16_t *)&time_$c_itimer_real_signal,
                         (uint32_t *)&time_$c_itimer_real_fault,
                         &status);
    }
}
