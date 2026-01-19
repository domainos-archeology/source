/*
 * SCSI - Small Computer Systems Interface Driver
 *
 * This module provides SCSI controller and device support for Domain/OS.
 *
 * NOTE: In this build, SCSI support is stubbed out. The functions return
 * status_$io_controller_not_in_system or are no-ops. This suggests this
 * particular Domain/OS image was built without SCSI hardware support enabled.
 *
 * Full SCSI implementations would include:
 * - Controller initialization (SCSI_$CINIT)
 * - Device initialization
 * - SCSI command execution
 * - Per-process ASID (Address Space ID) management
 */

#ifndef SCSI_H
#define SCSI_H

#include "base/base.h"

/*
 * Status codes used by SCSI subsystem
 */
#define status_$io_controller_not_in_system 0x00100002

/*
 * SCSI_$CINIT - Controller initialization
 *
 * Initializes a SCSI controller. In this build, returns
 * status_$io_controller_not_in_system indicating no SCSI
 * hardware is configured.
 *
 * Original address: 0x00e34f04
 *
 * @return Status code (always status_$io_controller_not_in_system)
 */
status_$t SCSI_$CINIT(void);

/*
 * SCSI_$FREE_ASID - Free per-process SCSI resources
 *
 * Called during process cleanup to release any SCSI-related
 * resources associated with a process's address space ID.
 *
 * In this build, this is a no-op stub.
 *
 * Original address: 0x00e88800
 */
void SCSI_$FREE_ASID(void);

#endif /* SCSI_H */
