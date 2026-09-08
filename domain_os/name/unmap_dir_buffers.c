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

void name_$unmap_dir_buffers(int16_t asid, name_$mapped_info_t *mapped_info)
{
    int32_t unmap_size;                 /* D2 */
    status_$t status;                   /* A6-0x0C */

    /* 0x00E5856C: tst.b (A2) / bpl - a Domain boolean */
    if (mapped_info->active < 0) {
        uint32_t first_base = mapped_info->first_base;
        uint32_t second_base = mapped_info->second_base;

        /* 0x00E58572-0x00E5858E: are the two halves contiguous? */
        if (first_base + 0x8000 == second_base) {
            unmap_size = 0x10000;
        } else {
            unmap_size = 0x8000;
        }

        /* 0x00E58590-0x00E585AE */
        MST_$UNMAP_PRIVI(1, &UID_$NIL, first_base, unmap_size, asid, &status);

        /*
         * 0x00E585B2: "tst.w (-0xa,A6)" with the status longword based at
         * A6-0x0C, i.e. the LOW word of the status - the subsystem/module
         * half at A6-0x0C is not looked at.  (source-ujs1)
         */
        if ((uint16_t)status != 0) {
            CRASH_SYSTEM(&status);
        }

        /* 0x00E585C4-0x00E585EE: two separate regions need a second unmap */
        if (unmap_size == 0x8000) {
            MST_$UNMAP_PRIVI(1, &UID_$NIL, second_base, 0x8000, asid, &status);

            /* 0x00E585F2: the same low-word test */
            if ((uint16_t)status != 0) {
                CRASH_SYSTEM(&status);
            }
        }

        /* 0x00E58602: clr.b (A2) */
        mapped_info->active = 0;
    }
}
