/*
 * audit_data.c - Audit Subsystem Global Data
 *
 * This file defines the global variables used by the audit subsystem.
 *
 * Original m68k addresses:
 *   AUDIT_$ENABLED:   0xE2E09E
 *   AUDIT_$CORRUPTED: 0xE2E09C
 *   AUDIT_$DATA:      0xE854D8
 */

#include "audit/audit_internal.h"

/*
 * AUDIT_$ENABLED - Master enable flag
 *
 * Set to 0xFF (-1) when auditing is enabled, 0 when disabled.
 */
int8_t AUDIT_$ENABLED = 0;

/*
 * Audit event-UID table entries (image data, {class, subtype, 0}).
 * VTOC_$MOUNT pushes 0x00E85648 at 0x00E3874C and VTOC_$DISMOUNT pushes
 * 0x00E85640 at 0x00E38894.
 */
uid_t AUDIT_$SET_SID_EU     = { 0x00040007u, 0x00000000u };  /* 0x00E85668 */
uid_t AUDIT_$DISMOUNT_LV_EU = { 0x0004000Eu, 0x00000000u };  /* 0x00E85640 */
uid_t AUDIT_$MOUNT_LV_EU    = { 0x0004000Du, 0x00000000u };  /* 0x00E85648 */

/*
 * AUDIT_$CORRUPTED - Error flag
 *
 * Set to 0xFF (-1) if the audit subsystem encountered an
 * unrecoverable error during initialization.
 */
int8_t AUDIT_$CORRUPTED = 0;

/*
 * AUDIT_$DATA - Main audit subsystem data area
 *
 * Contains all per-subsystem state including:
 *   - Per-process suspension counters
 *   - Log file state and buffer
 *   - Audit list hash table
 *   - Server process state
 */
audit_data_t AUDIT_$DATA;
