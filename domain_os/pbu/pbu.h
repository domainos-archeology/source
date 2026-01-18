/*
 * PBU_$ - Physical Backup Unit (Per-Bus Unit) subsystem
 *
 * This subsystem provides hardware-level event count support for
 * per-bus unit operations. On systems without PBU hardware, most
 * functions return "not present" status.
 *
 * The PBU subsystem manages a pool of 32 eventcounts (indices 0x101-0x120)
 * that can be allocated for hardware-level synchronization.
 *
 * Reverse engineered from Domain/OS
 */

#ifndef PBU_H
#define PBU_H

#include "os/os.h"

/*
 * Status codes for PBU subsystem
 * Module ID: 0x1E (30)
 */
#define status_$pbu_not_present     0x001e000a  /* PBU hardware not present */

/*
 * PBU eventcount index range
 * These indices map to the EC2_$PBU_ECS array
 */
#define PBU_EC_INDEX_MIN    0x101
#define PBU_EC_INDEX_MAX    0x120
#define PBU_EC_COUNT        32      /* 0x120 - 0x101 + 1 */

/*
 * PBU eventcount entry structure
 * Size: 24 bytes (0x18)
 *
 * Each entry contains an EC1-compatible eventcount structure
 * plus additional metadata for ownership validation.
 */
typedef struct pbu_ec_entry_t {
    ec_$eventcount_t ec;        /* 0x00: Eventcount structure (12 bytes) */
    int16_t          owner_id;  /* 0x0C: Owner identifier for validation */
    int16_t          reserved1; /* 0x0E: Reserved */
    int32_t          reserved2; /* 0x10: Reserved */
    int32_t          reserved3; /* 0x14: Reserved */
} pbu_ec_entry_t;

/*
 * External data
 * PBU eventcount array (32 entries at 0xE88460)
 */
extern pbu_ec_entry_t PBU_$EC_ARRAY[PBU_EC_COUNT];

/*
 * PBU_$FREE_ASID - Free an Address Space ID
 *
 * On systems without PBU hardware, this is a no-op.
 *
 * Original address: 0x00e590f8
 */
void PBU_$FREE_ASID(void);

/*
 * PBU_$FAULTED_UNITS - Get count of faulted PBU units
 *
 * On systems without PBU hardware, returns 0 and sets
 * status to status_$pbu_not_present.
 *
 * Parameters:
 *   status_ret - Pointer to receive status
 *
 * Returns:
 *   Count of faulted units (always 0 when PBU not present)
 *
 * Original address: 0x00e590fa
 */
uint16_t PBU_$FAULTED_UNITS(status_$t *status_ret);

/*
 * PBU_$INIT - Initialize PBU subsystem
 *
 * On systems without PBU hardware, this is a no-op.
 *
 * Original address: 0x00e5910e
 */
void PBU_$INIT(void);

/*
 * PBU_$ADVANCE_EC_INT - Advance a PBU eventcount (interrupt-level)
 *
 * Validates that the eventcount index is in the PBU range (0x101-0x120)
 * and that the owner ID matches, then advances the eventcount.
 *
 * Parameters:
 *   owner_id_ptr - Pointer to expected owner ID
 *   ec_index_ptr - Pointer to eventcount index (0x101-0x120)
 *   status_ret   - Pointer to receive status
 *
 * Original address: 0x00e88400
 */
void PBU_$ADVANCE_EC_INT(
    int16_t *owner_id_ptr,
    uint32_t *ec_index_ptr,
    status_$t *status_ret
);

#endif /* PBU_H */
