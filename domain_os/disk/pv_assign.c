/*
 * DISK_$PV_ASSIGN - Assign a physical volume (the older, eight-argument form)
 *
 * 0x00E6C95C - 0x00E6C9E6 (140 bytes).  Re-emitted from the disassembly on
 * 2026-09-19: the earlier prototype had a separate num_blocks argument and
 * mis-ordered the last three.  The image passes the caller's `info` cell
 * itself to DISK_$PV_ASSIGN_N as its num_blocks_ptr (`pea (A2)` at
 * 0x00E6C9AC), so the geometry comes back through the same longword the
 * option was read from.  The prologue loads A5 = 0xE826C4 but never uses
 * it.
 *
 * Arguments (0x00E6C96A - 0x00E6C986):
 *   (0x8,A6)  unit_type_ptr     -> word, copied to (-0x22,A6)
 *   (0xc,A6)  device_ptr        -> word, copied to (-0x20,A6)
 *   (0x10,A6) unit_ptr          -> word, copied to (-0x1e,A6)
 *   (0x14,A6) vol_idx_ptr       passed through
 *   (0x18,A6) info_ptr          -> longword (D3): > 0 assign only,
 *                                0 also return geometry, < 0 also return
 *                                the label info to the VA -info
 *   (0x1c,A6) sec_per_track_ptr passed through
 *   (0x20,A6) num_heads_ptr     passed through
 *   (0x24,A6) status            passed through
 *
 * The flags word (-0x1a,A6) is 1 for info > 0, 5 for info == 0, 7 for
 * info < 0 (0x00E6C988 - 0x00E6C998).  After the call, for info < 0, the
 * first longword and the following word of the 16-byte label record
 * (-0x10,A6) are stored at VA -info (0x00E6C9CA - 0x00E6C9D8).
 */

#include "disk/disk_internal.h"

/* Bits of the flags word handed to DISK_$PV_ASSIGN_N */
#define DISK_PV_ASSIGN_FLAG_NO_VOLX     0x0001
#define DISK_PV_ASSIGN_FLAG_LABEL       0x0002
#define DISK_PV_ASSIGN_FLAG_GEOMETRY    0x0004

void DISK_$PV_ASSIGN(int16_t *unit_type_ptr, int16_t *device_ptr,
                     int16_t *unit_ptr, uint16_t *vol_idx_ptr,
                     int32_t *info_ptr, uint16_t *sec_per_track_ptr,
                     uint16_t *num_heads_ptr, status_$t *status)
{
    int16_t unit_type;              /* (-0x22,A6) */
    int16_t device;                 /* (-0x20,A6) */
    int16_t unit;                   /* (-0x1e,A6) */
    uint16_t flags;                 /* (-0x1a,A6) */
    uint32_t label[4];              /* (-0x10,A6): zeroed below where the
                                     * image leaves frame contents */
    int32_t info;                   /* D3 */

    /* 0x00E6C96E - 0x00E6C986 */
    unit_type = *unit_type_ptr;
    device = *device_ptr;
    unit = *unit_ptr;
    info = *info_ptr;

    /* 0x00E6C988 - 0x00E6C998: signed tests (bgt / bpl) */
    flags = DISK_PV_ASSIGN_FLAG_NO_VOLX;
    if (info <= 0) {
        flags = DISK_PV_ASSIGN_FLAG_GEOMETRY | DISK_PV_ASSIGN_FLAG_NO_VOLX;
        if (info < 0) {
            flags |= DISK_PV_ASSIGN_FLAG_LABEL;
        }
    }

    /* 0x00E6C99C - 0x00E6C9C6 */
    label[0] = 0; label[1] = 0; label[2] = 0; label[3] = 0;
    DISK_$PV_ASSIGN_N(&unit_type, &device, &unit, &flags, vol_idx_ptr,
                      (uint32_t *)info_ptr, sec_per_track_ptr, num_heads_ptr,
                      label, status);

    /* 0x00E6C9CA - 0x00E6C9D8: longword + word to the VA -info */
    if (info < 0) {
        uint8_t *dest = (uint8_t *)ARCH_VA_TO_PTR((uint32_t)(-info));
        *(uint32_t *)dest = label[0];
        *(uint16_t *)(dest + 4) = (uint16_t)(label[1] >> 16);
    }
}
