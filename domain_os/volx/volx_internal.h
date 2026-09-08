/*
 * VOLX - Volume Index Management (Internal)
 *
 * Internal header for the VOLX subsystem.  Holds the mount table object and
 * the accessor every VOLX routine uses to reach an entry.
 */

#ifndef VOLX_INTERNAL_H
#define VOLX_INTERNAL_H

#include "arch/arch.h"
#include "ast/ast.h"
#include "audit/audit.h"
#include "bat/bat.h"
#include "dbuf/dbuf.h"
#include "dir/dir.h"
#include "disk/disk.h"
#include "network/network.h"
#include "name/name.h"   /* status_$directory_is_full, status_$name_already_exists */
#include "volx/volx.h"
#include "vtoc/vtoc.h"

/*
 * ----------------------------------------------------------------------------
 * The VOLX mount table
 *
 * The SR10.2 SAU2 link map lays the region out as
 *
 *   D    E825E4  VFMT_$FORMATN     size = 10
 *   D    E825F4  VFMT_$WRITEN      size = 10
 *   D    E82604  VOLX_             size = C0
 *   D    E826C4  DISK_             size = 64
 *
 * so the table is exactly 0xE82604..0xE826C3 - 0xC0 bytes, six 0x20-byte
 * entries.  (VFMT_$FORMATN and VFMT_$WRITEN are the VFMT procedure-variable
 * descriptors; an earlier revision of this header claimed they were
 * "misidentified labels that actually point into the VOLX table", which the
 * map disproves.  The segment `VOLX_` exports no interior symbols, so the
 * table is module-private and VOLX_$TABLE is a tree name, not a linker one.)
 *
 * Every VOLX routine loads the table base into A5 and holds a BIASED entry
 * pointer: `A5 + vol_idx * 0x20` is the address just PAST entry vol_idx, and
 * every field is read at a negative displacement from it.  The indices are
 * therefore 1-based, entry 1 starting at 0xE82604:
 *
 *   base load        0x00E6B0C4 FIND_VOLX        lea (0xe82604).l,A5
 *                    0x00E6B120 VOLX_$MOUNT      lea (0xe82604).l,A5
 *                    0x00E6B34E VOLX_$DISMOUNT   lea (0xe82604).l,A5
 *                    0x00E6B510 VOLX_$SHUTDOWN   lea (0xe82604).l,A5
 *                    0x00E6B5CE VOLX_$GET_INFO   lea (0xe82604).l,A5
 *                    0x00E6B634 VOLX_$GET_UIDS   lea (0xe82604).l,A5
 *                    0x00E6B6B8 VOLX_$REC_ENTRY  lea (0xe82604).l,A5
 *
 *   scale by 0x20    0x00E6B0DE FIND_VOLX        lea (0x20,A5),A0   (idx = 1)
 *                    0x00E6B104 FIND_VOLX        lea (0x20,A0),A0   (idx++)
 *                    0x00E6B2BE VOLX_$MOUNT      lsl.w #0x5,D1w
 *                    0x00E6B2C0 VOLX_$MOUNT      lea (0x0,A5,D1w*0x1),A0
 *                    0x00E6B418 VOLX_$DISMOUNT   lsl.w #0x5,D0w
 *                    0x00E6B472 VOLX_$DISMOUNT   lsl.w #0x5,D0w
 *                    0x00E6B48E VOLX_$DISMOUNT   lsl.w #0x5,D2w
 *                    0x00E6B524 VOLX_$SHUTDOWN   lea (0x20,A5),A3   (idx = 1)
 *                    0x00E6B5AA VOLX_$SHUTDOWN   lea (0x20,A3),A3   (idx++)
 *                    0x00E6B604 VOLX_$GET_INFO   lsl.w #0x5,D0w
 *                    0x00E6B67C VOLX_$GET_UIDS   lsl.l #0x5,D1
 *                    0x00E6B6CA VOLX_$REC_ENTRY  lsl.w #0x5,D1w
 *
 * Field displacements off that biased pointer (entry offset = 0x20 + disp):
 *
 *   disp   entry off  field       instructions
 *   -0x20  0x00       dir_uid     0x00E6B2C8/0x00E6B2CC (MOUNT store),
 *                                 0x00E6B474 (DISMOUNT compare),
 *                                 0x00E6B54C (SHUTDOWN pea, via A4),
 *                                 0x00E6B606 (GET_INFO load),
 *                                 0x00E6B690 (GET_UIDS load),
 *                                 0x00E6B6CC/0x00E6B6D0 (REC_ENTRY store)
 *   -0x18  0x08       lv_uid      0x00E6B2D4/0x00E6B2D8 (MOUNT store),
 *                                 0x00E6B41A (DISMOUNT compare),
 *                                 0x00E6B682 (GET_UIDS load)
 *   -0x10  0x10       parent_uid  0x00E6B2E0/0x00E6B2E4 (MOUNT store),
 *                                 0x00E6B490/0x00E6B4B0/0x00E6B4CA
 *                                 (DISMOUNT compare, pea, clear),
 *                                 0x00E6B534/0x00E6B550/0x00E6B572
 *                                 (SHUTDOWN, via A4)
 *   -0x08  0x18       dev         0x00E6B0F2 (FIND compare),
 *                                 0x00E6B2E8 (MOUNT store)
 *   -0x06  0x1A       bus         0x00E6B0F8 (FIND compare),
 *                                 0x00E6B2EC (MOUNT store)
 *   -0x04  0x1C       ctlr        0x00E6B0EC (FIND compare),
 *                                 0x00E6B2F0 (MOUNT store)
 *   -0x02  0x1E       lv_num      0x00E6B0E6 (FIND compare),
 *                                 0x00E6B2F4 (MOUNT store),
 *                                 0x00E6B4EC (DISMOUNT clear),
 *                                 0x00E6B52A/0x00E6B598 (SHUTDOWN test/clear)
 *
 * The index range is closed by the two scan loops, which both start at index 1
 * with `moveq #0x1` and run `dbf` on a `moveq #0x5` counter - six iterations,
 * indices 1..6:
 *   FIND_VOLX        0x00E6B0DA moveq #0x5,D3 / 0x00E6B0DC moveq #0x1,D4 /
 *                    0x00E6B108 dbf D3w,0x00E6B0E4
 *   VOLX_$SHUTDOWN   0x00E6B51A moveq #0x5,D2 / 0x00E6B51C moveq #0x1,D3 /
 *                    0x00E6B5AE dbf D2w,0x00E6B528
 *
 * 6 * 0x20 = 0xC0, exactly the segment size, so no index 0 slot exists in the
 * image: the biased pointer is what makes a 1-based index land at offset 0.
 * ----------------------------------------------------------------------------
 */

/* Target address of entry 1 (`D  E82604  VOLX_  size = C0`). */
#define VOLX_$TABLE_ADDR 0x00E82604UL

/* Host-side storage for the table.  On m68k the image's own copy at
 * VOLX_$TABLE_ADDR is used instead and this object is unreferenced. */
extern volx_$entry_t volx_$table_storage[VOLX_MAX_VOLUMES];

#if defined(ARCH_M68K)
#define VOLX_$TABLE ((volx_$entry_t *)VOLX_$TABLE_ADDR)
#else
#define VOLX_$TABLE volx_$table_storage
#endif

/*
 * VOLX_$ENTRY - address volume index `idx` (1..6).
 *
 * This is the C spelling of `A5 + idx * 0x20 - 0x20`; entry 1 is the first
 * element of the table.  Like the original it does no range checking - see
 * VOLX_$REC_ENTRY, whose caller must supply a mounted index.
 */
#define VOLX_$ENTRY(idx) (&VOLX_$TABLE[(int32_t)(idx) - 1])

/* The table is exactly the map's 0xC0-byte VOLX_ segment. */
_Static_assert(sizeof(volx_$entry_t) == 0x20, "volx_$entry_t is 0x20 bytes");
_Static_assert(sizeof(volx_$entry_t) == VOLX_ENTRY_SIZE, "VOLX_ENTRY_SIZE");
_Static_assert(sizeof(volx_$entry_t) * VOLX_MAX_VOLUMES == 0xC0,
               "VOLX_ table extent: D  E82604  VOLX_  size = C0");

/*
 * volx_$drop_mount_lv - the zero longword at 0x00E6B504.
 *
 * DIR_$DROP_MOUNT's third argument is a logical-volume number cell.  Both
 * VOLX_ callers hand it the SAME constant cell, which sits in the VOLX_
 * code segment between the routine that ends at 0x00E6B4FE and
 * VOLX_$SHUTDOWN's `link.w` at 0x00E6B508 and holds 00 00 00 00:
 *
 *   VOLX_$DISMOUNT  0x00E6B4A8  pea (0x5a,PC)   -> 0x00E6B4AA + 0x5A
 *   VOLX_$SHUTDOWN  0x00E6B548  pea (-0x46,PC)  -> 0x00E6B54A - 0x46
 *
 * Neither caller reads it back.  It was two separate function-local statics
 * in the tree, which is one object too many (bead source-9oyx).
 */
extern uint32_t volx_$drop_mount_lv;

#endif /* VOLX_INTERNAL_H */
