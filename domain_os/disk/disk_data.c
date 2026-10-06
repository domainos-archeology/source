/*
 * disk/disk_data.c - DISK subsystem global data
 */

#include "disk/disk_internal.h"

/*
 * DISK_$DATA - the DISK_ module data block (`D E7A1CC DISK_ size = B90`).
 * Zero in the image; DISK_$INIT builds the eventcount, the free list and the
 * volume descriptors at boot.
 *
 * The cells the SAU2 map names inside this block - the module exclusion lock
 * at DMOD_EXCLUSION (0x90), DISK_$DIAG (0xafe) and DISK_$DO_CHKSUM (0xb00) -
 * are accessors into it (disk/disk.h, disk/disk_internal.h), so each image
 * object has exactly one definition here.
 *
 * Original address: 0xE7A1CC
 */
uint8_t DISK_$DATA[DISK_$DATA_SIZE];

/*
 * DISK_$DEVICE_DATA - the device registration table (DISK_$DEVICES) and
 * DISK_$GET_STATS' template (`D E7AD5C DISK_ size = 198`, disk/disk.h).
 * Zero in the image; DISK_$REGISTER fills the table at boot.
 *
 * Original address: 0xE7AD5C
 */
MODULE_DATA_DEFINE(disk_$device_data_t, DISK_$DEVICE_DATA, 0x00E7AD5C);

/*
 * DISK_$MOUNT_DATA - the volume descriptor template and the two small
 * shift tables (`D E826C4 DISK_ size = 64`, disk/disk_internal.h), with
 * the image bytes:
 *   +0x00 00000000 00000000 00000000 00000001
 *   +0x10 00000000 00000000 00000000 00000000
 *   +0x20 0001 0002 0004 0001 0000 0000 0001 0000
 *   +0x30 .. +0x47 zero
 *   +0x48 0000 0000 0001 0000 0002 0000 0000 0000 0003 0000
 *   +0x5C 0001 0002 0004 0000
 *
 * Original address: 0xE826C4
 */
MODULE_DATA_DEFINE_INIT(disk_$mount_data_t, DISK_$MOUNT_DATA, 0x00E826C4, {
    .vol_template = {
        .addr_start     = 1,
        .sec_per_track  = 1,
        .num_heads      = 2,
        .blocks_per_cyl = 4,
        .bat_step       = 1,
        .num_parts      = 1,
    },
    .log2_table = { 0, 0, 1, 0, 2, 0, 0, 0, 3, 0 },
    .pow2_table = { 1, 2, 4, 0 },
});

/*
 * ml_$exclusion_t_00e7a274 - the disk module's second exclusion lock,
 * DISK_$DATA + 0xa8, taken around the raw-PPN path (see disk/io.c).  Zero in
 * the image.
 *
 * Original address: 0xE7A274
 */
ml_$exclusion_t ml_$exclusion_t_00e7a274;

/*
 * MOUNT_LOCK (0xE254B8) - guards the mount/dismount/LV-assign paths.  The
 * SAU2 map places it inside the PMAP_ segment, between PMAP_$L_PURIFIER_EC
 * (0xE254AC) and PMAP_$SCAN_FRACT (0xE254CC), so it is defined with its
 * image contents as PMAP_$DATA.mount_lock in pmap/pmap_data.c (source-iq58).
 */

/*
 * DISK_$DIAG (0xE7ACCA) and DISK_$DO_CHKSUM (0xE7ACCC) are bytes of
 * DISK_$DATA above, reached through the accessors in disk/disk.h.  Both
 * readers of DISK_$DIAG use `tst.b (0x00e7acca).l` (DISK_$DIAG_IO's caller at
 * 0x00E6BCAE and STOP_$WATCH at 0x00E8186A), so it is a single byte; the byte
 * at 0xE7ACCB is padding to DISK_$DO_CHKSUM.  Both are 0x00 in the image.
 */
