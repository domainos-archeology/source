/*
 * flop/flop.h - Floppy Disk Boot Module
 *
 * Provides floppy disk boot functionality for Domain/OS.
 * This module handles mounting floppy disks and loading
 * the boot shell from a floppy.
 *
 * The floppy is mounted at "/flp" in the namespace.
 */

#ifndef FLOP_H
#define FLOP_H

#include "base/base.h"

/*
 * ============================================================================
 * Function Prototypes
 * ============================================================================
 */

/*
 * FLOP_$BOOT - Boot from floppy disk
 *
 * Attempts to boot the system from a floppy disk by:
 * 1. Mounting the floppy volume at "/flp"
 * 2. Resolving the path "/flp/sys/boot_shell"
 * 3. Locking and mapping the boot shell executable
 * 4. Returning the entry point address
 *
 * Parameters:
 *   entry_point - Output: receives the entry point address of boot shell
 *   status_ret  - Status return
 *
 * Returns:
 *   Non-zero (true) if boot successful, 0 (false) on failure
 *
 * On success, *entry_point contains the address to jump to for boot.
 * On failure, *status_ret contains the error status.
 *
 * Error handling:
 *   Each step calls flop_$boot_errchk() which invokes OS_$BOOT_ERRCHK
 *   with "Trying normal shell" as the fallback message.
 */
int8_t FLOP_$BOOT(uint32_t *entry_point, status_$t *status_ret);

#endif /* FLOP_H */
