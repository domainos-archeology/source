/*
 * smd/unmap_display_u.c - Unmap display memory from user-mode access
 *
 * Unmaps display memory from the calling process's address space,
 * releasing the user-mode mapping to the display framebuffer.
 *
 * Original address: 0x00E6F97C
 */

#include "smd/smd_internal.h"
#include "mst/mst.h"

/*
 * SMD_$UNMAP_DISPLAY_U - Unmap display memory from user-mode access
 *
 * Unmaps the display framebuffer from the current process's address space.
 * The mapping is per-ASID, so only affects the calling process.
 *
 * Parameters:
 *   status_ret - Output: status return
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$display_invalid_use_of_driver_procedure - No display associated
 *   status_$display_memory_not_mapped - Display memory not currently mapped
 *   Other MST errors with high bit set on failure
 *
 * Original implementation notes:
 *   - Gets display unit from ASID-to-unit mapping
 *   - Checks if memory is actually mapped for this ASID
 *   - Calls MST_$UNMAP to remove the mapping
 *   - Clears the cached mapping address
 */
void SMD_$UNMAP_DISPLAY_U(status_$t *status_ret)
{
    uint16_t unit;
    uint16_t asid;
    uint8_t *unit_base;
    smd_display_hw_t *hw;
    uint32_t existing_mapping;
    uint32_t unmap_addr;

    /* Get current process's ASID */
    asid = PROC1_$AS_ID;

    /* Look up display unit for this ASID */
    unit = SMD_GLOBALS.asid_to_unit[asid];
    if (unit == 0) {
        /* No display associated with this process */
        *status_ret = status_$display_invalid_use_of_driver_procedure;
        return;
    }

    /*
     * Calculate unit base address.
     * unit_base = SMD_DISPLAY_UNITS + unit * 0x10c
     */
    unit_base = ((uint8_t *)SMD_DISPLAY_UNITS) + (uint32_t)unit * SMD_DISPLAY_UNIT_SIZE;

    /* Get existing mapping for this ASID */
    /* Offset is ASID*4 - 0xe8 from unit_base */
    existing_mapping = *(uint32_t *)(unit_base + ((int32_t)asid * 4) - 0xe8);

    if (existing_mapping == 0) {
        /* Not currently mapped */
        *status_ret = status_$display_memory_not_mapped;
        return;
    }

    /*
     * Unmap the display memory via MST_$UNMAP.
     * Pass the UID from offset +0x0c and the mapped address.
     */
    uid_t *uid_ptr;

    uid_ptr = (uid_t *)(unit_base + 0x0c);

    /* Get hw pointer to determine display type for unmap parameters */
    hw = *(smd_display_hw_t **)(unit_base - 0xf4);

    /* Set up address to unmap */
    unmap_addr = existing_mapping;

    MST_$UNMAP(uid_ptr, &unmap_addr,
               (uint32_t *)((uint8_t *)&SMD_GLOBALS + (hw->display_type << 2)),
               status_ret);

    /* Clear the cached mapping */
    *(uint32_t *)(unit_base + ((int32_t)asid * 4) - 0xe8) = 0;

    if (*status_ret != status_$ok) {
        /* Set high bit to indicate error from nested call */
        *(uint8_t *)status_ret |= 0x80;
    }
}
