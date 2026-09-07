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
    smd_display_unit_t *rec;
    smd_display_hw_t *hw;
    uint32_t unmap_addr;

    /* 0x00e6f98a-0x00e6f996 */
    asid = PROC1_$AS_ID;
    unit = SMD_GLOBALS.asid_to_unit[asid];
    if (unit == 0) {
        /* 0x00e6f99c */
        *status_ret = status_$display_invalid_use_of_driver_procedure;
        return;
    }

    /* 0x00e6f9a6-0x00e6f9b0 */
    rec = smd_$unit_rec((int16_t)unit);

    /* 0x00e6f9b4-0x00e6f9c0: (-0xE8,A3) + asid*4, i.e. the 1-based
     * mapped_addresses entry.  The ASID is re-read from the global. */
    asid = PROC1_$AS_ID;
    unmap_addr = rec->mapped_addresses[asid - 1];

    if (unmap_addr == 0) {
        /* 0x00e6f9c6 */
        *status_ret = status_$display_memory_not_mapped;
        return;
    }

    /*
     * 0x00e6f9ce-0x00e6f9e8: MST_$UNMAP(uid, &addr, &length, status), with the
     * mapped address copied into a local first (the callee gets a var
     * parameter) and the length taken from the per-display-type table at the
     * front of the globals.
     */
    hw = rec->hw;
    MST_$UNMAP(&rec->display_uid, &unmap_addr,
               &SMD_GLOBALS.display_map_length[hw->display_type],
               status_ret);

    /* 0x00e6f9f2-0x00e6f9fe: clear the cached mapping (ASID re-read again) */
    asid = PROC1_$AS_ID;
    rec->mapped_addresses[asid - 1] = 0;

    /* 0x00e6fa02-0x00e6fa06: "bset.b #7,(A2)" sets bit 31 of the status */
    if (*status_ret != status_$ok) {
        *status_ret |= 0x80000000u;
    }
}
