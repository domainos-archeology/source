/*
 * name_$unmap_dir_buffers - Unmap directory memory-mapped buffers
 *
 * Unmaps the memory regions used for a directory's mapped access.
 * Each directory mapping consists of either one 0x10000-byte region
 * (if the two halves are contiguous) or two 0x8000-byte regions.
 *
 * The mapped_info structure layout:
 *   byte 0:    active flag (negative = active)
 *   offset 4:  first buffer base address (uint32_t)
 *   offset 12: second buffer base address (uint32_t)
 *
 * If first_base + 0x8000 == second_base, the buffers are contiguous
 * and we unmap a single 0x10000 region. Otherwise we unmap two
 * separate 0x8000 regions.
 *
 * After unmapping, clears the active flag (byte 0) to 0.
 *
 * Parameters:
 *   asid        - Address space ID for the unmap
 *   mapped_info - Pointer to the directory mapped info structure
 *
 * Original address: 0x00E58560
 * Size: 174 bytes
 */

#include "name/name_internal.h"
#include "mst/mst.h"
#include "misc/crash_system.h"

void name_$unmap_dir_buffers(int16_t asid, void *mapped_info)
{
    char *info = (char *)mapped_info;
    int32_t unmap_size;
    status_$t status;

    /* Only unmap if the mapping is active (high bit set) */
    if (*info < 0) {
        uint32_t first_base = *(uint32_t *)(info + 4);
        uint32_t second_base = *(uint32_t *)(info + 12);

        /* Check if the two halves are contiguous */
        if (first_base + 0x8000 == second_base) {
            unmap_size = 0x10000;
        } else {
            unmap_size = 0x8000;
        }

        /* Unmap the first (or only) region */
        MST_$UNMAP_PRIVI(1, (uid_t *)&UID_$NIL, first_base, unmap_size, asid, &status);
        if (((uint32_t)status >> 16) != 0) {
            CRASH_SYSTEM(&status);
        }

        /* If two separate regions, unmap the second one too */
        if (unmap_size == 0x8000) {
            MST_$UNMAP_PRIVI(1, (uid_t *)&UID_$NIL, second_base, 0x8000, asid, &status);
            if (((uint32_t)status >> 16) != 0) {
                CRASH_SYSTEM(&status);
            }
        }

        /* Mark mapping as inactive */
        *info = 0;
    }
}
