/*
 * TIME_$SET_CPU_LIMIT_CALLBACK - Callback for the per-process CPU time limit
 *
 * Fires from the virtual-timer queue when a process's accumulated CPU time
 * reaches the limit installed by TIME_$SET_CPU_LIMIT.
 *
 * Original address: 0x00e58af8
 *
 * Assembly:
 *   00e58afe  movea.l (0x8,A6),A0        ; arg
 *   00e58b02  movea.l (A0),A2            ; A2 = &elem->callback_arg
 *   00e58b04  move.w (0x2,A2),D0w        ; as_id
 *   00e58b08..00e58b18                   ; entry = 0xE29198 + as_id*0x1C
 *   00e58b1c  tst.l (0xc,A0) / bne
 *   00e58b22  tst.w (0x10,A0) / beq -> return
 *   00e58b2c  pea (0x26,PC)              ; -> 0xE58B54, status cell
 *   00e58b30  pea (0x20,PC)              ; -> 0xE58B52, signal-number cell
 *   00e58b44  jsr PROC2_$SIGNAL_OS
 */

#include "time/time_internal.h"

/* Offsets within a CPU-limit entry */
#define CPU_LIMIT_HIGH      0x0C
#define CPU_LIMIT_LOW       0x10

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
    uint8_t *cpu_entry;
    status_$t status;

    callback_arg = *arg;
    as_id = (uint16_t)*callback_arg;      /* 0xE58B04: move.w (0x2,A2),D0w */

    as_offset = (int16_t)(as_id * CPU_LIMIT_DB_ENTRY_SIZE);
    cpu_entry = (uint8_t *)ARCH_VA_TO_PTR(CPU_LIMIT_DB_BASE + as_offset);

    if (*(uint32_t *)(cpu_entry + CPU_LIMIT_HIGH) != 0 ||
        *(uint16_t *)(cpu_entry + CPU_LIMIT_LOW) != 0) {
        PROC2_$SIGNAL_OS(&PROC2_$UID[as_id],   /* 0xE7BE94 + as_id*8 */
                         (int16_t *)&time_$c_cpu_limit_signal,
                         (uint32_t *)&time_$c_cpu_limit_fault,
                         &status);
    }
}
