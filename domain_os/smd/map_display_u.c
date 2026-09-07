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
 * Constant cells in the code region, all passed by reference with pea (d,PC):
 *   0x00E6F924 pea (-0x14cc,PC) -> 0x00E6F926 - 0x14CC = 0x00E6E45A, a byte
 *                                  holding 0x00 (MST_$MAP's concurrency arg)
 *   0x00E6F928 pea (0x4e,PC)    -> 0x00E6F92A + 0x4E = 0x00E6F978, a longword
 *                                  holding 0x00000000
 *   0x00E6F92C pea (0x48,PC)    -> 0x00E6F92E + 0x48 = 0x00E6F976, a word
 *                                  holding 0x0006 (MST_$MAP's mode)
 *   0x00E6F93C pea (0x3a,PC)    -> 0x00E6F93E + 0x3A = 0x00E6F978 again
 * (contents read with gsk).
 */
static const uint32_t smd_$map_zero = 0;         /* 0x00E6F978 */
static const uint16_t smd_$map_mode = 6;         /* 0x00E6F976 */
static const uint8_t smd_$map_concurrency = 0;   /* 0x00E6E45A */

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
    smd_display_unit_t *rec;
    smd_display_hw_t *hw;
    uint32_t map_info;
    void *map_result;

    /* 0x00e6f8de-0x00e6f8ea */
    asid = PROC1_$AS_ID;
    unit = SMD_GLOBALS.asid_to_unit[asid];
    if (unit == 0) {
        /* 0x00e6f8f0 */
        *status_ret = status_$display_invalid_use_of_driver_procedure;
        return;
    }

    /* 0x00e6f8fa-0x00e6f904: A3 = 0xE2E3FC + unit*0x10C */
    rec = smd_$unit_rec((int16_t)unit);

    /*
     * 0x00e6f908-0x00e6f918: the per-ASID mapping table is indexed as
     * (-0xE8,A3) + asid*4, i.e. record offset 0x0C + asid*4, which is
     * mapped_addresses[asid - 1] (the array itself starts at 0x10).  The ASID
     * is re-read from the global here rather than reusing the earlier copy.
     */
    asid = PROC1_$AS_ID;
    *mapped_addr = rec->mapped_addresses[asid - 1];

    if (*mapped_addr != 0) {
        /* 0x00e6f96a: already mapped - just report success */
        *status_ret = status_$ok;
        return;
    }

    /*
     * 0x00e6f91e-0x00e6f944: eight arguments, pushed right to left.  The
     * length argument is the per-display-type entry at the very front of the
     * globals ("pea (0x0,A5,D1w*0x1)" with D1 = display_type * 4).
     */
    hw = rec->hw;
    map_result = MST_$MAP(&rec->display_uid,
                          (uint32_t *)&smd_$map_zero,
                          &SMD_GLOBALS.display_map_length[hw->display_type],
                          (uint16_t *)&smd_$map_mode,
                          (uint32_t *)&smd_$map_zero,
                          (uint8_t *)&smd_$map_concurrency,
                          &map_info,
                          status_ret);

    /* 0x00e6f94e: MST_$MAP returns the mapped address in A0 */
    *mapped_addr = (uint32_t)(uintptr_t)map_result;

    /* 0x00e6f950-0x00e6f954: "bset.b #7,(A2)" sets bit 31 of the status */
    if (*status_ret != status_$ok) {
        *status_ret |= 0x80000000u;
    }

    /* 0x00e6f958-0x00e6f964: the ASID is read from the global a third time */
    asid = PROC1_$AS_ID;
    rec->mapped_addresses[asid - 1] = (uint32_t)(uintptr_t)map_result;
}
