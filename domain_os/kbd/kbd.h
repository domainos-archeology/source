/*
 * KBD - Keyboard Controller
 *
 * This module provides keyboard controller operations for Domain/OS.
 * It handles keyboard input processing, translation tables, and
 * touchpad event management.
 */

#ifndef KBD_H
#define KBD_H

#include "base/base.h"
#include "ec/ec.h"

/* Forward declaration */
typedef struct kbd_state_t kbd_state_t;

/*
 * ============================================================================
 * Initialization Functions
 * ============================================================================
 */

/*
 * KBD_$INIT - Initialize keyboard state structure
 *
 * Initializes a keyboard state structure for a terminal line.
 * Sets up the touchpad buffer pointer, event counter, and defaults.
 *
 * Parameters:
 *   state - Pointer to keyboard state structure to initialize
 *
 * Original address: 0x00e33364
 */
void KBD_$INIT(kbd_state_t *state);

/*
 * KBD_$RESET - Reset the keyboard controller
 *
 * Resets the keyboard controller hardware to a known state.
 * Currently a no-op stub.
 *
 * Original address: 0x00e1ab28
 */
void KBD_$RESET(void);

/*
 * KBD_$CRASH_INIT - Initialize keyboard for crash console
 *
 * Initializes the keyboard controller for use during crash handling.
 * Currently a no-op stub.
 *
 * Original address: 0x00e1ce94
 */
void KBD_$CRASH_INIT(void);

/*
 * ============================================================================
 * Receive/Processing Functions
 * ============================================================================
 */

/*
 * KBD_$RCV - Keyboard receive handler
 *
 * Processes incoming keyboard data through a state machine.
 * Handles normal keys, touchpad data, and special keys.
 *
 * Parameters:
 *   state - Pointer to keyboard state structure
 *   key   - Received key byte
 *
 * Original address: 0x00e1ccc0
 */
void KBD_$RCV(kbd_state_t *state, uint8_t key);

/*
 * KBD_$OUTPUT_BUFFER_DRAINED - Signal output buffer drained
 *
 * Advances the keyboard event counter to signal that the
 * output buffer has been drained.
 *
 * Parameters:
 *   state - Pointer to keyboard state structure
 *
 * Original address: 0x00e1ce96
 */
void KBD_$OUTPUT_BUFFER_DRAINED(kbd_state_t *state);

/*
 * ============================================================================
 * Descriptor/Mode Functions
 * ============================================================================
 */

/*
 * KBD_$GET_DESC - Get keyboard descriptor
 *
 * Validates the line number and returns the keyboard descriptor.
 *
 * Parameters:
 *   line_ptr   - Pointer to terminal line number
 *   status_ret - Pointer to receive status
 *
 * Returns:
 *   Keyboard descriptor pointer
 *
 * Original address: 0x00e1aa26
 */
void *KBD_$GET_DESC(uint16_t *line_ptr, status_$t *status_ret);

/*
 * KBD_$SET_KBD_MODE - Set keyboard mode
 *
 * Sets the keyboard mode for a terminal line.
 * Currently a stub that always returns success.
 *
 * Parameters:
 *   line_ptr - Pointer to terminal line number
 *   mode     - Pointer to keyboard mode value
 *   status   - Pointer to receive status code
 *
 * Original address: 0x00e72516
 */
void KBD_$SET_KBD_MODE(short *line_ptr, unsigned char *mode, status_$t *status);

/*
 * ============================================================================
 * Keyboard Type Functions
 * ============================================================================
 */

/*
 * KBD_$SET_KBD_TYPE - Set keyboard type
 *
 * Sets the keyboard type for a terminal line.
 *
 * Parameters:
 *   line_ptr - Pointer to terminal line number
 *   type_ptr - Pointer to type data
 *   type_len - Pointer to type string length
 *   status   - Pointer to receive status code
 *
 * Original address: 0x00e72524
 */
void KBD_$SET_KBD_TYPE(uint16_t *line_ptr, void *type_ptr,
                       uint16_t *type_len, status_$t *status);

/*
 * KBD_$INQ_KBD_TYPE - Inquire keyboard type
 *
 * Returns the keyboard type string for a terminal line.
 *
 * Parameters:
 *   line_ptr - Pointer to terminal line number
 *   type_buf - Buffer to receive type string
 *   type_len - Pointer to receive type string length
 *   status   - Pointer to receive status code
 *
 * Original address: 0x00e72562
 */
void KBD_$INQ_KBD_TYPE(uint16_t *line_ptr, uint8_t *type_buf,
                       uint16_t *type_len, status_$t *status);

/*
 * ============================================================================
 * Character I/O Functions
 * ============================================================================
 */

/*
 * KBD_$PUT - Write keyboard string
 *
 * Sends a keyboard string to the terminal subsystem.
 *
 * Parameters:
 *   line_ptr - Pointer to terminal line number
 *   type_ptr - Pointer to type data
 *   str      - String to send
 *   length   - Pointer to length of string
 *   status   - Pointer to receive status code
 *
 * Original address: 0x00e1ceac
 */
void KBD_$PUT(uint16_t *line_ptr, uint16_t *type_ptr, void *str,
              uint16_t *length, status_$t *status);

/*
 * KBD_$GET_CHAR_AND_MODE - Get character and keyboard mode
 *
 * Fetches a character from the keyboard ring buffer.
 *
 * Parameters:
 *   line_ptr - Pointer to terminal line number
 *   char_out - Pointer to receive character
 *   mode_out - Pointer to receive mode
 *   status   - Pointer to receive status code
 *
 * Returns:
 *   -1 (0xFF) if character available, 0 otherwise
 *
 * Original address: 0x00e724c4
 */
int8_t KBD_$GET_CHAR_AND_MODE(uint16_t *line_ptr, uint8_t *char_out,
                               uint8_t *mode_out, status_$t *status);

#endif /* KBD_H */
