/*
 * TIME_$WRT_TIMER - Write to a hardware timer
 *
 * Writes a value to one of the hardware timers. The timer index determines
 * which timer register pair receives the value.
 *
 * Parameters:
 *   timer_index - Pointer to timer index (0-3)
 *   value       - Pointer to 16-bit value to write
 *
 * Timer indices:
 *   0 - Control registers (0xFFAC01, 0xFFAC03)
 *   1 - Real-time event timer (0xFFAC05, 0xFFAC07)
 *   2 - Virtual timer (0xFFAC09, 0xFFAC0B)
 *   3 - Auxiliary timer (0xFFAC0D, 0xFFAC0F)
 *
 * Original address: 0x00e2afa0
 *
 * Assembly:
 *   00e2afa0    movea.l (0x4,SP),A0          ; First param (timer_index ptr)
 *   00e2afa4    clr.l D0
 *   00e2afa6    move.w (A0),D0w              ; D0 = *timer_index
 *   00e2afa8    movea.l (0x8,SP),A0          ; Second param (value ptr)
 *   00e2afac    move.w (A0),D1w              ; D1 = *value
 *   00e2afae    lea (0xffac00).l,A0
 *   00e2afb4    asl.w #0x2,D0w               ; D0 = timer_index * 4
 *   00e2afb6    lea (0x0,A0,D0w*0x1),A0      ; A0 = 0xffac00 + (index * 4)
 *   00e2afba    movep.w D1w,(0x1,A0)         ; Write to (A0+1, A0+3)
 *   00e2afbe    cmp.w #0x8,D0w
 *   00e2afc2    beq.b 0x00e2afce             ; if index*4 == 8 (timer 2)
 *   00e2afc4    blt.b 0x00e2afd4             ; if index*4 < 8 (timer 0 or 1)
 *   00e2afc6    sf (0x00e2af6b).l            ; Clear IN_RT_INT (timer 3)
 *   00e2afcc    bra.b 0x00e2afd4
 *   00e2afce    sf (0x00e2af6a).l            ; Clear IN_VT_INT (timer 2)
 *   00e2afd4    rts
 *
 * The M68K movep instruction writes alternating bytes:
 *   movep.w D1,(0x1,A0) writes high byte to (A0+1), low byte to (A0+3)
 */

#include "time/time_internal.h"

void TIME_$WRT_TIMER(uint16_t *timer_index, uint16_t *value)
{
    volatile uint8_t *timer_base = (volatile uint8_t *)TIME_TIMER_BASE;
    uint16_t idx = *timer_index;
    uint16_t val = *value;
    uint16_t offset = idx * 4;

    /*
     * Write timer value using movep equivalent:
     * High byte to (base + offset + 1), low byte to (base + offset + 3)
     */
    timer_base[offset + 1] = (uint8_t)(val >> 8);
    timer_base[offset + 3] = (uint8_t)(val & 0xFF);

    /*
     * Clear interrupt flags based on timer index:
     * - Timer 2 (VT timer): clear IN_VT_INT
     * - Timer 3 (aux timer): clear IN_RT_INT
     */
    if (offset == 8) {
        /* Timer 2: clear virtual timer interrupt flag */
        IN_VT_INT = 0;
    } else if (offset > 8) {
        /* Timer 3+: clear real-time interrupt flag */
        IN_RT_INT = 0;
    }
}
