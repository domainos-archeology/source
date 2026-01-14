// OS subsystem header for Domain/OS
// Core operating system functions

#ifndef OS_H
#define OS_H

#include "base/base.h"
#include "ec/ec.h"
#include "ml/ml.h"

// =============================================================================
// OS Revision Information
// =============================================================================

// OS_$REV structure size (0x33 * 4 = 204 bytes)
#define OS_REV_SIZE  204

// OS revision info (located at 0xe78400)
extern uint32_t OS_$REV[];

// =============================================================================
// OS Shutdown State
// =============================================================================

// Flag indicating shutdown is in progress
extern char OS_$SHUTTING_DOWN_FLAG;

// Shutdown eventcount
extern ec_$eventcount_t OS_$SHUTDOWN_EC;

// =============================================================================
// Memory Copy/Zero Functions
// =============================================================================

// OS_$DATA_COPY - Copy memory efficiently
// Optimized copy that uses 4-byte transfers when both src and dst are aligned
// @param src: Source address
// @param dst: Destination address
// @param len: Number of bytes to copy
extern void OS_$DATA_COPY(const char *src, char *dst, int len);

// OS_$DATA_ZERO - Zero memory efficiently
// Optimized zero that handles alignment and uses 4-byte writes when possible
// @param ptr: Address to zero
// @param len: Number of bytes to zero
extern void OS_$DATA_ZERO(char *ptr, uint len);

// =============================================================================
// Boot and Initialization Functions
// =============================================================================

// OS_$INIT - Main OS initialization entry point
// Called during system boot to initialize all subsystems
// @param param_1: Boot parameters
// @param param_2: Additional boot parameters
extern void OS_$INIT(uint32_t *param_1, uint32_t *param_2);

// OS_$BOOT_ERRCHK - Check and report boot errors
// If status is non-zero, formats and displays error message, then waits
// @param format_str: Format string for error message
// @param arg_str: Additional argument string
// @param line_ptr: Pointer to line number
// @param status: Pointer to status to check
// @return: 0xFF if status was ok, 0 if error was displayed
extern char OS_$BOOT_ERRCHK(const char *format_str, const char *arg_str,
                            short *line_ptr, status_$t *status);

// =============================================================================
// Version and Information Functions
// =============================================================================

// OS_$GET_REV_INFO - Get OS revision information
// Copies the OS_$REV structure to the caller's buffer
// @param buf: Buffer to receive revision info (must be OS_REV_SIZE bytes)
extern void OS_$GET_REV_INFO(void *buf);

// =============================================================================
// Display Functions
// =============================================================================

// OS_$INSTALL_DISPLAY_ASTE - Install display address space table entry
// Sets up memory mapping for display hardware
// @param uid: UID of the display object
// @param param_2: Virtual address parameter
// @param size: Pointer to size of display memory
// @param touch: Pointer to touch flag (if true, touch all pages)
extern void OS_$INSTALL_DISPLAY_ASTE(uid_$t *uid, void *param_2,
                                     int *size, char *touch);

// =============================================================================
// System Control Functions
// =============================================================================

// OS_$SHUTDOWN - Perform system shutdown
// Shuts down all subsystems in order and halts the system
// @param status: Pointer to status (used for privilege check)
extern void OS_$SHUTDOWN(status_$t *status);

// =============================================================================
// Checksum Functions
// =============================================================================

// OS_$CHKSUM - Calculate checksum (stub implementation)
// Currently returns 0 for both outputs
// @param param_1: First parameter
// @param param_2: Second parameter
// @param param_3: Third parameter
// @param result_byte: Pointer to receive byte result (set to 0)
// @param result_long: Pointer to receive long result (set to 0)
extern void OS_$CHKSUM(void *param_1, void *param_2, void *param_3,
                       char *result_byte, uint32_t *result_long);

// =============================================================================
// Eventcount Functions
// =============================================================================

// OS_$GET_EC - Get the shutdown eventcount
// Returns a registered eventcount for monitoring shutdown state
// @param param_1: Unused parameter
// @param ec_ret: Pointer to receive eventcount pointer
// @param status: Pointer to receive status
extern void OS_$GET_EC(void *param_1, ec_$eventcount_t **ec_ret,
                       status_$t *status);

#endif /* OS_H */
