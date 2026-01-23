/*
 * DISK_$PV_ASSIGN_N - Physical Volume Assignment (Extended)
 *
 * Extended syscall for assigning a physical volume. Provides fine-grained
 * control over which output parameters are updated.
 *
 * This is the main syscall entry point for physical volume assignment.
 * It validates the unit type and delegates to DISK_$PV_MOUNT_INTERNAL.
 *
 * Parameters:
 *   unit_type_ptr     - Pointer to unit type (0=floppy, 1=winchester, 4=optical)
 *   device_ptr        - Pointer to device number
 *   unit_ptr          - Pointer to unit number
 *   flags_ptr         - Pointer to flags:
 *                       bit 0: 0=return vol_idx, 1=don't return vol_idx
 *                       bit 1: return pvlabel_info if set
 *                       bit 2: return num_blocks/sec_per_track/num_heads if set
 *   vol_idx_ptr       - Output: volume index assigned
 *   num_blocks_ptr    - I/O: number of blocks on volume
 *   sec_per_track_ptr - I/O: sectors per track
 *   num_heads_ptr     - I/O: number of heads
 *   pvlabel_info      - I/O: PV label info (16 bytes)
 *   status            - Output: status code
 *
 * Flags behavior:
 *   - If bit 2 (0x04) is set on input, num_blocks is set to -1 (0xffffffff)
 *     to indicate the internal function should determine the value
 *   - If bit 0 (0x01) is clear, vol_idx is returned in vol_idx_ptr
 *   - If bit 1 (0x02) is set, pvlabel_info is updated on output
 *   - If bit 2 (0x04) is set, num_blocks/sec_per_track/num_heads are updated
 *
 * Original address: 0x00e6c852
 */

#include "disk/disk_internal.h"

/* Flag bits */
#define FLAG_NO_RETURN_VOLIDX    0x01  /* Don't return vol_idx */
#define FLAG_RETURN_PVLABEL      0x02  /* Return pvlabel_info */
#define FLAG_RETURN_GEOMETRY     0x04  /* Return num_blocks/geometry */

/* Valid unit types */
#define UNIT_TYPE_FLOPPY      0
#define UNIT_TYPE_WINCHESTER  1
#define UNIT_TYPE_OPTICAL     4

void DISK_$PV_ASSIGN_N(int16_t *unit_type_ptr, int16_t *device_ptr,
                       int16_t *unit_ptr, uint16_t *flags_ptr,
                       uint16_t *vol_idx_ptr, uint32_t *num_blocks_ptr,
                       uint16_t *sec_per_track_ptr, uint16_t *num_heads_ptr,
                       uint32_t *pvlabel_info, status_$t *status)
{
    int16_t unit_type;
    uint16_t flags;
    int16_t device;
    int16_t unit;

    /* Local copies for I/O parameters */
    uint32_t local_num_blocks;
    uint16_t local_sec_per_track;
    uint16_t local_num_heads;
    uint16_t local_vol_idx;
    uint32_t local_pvlabel[4];  /* 16 bytes */

    status_$t local_status;
    uint16_t return_volidx;
    int i;

    /* Read input parameters */
    unit_type = *unit_type_ptr;
    flags = *flags_ptr;
    device = *device_ptr;
    unit = *unit_ptr;

    /* Copy input values that may be both input and output */
    local_num_blocks = *num_blocks_ptr;
    local_sec_per_track = *sec_per_track_ptr;
    local_num_heads = *num_heads_ptr;

    /* Copy pvlabel_info (16 bytes = 4 uint32_t) */
    for (i = 0; i < 4; i++) {
        local_pvlabel[i] = pvlabel_info[i];
    }

    /* Validate unit type */
    if (unit_type != UNIT_TYPE_FLOPPY &&
        unit_type != UNIT_TYPE_WINCHESTER &&
        unit_type != UNIT_TYPE_OPTICAL) {
        *status = status_$invalid_unit_number;
        return;
    }

    /* If FLAG_RETURN_GEOMETRY is set, signal internal function to determine blocks */
    if ((flags & FLAG_RETURN_GEOMETRY) != 0) {
        local_num_blocks = 0xffffffff;
    }

    /* Determine whether to return vol_idx */
    return_volidx = (flags & FLAG_NO_RETURN_VOLIDX) == 0 ? 1 : 0;

    /* Call internal mount function */
    DISK_$PV_MOUNT_INTERNAL(return_volidx, unit_type, device, unit,
                            &local_vol_idx, &local_num_blocks,
                            &local_sec_per_track, &local_num_heads,
                            local_pvlabel, &local_status);

    /* Store status */
    *status = local_status;

    /* Return vol_idx if requested */
    *vol_idx_ptr = local_vol_idx;

    /* Return pvlabel_info if FLAG_RETURN_PVLABEL is set */
    if ((flags & FLAG_RETURN_PVLABEL) != 0) {
        for (i = 0; i < 4; i++) {
            pvlabel_info[i] = local_pvlabel[i];
        }
    }

    /* Return geometry if FLAG_RETURN_GEOMETRY is set */
    if ((flags & FLAG_RETURN_GEOMETRY) != 0) {
        *num_blocks_ptr = local_num_blocks;
        *sec_per_track_ptr = local_sec_per_track;
        *num_heads_ptr = local_num_heads;
    }
}
