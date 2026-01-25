/*
 * VOLX - Volume Index Management (Internal)
 *
 * Internal header for the VOLX subsystem.
 * Contains internal data structures and helper declarations.
 */

#ifndef VOLX_INTERNAL_H
#define VOLX_INTERNAL_H

#include "volx/volx.h"
#include "ast/ast.h"
#include "network/network.h"
#include "disk/disk.h"
#include "dbuf/dbuf.h"
#include "audit/audit.h"
#include "vtoc/vtoc.h"
#include "dir/dir.h"
#include "disk/disk.h"

/*
 * Access macros for VOLX table
 *
 * The table base is at 0xE82604. Entries are 32 bytes each.
 * Index 0 is unused; valid indices are 1-6.
 *
 * Note: Ghidra shows references like:
 *   - 0xE825E4 = base + 0x00 - 0x20 = dir_uid for entry 1
 *   - 0xE825EC = base + 0x08 - 0x20 = lv_uid for entry 1
 *   - 0xE825F4 = base + 0x10 - 0x20 = parent_uid for entry 1
 *
 * The code uses vol_idx << 5 as an offset, so:
 *   entry 1: 0x20, entry 2: 0x40, etc.
 */
#define VOLX_ENTRY(idx) (&VOLX_$TABLE_BASE[(idx)])

/*
 * Offset calculations used in assembly
 *
 * For a given vol_idx:
 *   offset = vol_idx << 5 (multiply by 32)
 *   ptr = base + offset
 *
 * Then accesses are at:
 *   ptr - 0x20 = dir_uid  (offset 0x00 in entry)
 *   ptr - 0x18 = lv_uid   (offset 0x08 in entry)
 *   ptr - 0x10 = parent_uid (offset 0x10 in entry)
 *   ptr - 0x08 = dev      (offset 0x18 in entry)
 *   ptr - 0x06 = bus      (offset 0x1A in entry)
 *   ptr - 0x04 = ctlr     (offset 0x1C in entry)
 *   ptr - 0x02 = lv_num   (offset 0x1E in entry)
 */

/*
 * Magic address constants from Ghidra disassembly
 *
 * These correspond to offsets from the table base (0xE82604):
 *   0xE825E4 = 0xE82604 + 0x20 - 0x20 = base (dir_uid of entry 1)
 *   0xE825E8 = base + 0x04 (dir_uid.low of entry 1)
 *   0xE825EC = base + 0x08 (lv_uid.high of entry 1)
 *   0xE825F0 = base + 0x0C (lv_uid.low of entry 1)
 *   0xE825F4 = base + 0x10 (parent_uid.high of entry 1)
 *   0xE825F8 = base + 0x14 (parent_uid.low of entry 1)
 *   0xE825FC = base + 0x18 (dev of entry 1)
 *   0xE825FE = base + 0x1A (bus of entry 1)
 *   0xE82600 = base + 0x1C (ctlr of entry 1)
 *   0xE82602 = base + 0x1E (lv_num of entry 1)
 *
 * Note: VFMT_$FORMATN and ERROR_$PRINT are misidentified labels in Ghidra
 * that actually point into the VOLX table.
 */
#if defined(M68K)
#define VOLX_DIR_UID_FIELD(idx)    (*(uid_t *)((char *)VOLX_$TABLE_BASE + ((idx) << 5)))
#define VOLX_LV_UID_FIELD(idx)     (*(uid_t *)((char *)VOLX_$TABLE_BASE + ((idx) << 5) + 0x08))
#define VOLX_PARENT_UID_FIELD(idx) (*(uid_t *)((char *)VOLX_$TABLE_BASE + ((idx) << 5) + 0x10))
#define VOLX_LV_NUM_FIELD(idx)     (*(int16_t *)((char *)VOLX_$TABLE_BASE + ((idx) << 5) + 0x1E))
#endif

#endif /* VOLX_INTERNAL_H */
