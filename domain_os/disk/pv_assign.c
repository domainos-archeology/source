/*
 * DISK_$PV_ASSIGN - Physical Volume Assignment
 *
 * Simplified syscall for assigning a physical volume. This is a wrapper
 * around DISK_$PV_ASSIGN_N that provides a simpler interface.
 *
 * The info_ptr parameter controls behavior:
 *   - info > 0: Standard assignment, return vol_idx only
 *   - info == 0: Return geometry (num_blocks, sec_per_track)
 *   - info < 0: Same as info==0, plus copy PV label info to -info address
 *
 * Parameters:
 *   unit_type_ptr   - Pointer to unit type (0=floppy, 1=winchester, 4=optical)
 *   device_ptr      - Pointer to device number
 *   unit_ptr        - Pointer to unit number
 *   vol_idx_ptr     - Output: volume index assigned
 *   info_ptr        - Pointer to info/flags control:
 *                     >0: just assign, vol_idx_ptr receives the volume index
 *                     ==0: return geometry info in num_blocks_ptr/sec_per_track_ptr
 *                     <0: negative of pointer to receive PV label info (6 bytes)
 *   num_blocks_ptr  - I/O: number of blocks on volume (read if info<=0)
 *   sec_per_track_ptr - I/O: sectors per track (read if info<=0)
 *   status          - Output: status code
 *
 * Original address: 0x00e6c95c
 */

#include "disk/disk_internal.h"

/* Flag bits for DISK_$PV_ASSIGN_N */
#define FLAG_NO_RETURN_VOLIDX    0x01  /* Don't return vol_idx */
#define FLAG_RETURN_PVLABEL      0x02  /* Return pvlabel_info */
#define FLAG_RETURN_GEOMETRY     0x04  /* Return num_blocks/geometry */

void DISK_$PV_ASSIGN(int16_t *unit_type_ptr, int16_t *device_ptr,
                     int16_t *unit_ptr, uint16_t *vol_idx_ptr,
                     int32_t *info_ptr, uint32_t *num_blocks_ptr,
                     uint16_t *sec_per_track_ptr, status_$t *status)
{
    int16_t unit_type;
    int16_t device;
    int16_t unit;
    int32_t info;
    uint16_t flags;

    /* Local copies for internal call */
    uint16_t local_num_heads;
    uint32_t local_pvlabel[4];  /* 16 bytes: pvlabel_info */

    /* Read input parameters */
    unit_type = *unit_type_ptr;
    device = *device_ptr;
    unit = *unit_ptr;
    info = *info_ptr;

    /* Determine flags based on info value:
     * info > 0: flags = 1 (return vol_idx only)
     * info == 0: flags = 5 (return vol_idx + geometry)
     * info < 0: flags = 7 (return vol_idx + geometry + pvlabel)
     */
    flags = 1;  /* Default: return vol_idx only */
    if (info <= 0) {
        flags = FLAG_RETURN_GEOMETRY | FLAG_NO_RETURN_VOLIDX;  /* 5 */
        if (info < 0) {
            flags |= FLAG_RETURN_PVLABEL;  /* 7 */
        }
    }

    /* Call extended version */
    DISK_$PV_ASSIGN_N(&unit_type, &device, &unit, &flags,
                      vol_idx_ptr, num_blocks_ptr,
                      sec_per_track_ptr, &local_num_heads,
                      local_pvlabel, status);

    /* If info < 0, copy pvlabel_info to the address specified by -info */
    if (info < 0) {
        uint32_t *dest = (uint32_t *)(-info);
        /* Copy first 4 bytes (uint32_t) and then 2 bytes (uint16_t) */
        dest[0] = local_pvlabel[0];
        /* The 6-byte pvlabel info is stored as 4+2 bytes */
        *((uint16_t *)(dest + 1)) = (uint16_t)(local_pvlabel[1] >> 16);
    }
}
