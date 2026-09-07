/*
 * disk/disk_data.c - DISK subsystem global data
 */

#include "disk/disk_internal.h"

/*
 * DISK_$DO_CHKSUM - Disk checksum enable flag (negative = checksums on)
 * Original address: 0xE7ACCC (1 byte; image value 0x00)
 */
int8_t DISK_$DO_CHKSUM = 0;

/*
 * DISK_$DATA - the DISK_ module data block (`D E7A1CC DISK_ size = B90`).
 * Zero in the image; DISK_$INIT builds the eventcount, the free list and the
 * volume descriptors at boot.
 *
 * NOTE: the module exclusion lock at DISK_$DATA + DMOD_EXCLUSION (0x90) is the
 * same storage as ml_$exclusion_t_00e7a25c below.  In the image they are one
 * object; here the tree keeps both spellings because the A5-displacement
 * callers (disk_$get_qblks_internal, disk_$rtn_qblks_internal) reach it
 * through the block while DISK_$INIT names it directly.
 *
 * Original address: 0xE7A1CC
 */
uint8_t DISK_$DATA[DISK_$DATA_SIZE];

/*
 * ml_$exclusion_t_00e7a25c - the disk module's request-pool exclusion lock,
 * DISK_$DATA + 0x90.  DISK_$INIT calls ML_$EXCLUSION_INIT on it
 * (disk/init.c); zero in the image.
 *
 * Original address: 0xE7A25C
 */
ml_$exclusion_t ml_$exclusion_t_00e7a25c;

/*
 * ml_$exclusion_t_00e7a274 - the disk module's second exclusion lock,
 * DISK_$DATA + 0xa8, taken around the raw-PPN path (see disk/io.c).  Zero in
 * the image.
 *
 * Original address: 0xE7A274
 */
ml_$exclusion_t ml_$exclusion_t_00e7a274;

/*
 * MOUNT_LOCK - guards the mount/dismount/LV-assign paths.  Named by the SAU2
 * map, which places it between PMAP_$L_PURIFIER_EC (0xE254AC) and
 * PMAP_$SCAN_FRACT (0xE254CC), so it is 0x14 bytes - one ml_$exclusion_t.
 *
 * Unlike the two locks above it is initialised in the image:
 *
 *   00e254b8  00 00 00 00 00 e2 54 b8  00 e2 54 b8 00 00 00 00
 *   00e254c8  ff ff 00 00
 *
 * i.e. f1 = 0, f2 = f3 = &MOUNT_LOCK (an empty self-linked waiter queue),
 * f4 = 0 and f5 = -1 (unlocked).
 *
 * Original address: 0xE254B8
 */
ml_$exclusion_t MOUNT_LOCK = { 0, &MOUNT_LOCK, &MOUNT_LOCK, 0, -1 };

/*
 * DISK_$DIAG - diagnostic mode flag, DISK_$DATA + 0xafe.  Both readers use
 * `tst.b (0x00e7acca).l` (DISK_$DIAG_IO's caller at 0x00E6BCAE and
 * STOP_$WATCH at 0x00E8186A), so it is a single byte; the byte at 0xE7ACCB is
 * padding to DISK_$DO_CHKSUM.  Image value 0x00.
 *
 * Original address: 0xE7ACCA (1 byte)
 */
int8_t DISK_$DIAG = 0;
