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
