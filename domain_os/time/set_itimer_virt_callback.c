/*
 * TIME_$SET_ITIMER_VIRT_CALLBACK - Callback for the virtual interval timer
 *
 * Identical to TIME_$SET_ITIMER_REAL_CALLBACK except that it inspects the
 * virtual half of the itimer database (real entry + 0x658) and carries its
 * own signal/status constant cells.
 *
 * Original address: 0x00e58a98
 *
 * Assembly:
 *   00e58a9e  movea.l (0x8,A6),A0        ; arg
 *   00e58aa2  movea.l (A0),A2            ; A2 = &elem->callback_arg
 *   00e58aa4  move.w (0x2,A2),D0w        ; as_id
 *   00e58aa8..00e58ab8                   ; entry = 0xE297F0 + as_id*0x1C
 *   00e58abc  tst.l (0x664,A0) / bne
 *   00e58ac2  tst.w (0x668,A0) / beq -> return
 *   00e58acc  pea (0x26,PC)              ; -> 0xE58AF4, status cell
 *   00e58ad0  pea (0x20,PC)              ; -> 0xE58AF2, signal-number cell
 *   00e58ae4  jsr PROC2_$SIGNAL_OS
 */

#include "time/time_internal.h"

/*
 * `gsk read 0x00E58AEC`:
 *   00e58af2: 00 1d            signal number
 *   00e58af4: 00 0d 00 08      status_$t "OS / time manager: virtual interval
 *                              timer fault"
 */
static const int16_t time_$c_itimer_virt_signal = 0x001D;   /* 0x00E58AF2 */
static const status_$t time_$c_itimer_virt_fault = 0x000D0008; /* 0x00E58AF4 */

void TIME_$SET_ITIMER_VIRT_CALLBACK(time_$callback_arg_t arg)
{
    uint32_t *callback_arg;
    uint16_t as_id;
    int16_t as_offset;
    uint8_t *itimer_entry;
    status_$t status;

    callback_arg = *arg;
    as_id = (uint16_t)*callback_arg;      /* 0xE58AA4: move.w (0x2,A2),D0w */

    as_offset = (int16_t)(as_id * ITIMER_DB_ENTRY_SIZE);
    itimer_entry = (uint8_t *)ARCH_VA_TO_PTR(ITIMER_DB_BASE + as_offset);

    if (*(uint32_t *)(itimer_entry + ITIMER_VIRT_INTERVAL_HIGH) != 0 ||
        *(uint16_t *)(itimer_entry + ITIMER_VIRT_INTERVAL_LOW) != 0) {
        PROC2_$SIGNAL_OS(&PROC2_$UID[as_id],   /* 0xE7BE94 + as_id*8 */
                         (int16_t *)&time_$c_itimer_virt_signal,
                         (uint32_t *)&time_$c_itimer_virt_fault,
                         &status);
    }
}
