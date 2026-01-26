/*
 * flop/flop_internal.h - Floppy Boot Module Internal Declarations
 *
 * Internal data and helper function declarations for the floppy
 * boot module.
 */

#ifndef FLOP_INTERNAL_H
#define FLOP_INTERNAL_H

#include "flop/flop.h"
#include "uid/uid.h"
#include "name/name.h"
#include "file/file.h"
#include "mst/mst.h"
#include "dir/dir.h"
#include "volx/volx.h"
#include "os/os.h"
#include "disk/disk.h"

/*
 * ============================================================================
 * Constants
 * ============================================================================
 */

/* Floppy mount point name */
#define FLOP_MOUNT_NAME     "flp"
#define FLOP_MOUNT_NAME_LEN 3

/* Boot shell path */
#define FLOP_BOOT_SHELL_PATH     "/flp/sys/boot_shell"
#define FLOP_BOOT_SHELL_PATH_LEN 19

/* Fallback error message */
#define FLOP_FALLBACK_MSG       "Trying normal shell"
#define FLOP_FALLBACK_MSG_LEN   19

/*
 * ============================================================================
 * Internal Function Prototypes
 * ============================================================================
 */

/*
 * flop_$mount_floppy - Mount floppy and add to namespace
 *
 * Mounts the floppy volume and creates the "/flp" directory
 * entry in the namespace.
 *
 * Parameters:
 *   status_ret - Status return
 *
 * Original address: 0x00E323E6 (338 bytes)
 */
void flop_$mount_floppy(status_$t *status_ret);

/*
 * flop_$boot_errchk - Check status and report boot error
 *
 * Checks if an error occurred and reports it via OS_$BOOT_ERRCHK.
 * Uses "Trying normal shell" as the fallback message.
 *
 * Parameters:
 *   msg - Error message to display if status is not OK
 *
 * This function accesses the status from the caller's stack frame
 * via the A6 link chain.
 *
 * Original address: 0x00E323A8 (38 bytes)
 */
void flop_$boot_errchk(const char *msg);

#endif /* FLOP_INTERNAL_H */
