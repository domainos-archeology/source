/*
 * io_data.c - IO Module Global Data Definitions
 *
 * This file defines the global variables used by the IO subsystem.
 *
 * Original M68K addresses:
 *   IO_$SAVED_OS_SP:    0x00E2E822 (4 bytes, void pointer)
 *   IO_$SAVED_INT_SR:   0x00EB2BF8 (2 bytes, uint16)
 *   IO_$INT_STACK:      below 0x00EB2BE8 (interrupt stack, grows downward)
 */

#include "io/io_internal.h"

/*
 * ============================================================================
 * Interrupt Stack State
 * ============================================================================
 */

/*
 * IO_$SAVED_OS_SP - Saved OS stack pointer during interrupt processing
 *
 * When an interrupt switches to the dedicated interrupt stack, the
 * previous (OS) stack pointer is saved here. A non-zero value indicates
 * we are currently on the interrupt stack. Cleared when switching back
 * to the OS stack.
 *
 * Original address: 0x00E2E822
 */
void *IO_$SAVED_OS_SP = NULL;

/*
 * IO_$SAVED_INT_SR - Saved status register from interrupted context
 *
 * When switching to the interrupt stack, the SR value from the
 * interrupted exception frame is saved here so it can be examined
 * during interrupt exit processing (e.g., to determine the interrupted
 * interrupt priority level).
 *
 * Original address: 0x00EB2BF8
 */
uint16_t IO_$SAVED_INT_SR = 0;

#if !defined(M68K)
/*
 * IO_$INT_STACK - Dedicated interrupt stack buffer
 *
 * On M68K hardware, the interrupt stack is at a fixed address (top at
 * 0x00EB2BE8, growing downward). For non-M68K builds, we allocate a
 * buffer to serve as the interrupt stack.
 *
 * The stack is relatively small since interrupt handlers should be brief
 * and delegate heavier work to deferred interrupt processing.
 */
uint8_t IO_$INT_STACK[IO_$INT_STACK_SIZE];
#endif /* !M68K */
