/*
 * TIMER - Hardware Timer Module
 *
 * Provides low-level hardware timer access for Domain/OS.
 * This module interfaces with the MC68901 MFP timer hardware.
 *
 * Hardware timer registers at 0xFFAC00:
 *   - 0xFFAC03: Control/status byte
 *   - 0xFFAC05,07: Real-time timer counter (movep.w access)
 *   - 0xFFAC09,0B: Virtual timer counter (movep.w access)
 */

#ifndef TIMER_H
#define TIMER_H

#include "base/base.h"

/*
 * Hardware timer base address
 */
#define TIMER_BASE_ADDR     0xFFAC00

/*
 * Timer register offsets
 */
#define TIMER_CTRL_OFFSET   0x03    /* Control/status register */
#define TIMER_RTE_HI_OFFSET 0x05    /* RTE timer high byte */
#define TIMER_RTE_LO_OFFSET 0x07    /* RTE timer low byte */
#define TIMER_VT_HI_OFFSET  0x09    /* VT timer high byte */
#define TIMER_VT_LO_OFFSET  0x0B    /* VT timer low byte */

/*
 * Timer control bits
 */
#define TIMER_CTRL_RTE_INT  0x01    /* RTE interrupt pending */
#define TIMER_CTRL_VT_INT   0x02    /* VT interrupt pending */

/*
 * ============================================================================
 * Function Prototypes
 * ============================================================================
 */

/*
 * TIMER_$INIT - Initialize the hardware timer
 *
 * Sets up the hardware timer for use by the TIME subsystem.
 * - Sets timer interrupt vector via TIME_$SET_VECTOR()
 * - Initializes Timer 1 (RTE) with initial tick count (0x1046)
 * - Initializes Timer 2 (VT) and Timer 3 (aux) to max value (disabled)
 * - Programs timer control registers with initialization sequence
 *
 * Returns: 0 on success
 *
 * Original address: 0x00e16340
 */
int32_t TIMER_$INIT(void);

/*
 * TIMER_$READ_RTE - Read real-time timer value
 *
 * Returns the current value of the real-time event timer.
 *
 * Returns:
 *   Current 16-bit timer value
 */
uint16_t TIMER_$READ_RTE(void);

/*
 * TIMER_$READ_VT - Read virtual timer value
 *
 * Returns the current value of the virtual timer.
 *
 * Returns:
 *   Current 16-bit timer value
 */
uint16_t TIMER_$READ_VT(void);

/*
 * TIMER_$WRITE_RTE - Write real-time timer value
 *
 * Sets the real-time event timer countdown value.
 *
 * Parameters:
 *   value - 16-bit timer value to set
 */
void TIMER_$WRITE_RTE(uint16_t value);

/*
 * TIMER_$WRITE_VT - Write virtual timer value
 *
 * Sets the virtual timer countdown value.
 *
 * Parameters:
 *   value - 16-bit timer value to set
 */
void TIMER_$WRITE_VT(uint16_t value);

#endif /* TIMER_H */
