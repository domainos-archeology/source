/*
 * FIM - Fault/Interrupt Manager Module
 *
 * Provides fault and interrupt handling for Domain/OS.
 * Manages signal delivery, cleanup handlers, and quit processing.
 */

#ifndef FIM_H
#define FIM_H

#include "base/base.h"
#include "ec/ec.h"

/*
 * ============================================================================
 * Global Data
 * ============================================================================
 */

/*
 * FIM_$QUIT_VALUE - Quit value array indexed by address space ID
 *
 * Each address space has a quit value that indicates whether
 * a quit (SIGQUIT) has been requested for processes in that AS.
 */
extern uint32_t FIM_$QUIT_VALUE[];

/*
 * FIM_$QUIT_EC - Quit event count array indexed by address space ID
 *
 * Each address space has an event count for quit signaling.
 * When a quit is requested, the corresponding EC is advanced.
 */
extern void *FIM_$QUIT_EC[];

/*
 * ============================================================================
 * Function Prototypes
 * ============================================================================
 */

/*
 * FIM_$INIT - Initialize the FIM subsystem
 */
void FIM_$INIT(void);

/*
 * FIM_$CLEANUP - Perform cleanup processing
 *
 * Called to perform cleanup processing before process termination.
 *
 * Parameters:
 *   context - Cleanup context pointer
 *
 * Returns:
 *   Status code
 */
status_$t FIM_$CLEANUP(void *context);

/*
 * FIM_$RLS_CLEANUP - Release cleanup resources
 *
 * Called to release cleanup handler resources.
 *
 * Parameters:
 *   context - Cleanup context pointer
 */
void FIM_$RLS_CLEANUP(void *context);

/*
 * FIM_$SIGNAL - Signal a fault condition
 *
 * Called to signal a fault condition to the current process.
 *
 * Parameters:
 *   status - The status code representing the fault
 */
void FIM_$SIGNAL(status_$t status);

/*
 * FIM_$SET_HANDLER - Set a signal handler
 *
 * Parameters:
 *   signal - Signal number
 *   handler - Handler function pointer
 *   mask - Signal mask during handler execution
 *   status - Status return
 */
void FIM_$SET_HANDLER(int16_t *signal, void *handler, uint32_t *mask,
                      status_$t *status);

/*
 * FIM_$GET_HANDLER - Get current signal handler
 *
 * Parameters:
 *   signal - Signal number
 *   handler_ret - Receives handler function pointer
 *   mask_ret - Receives signal mask
 *   status - Status return
 */
void FIM_$GET_HANDLER(int16_t *signal, void **handler_ret, uint32_t *mask_ret,
                      status_$t *status);

#endif /* FIM_H */
