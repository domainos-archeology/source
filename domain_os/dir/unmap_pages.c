/*
 * DIR_$UNMAP_PAGES - Unmap directory pages from memory
 *
 * Unmaps the cached directory page groups. For generic directories
 * (type 0), calls MST_$UNMAP_PRIVI to actually unmap the mapped
 * regions. For well-known directories (NODE, COM, WDIR, NDIR),
 * saves the mapping info back to the global mapping info arrays
 * so subsequent opens can reuse the mapping.
 *
 * Handle fields used:
 *   +0x0C: Directory type (0=generic, 1=NODE, 2=COM, 3=WDIR, 4=NDIR)
 *   +0x20: Mapped flag (16 bytes of mapping info starting here)
 *   +0x24: Cache slot 0 base address
 *   +0x2C: Cache slot 1 base address
 *
 * Parameters:
 *   handle - Pointer to handle structure
 *
 * Original address: 0x00E4B6BA
 * Original size: 252 bytes
 */

#include "dir/dir_internal.h"
#include "name/name.h"

void DIR_$UNMAP_PAGES(void *handle)
{
    uint8_t *h = (uint8_t *)handle;
    status_$t local_status;

    /* Not mapped - nothing to do */
    if ((int8_t)h[0x20] >= 0) {
        return;
    }

    if (*(int16_t *)(h + 0x0C) == 0) {
        /* Generic directory - actually unmap */
        uint32_t unmap_size;

        /* Check if both slots map contiguous 32KB regions */
        if (*(uint32_t *)(h + 0x24) + 0x8000 == *(uint32_t *)(h + 0x2C)) {
            unmap_size = 0x10000;
        } else {
            unmap_size = 0x8000;
        }

        MST_$UNMAP_PRIVI(3, handle, *(uint32_t *)(h + 0x24),
                         unmap_size, PROC1_$AS_ID, &local_status);

        if (unmap_size == 0x8000) {
            /* Need to unmap second slot separately */
            MST_$UNMAP_PRIVI(3, handle, *(uint32_t *)(h + 0x2C),
                             0x8000, PROC1_$AS_ID, &local_status);
        }
    } else {
        /* Well-known directory - save mapping info */
        uint32_t *dest;

        if (*(int16_t *)(h + 0x0C) == 1) {
            dest = (uint32_t *)&NAME_$NODE_MAPPED_INFO;
        } else if (*(int16_t *)(h + 0x0C) == 2) {
            dest = (uint32_t *)&NAME_$COM_MAPPED_INFO;
        } else if (*(int16_t *)(h + 0x0C) == 3) {
            /* per-ASID slot: base + (PROC1_$AS_ID << 4) */
            dest = (uint32_t *)&NAME_$DATA.wdir_mapped_info[PROC1_$AS_ID];
        } else if (*(int16_t *)(h + 0x0C) == 4) {
            dest = (uint32_t *)&NAME_$DATA.ndir_mapped_info[PROC1_$AS_ID];
        } else {
            goto clear_flag;
        }

        /* Copy 16 bytes of mapping info from handle to saved location */
        uint32_t *src = (uint32_t *)(h + 0x20);
        dest[0] = src[0];
        dest[1] = src[1];
        dest[2] = src[2];
        dest[3] = src[3];
    }

clear_flag:
    h[0x20] = 0;  /* Clear mapped flag */
}
