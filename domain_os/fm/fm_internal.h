/*
 * FM Internal Header
 *
 * Internal data structures and declarations for the File Map subsystem.
 * This header should only be included by FM implementation files.
 */

#ifndef FM_INTERNAL_H
#define FM_INTERNAL_H

#include "fm/fm.h"
#include "ml/ml.h"
#include "dbuf/dbuf.h"
#include "vtoc/vtoc_internal.h"

/*
 * Lock ID for disk operations
 * Shared with VTOC and other disk subsystems
 */
#define FM_LOCK_ID              0x10

/*
 * Buffer release flags (passed to DBUF_$SET_BUFF)
 */
#define FM_BUF_RELEASE          0x08    /* Release buffer without write */
#define FM_BUF_DIRTY            0x09    /* Mark dirty, release */
#define FM_BUF_WRITEBACK        0x0B    /* Write back immediately, release */

/*
 * File map entry sizes and offsets
 */
#define FM_ENTRY_SIZE           0x80    /* 128 bytes per file map entry */
#define FM_ENTRIES_PER_BLOCK    8       /* 8 entries per 1024-byte block */

/*
 * VTOCE file map area offsets
 * Where the direct block pointers are stored within a VTOCE
 */
#define FM_VTOCE_NEW_OFFSET     0xD8    /* Offset in new format VTOCE (0x150 bytes) */
#define FM_VTOCE_OLD_OFFSET     0x44    /* Offset in old format VTOCE (0xCC bytes) */

/*
 * VTOCE entry sizes
 */
#define FM_VTOCE_NEW_SIZE       0x150   /* 336 bytes - new format */
#define FM_VTOCE_OLD_SIZE       0xCC    /* 204 bytes - old format */

/*
 * Status codes
 */
#define status_$VTOC_not_mounted    0x20001     /* VTOC not mounted */

/*
 * Address extraction macros
 */
#define FM_BLOCK_NUMBER(addr)   ((addr) >> 4)           /* Extract block number */
#define FM_ENTRY_INDEX(addr)    ((addr) & 0x0F)         /* Extract entry index (0-15) */

/*
 * External references to global data
 *
 * Volume mount status and format flags are in VTOC data area.
 * Base address: 0xE784D0
 */

/* DAT_00e78747 = vtoc_$data.mounted - mount status byte array */
/* DAT_00e7874f = vtoc_$data.format - format flag byte array */

/*
 * VTOC cache lookup tracking (for write-protected optimization)
 * Located at base + 0x26f + vol_idx
 * When bit 7 is set, the volume has cached lookup data
 */
extern int8_t VTOC_CACH_LOOKUPS[];  /* 0xE7873F: Cache lookup flags */

#endif /* FM_INTERNAL_H */
