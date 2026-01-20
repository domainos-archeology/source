/*
 * smd/map_display_u.c - Map display memory for user-mode access
 *
 * Maps display memory into the calling process's address space,
 * allowing user-mode code to directly access display framebuffer.
 *
 * Original address: 0x00E6F8D0
 */

#include "smd/smd_internal.h"
#include "mst/mst.h"

/*
 * SMD_$MAP_DISPLAY_U - Map display memory for user-mode access
 *
 * Maps the display framebuffer into the current process's address space.
 * The mapping is per-ASID (address space ID), so each process gets its
 * own mapping. If already mapped, returns the existing mapping.
 *
 * Parameters:
 *   mapped_addr - Output: pointer to receive the mapped address
 *   status_ret  - Output: status return
 *
 * Status codes:
 *   status_$ok - Success
 *   status_$display_invalid_use_of_driver_procedure - No display associated
 *   Other MST errors with high bit set on failure
 *
 * Original implementation notes:
 *   - Gets display unit from ASID-to-unit mapping
 *   - If already mapped for this ASID, returns cached address
 *   - Otherwise calls MST_$MAP to create new mapping
 *   - Caches mapped address per-ASID in display unit structure
 */
void SMD_$MAP_DISPLAY_U(uint32_t *mapped_addr, status_$t *status_ret)
{
    uint16_t unit;
    uint16_t asid;
    uint8_t *unit_base;
    smd_display_hw_t *hw;
    uint32_t existing_mapping;
    int32_t map_result;

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
     *
     * Field offsets from unit_base:
     *   -0xf4: hw pointer (in slot unit-1)
     *   -0xe8 + ASID*4: mapped_addresses[ASID] (in slot unit-1)
     *   +0x04: hdm_list_ptr (in slot unit)
     *   +0x0c: UID for MST mapping (in slot unit)
     */
    unit_base = ((uint8_t *)SMD_DISPLAY_UNITS) + (uint32_t)unit * SMD_DISPLAY_UNIT_SIZE;

    /* Get existing mapping for this ASID */
    /* Offset is ASID*4 - 0xe8 from unit_base */
    existing_mapping = *(uint32_t *)(unit_base + ((int32_t)asid * 4) - 0xe8);

    /* Return existing mapping address */
    *mapped_addr = existing_mapping;

    if (existing_mapping == 0) {
        /*
         * No existing mapping - create new one via MST_$MAP.
         *
         * The MST_$MAP call parameters are set up based on the display
         * hardware configuration. The UID is at offset +0x0c from unit_base,
         * and mapping parameters come from static data tables.
         *
         * MST_$MAP returns the mapped address in A0 (map_result).
         */
        uid_t *uid_ptr;
        uint32_t start_va;
        uint32_t length;
        uint16_t area_id;
        uint32_t area_size;
        uint8_t rights;

        /* UID at offset 0x0c from unit_base */
        uid_ptr = (uid_t *)(unit_base + 0x0c);

        /* Get hw pointer to determine display type for mapping parameters */
        hw = *(smd_display_hw_t **)(unit_base - 0xf4);

        /*
         * Set up mapping parameters based on display type.
         * These are from static tables in the original binary at addresses
         * 0xe6f976, 0xe6f978, etc. For now, use placeholder values.
         *
         * TODO: Extract actual mapping parameter tables from binary.
         */
        start_va = 0;       /* Let MST choose address */
        length = 0;         /* Map entire object */
        area_id = 0;        /* From static table based on display type */
        area_size = 0;      /* From static table */
        rights = 0x06;      /* Read/write access */

        MST_$MAP(uid_ptr, &start_va, &length, &area_id, &area_size,
                 &rights, &map_result, status_ret);

        /* Store mapped address in output and cache */
        *mapped_addr = (uint32_t)map_result;

        if (*status_ret != status_$ok) {
            /* Set high bit to indicate error from nested call */
            *(uint8_t *)status_ret |= 0x80;
        }

        /* Cache the mapping for future calls */
        *(uint32_t *)(unit_base + ((int32_t)asid * 4) - 0xe8) = (uint32_t)map_result;
    } else {
        /* Already mapped - return success */
        *status_ret = status_$ok;
    }
}
