/*
 * flop/flop_internal.h - Floppy Boot Module Internal Declarations
 *
 * Module FLOP_BOOT: code 0x00E323A8-0x00E32733 (map size 0x38C), holding
 * flop_$boot_errchk (0x00E323A8, nested in FLOP_$BOOT), flop_$mount_floppy
 * (0x00E323E6) and FLOP_$BOOT (0x00E3254C), with the compiler's `pea (d,PC)`
 * constant cells and strings interleaved between them.
 */

#ifndef FLOP_INTERNAL_H
#define FLOP_INTERNAL_H

#include "flop/flop.h"
#include "uid/uid.h"
#include "name/name.h"
#include "file/file.h"
#include "mst/mst.h"
#include "dir/dir.h"
#include "volx/volx.h"
#include "os/os.h"
#include "disk/disk.h"

/*
 * ============================================================================
 * Constant cells shared by mount.c and boot.c
 *
 * Each of these is ONE object in the image (0x00E32538-0x00E32542, defined in
 * mount.c with the image bytes) that both routines read by address, for
 * different purposes.  See mount.c for the per-use `pea` displacements.
 * ============================================================================
 */

/* 0x00E32538: 00 - VOLX write_prot / DISMOUNT force; FILE_$LOCK rights;
 * MST_$MAP_AT concurrency */
extern uint8_t flop_zero_byte;

/* 0x00E3253A: 00 04 - length of "/flp"; FILE_$LOCK lock mode */
extern uint16_t flop_word_four;

/* 0x00E32540: 00 01 - VOLX dev and lv_num; FILE_$LOCK lock index */
extern uint16_t flop_word_one;

/* 0x00E32542: ff - VOLX salvage_ok; MST_$MAP concurrency */
extern uint8_t flop_ff_byte;

/*
 * ============================================================================
 * Internal Function Prototypes
 * ============================================================================
 */

/*
 * flop_$mount_floppy - Mount floppy and add to namespace
 *
 * Mounts the floppy volume (device 1, bus 0, controller 0, LV 1) and links
 * its root directory into the node directory as "flp".  On success
 * *status_ret is VOLX_$MOUNT's status with bit 31 stripped, which may be
 * status_$volume_disk_is_write_protected.
 *
 * Original address: 0x00E323E6 (338 bytes)
 */
void flop_$mount_floppy(status_$t *status_ret);

#endif /* FLOP_INTERNAL_H */
