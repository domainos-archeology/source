/*
 * DISK_$PV_ASSIGN_N - Assign a physical volume, with option flags
 *
 * 0x00E6C852 - 0x00E6C95A (266 bytes).  Verified against the disassembly
 * on 2026-09-19; the earlier emission was faithful, this one cites the
 * ranges.  The prologue loads A5 = 0xE826C4 but never uses it.
 *
 * Arguments (all by reference):
 *   (0x8,A6)  unit_type_ptr     -> word (D3): 0, 1 or 4, else
 *                                status_$invalid_unit_number and nothing
 *                                else is written
 *   (0xc,A6)  device_ptr        -> word, copied to (-0x2c,A6)
 *   (0x10,A6) unit_ptr          -> word, copied to (-0x2a,A6)
 *   (0x14,A6) flags_ptr         -> word (D2): bit 0 = do not assign a
 *                                volume index (mount type 0 rather than
 *                                1), bit 1 = return the label record,
 *                                bit 2 = ask for and return the geometry
 *   (0x18,A6) vol_idx_ptr       <- always written on the success path
 *   (0x1c,A6) num_blocks_ptr    <-> longword (A4); -1 in when bit 2 is set
 *   (0x20,A6) sec_per_track_ptr <-> word
 *   (0x24,A6) num_heads_ptr     <-> word
 *   (0x28,A6) pvlabel_info      <-> 16 bytes (A3)
 *   (0x2c,A6) status            <- (A2)
 *
 * DISK_$PV_MOUNT_INTERNAL is called with the four words and the addresses
 * of the local copies (0x00E6C8E2 - 0x00E6C90C), and the copies are
 * handed back according to the flags (0x00E6C910 - 0x00E6C950).
 */

#include "disk/disk_internal.h"

#define DISK_PV_ASSIGN_FLAG_NO_VOLX     0x0001
#define DISK_PV_ASSIGN_FLAG_LABEL       0x0002
#define DISK_PV_ASSIGN_FLAG_GEOMETRY    0x0004

/* 0x00E6C8AA - 0x00E6C8B8 */
#define DISK_UNIT_TYPE_0    0
#define DISK_UNIT_TYPE_1    1
#define DISK_UNIT_TYPE_4    4

void DISK_$PV_ASSIGN_N(int16_t *unit_type_ptr, int16_t *device_ptr,
                       int16_t *unit_ptr, uint16_t *flags_ptr,
                       uint16_t *vol_idx_ptr, uint32_t *num_blocks_ptr,
                       uint16_t *sec_per_track_ptr, uint16_t *num_heads_ptr,
                       uint32_t *pvlabel_info, status_$t *status)
{
    int16_t unit_type;              /* D3 */
    int16_t device;                 /* (-0x2c,A6) */
    int16_t unit;                   /* (-0x2a,A6) */
    uint16_t flags;                 /* D2 */
    int16_t mount_type;             /* (-0x1e,A6) */
    uint16_t vol_idx;               /* (-0x20,A6): not initialised */
    uint32_t num_blocks;            /* (-0x1c,A6) */
    uint16_t sec_per_track;         /* (-0x24,A6) */
    uint16_t num_heads;             /* (-0x22,A6) */
    uint32_t label[4];              /* (-0x10,A6) */
    status_$t local_status;         /* (-0x14,A6) */
    int i;

    /* 0x00E6C86C - 0x00E6C8A8 */
    unit_type = *unit_type_ptr;
    device = *device_ptr;
    unit = *unit_ptr;
    flags = *flags_ptr;
    num_blocks = *num_blocks_ptr;
    sec_per_track = *sec_per_track_ptr;
    num_heads = *num_heads_ptr;
    for (i = 0; i < 4; i++) {
        label[i] = pvlabel_info[i];
    }
    vol_idx = 0;

    /* 0x00E6C8AA - 0x00E6C8C0 */
    if (unit_type != DISK_UNIT_TYPE_0 && unit_type != DISK_UNIT_TYPE_1 &&
        unit_type != DISK_UNIT_TYPE_4) {
        *status = status_$invalid_unit_number;
        return;
    }

    /* 0x00E6C8C4 - 0x00E6C8E0 */
    if ((flags & DISK_PV_ASSIGN_FLAG_GEOMETRY) != 0) {
        num_blocks = 0xFFFFFFFFu;
    }
    mount_type = ((flags & DISK_PV_ASSIGN_FLAG_NO_VOLX) != 0) ? 0 : 1;

    /* 0x00E6C8E2 - 0x00E6C90C */
    DISK_$PV_MOUNT_INTERNAL(mount_type, unit_type, device, unit,
                            &vol_idx, &num_blocks, &sec_per_track, &num_heads,
                            label, &local_status);

    /* 0x00E6C910 - 0x00E6C928 */
    *status = local_status;
    *vol_idx_ptr = vol_idx;

    /* 0x00E6C92A - 0x00E6C93C */
    if ((flags & DISK_PV_ASSIGN_FLAG_LABEL) != 0) {
        for (i = 0; i < 4; i++) {
            pvlabel_info[i] = label[i];
        }
    }

    /* 0x00E6C93E - 0x00E6C950 */
    if ((flags & DISK_PV_ASSIGN_FLAG_GEOMETRY) != 0) {
        *num_blocks_ptr = num_blocks;
        *sec_per_track_ptr = sec_per_track;
        *num_heads_ptr = num_heads;
    }
}
