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

/*
 * AUDIT_HASH_MODULO - Hash table bucket count
 *
 * Used by UID_$HASH to compute bucket indices.
 */
int16_t AUDIT_HASH_MODULO = AUDIT_HASH_TABLE_SIZE;
