/*
 * dbuf/dbuf_data.c - DBUF module data
 *
 * The DBUF module data block is `D E78B58 DBUF size = 920` in the SAU2 map.
 * Every DBUF_ routine loads 0xE78B58 into A5 (DBUF_$INIT does so at
 * 0x00E3ABE2), and DBUF_$INIT writes each of the cells below; see
 * dbuf/dbuf_internal.h for the displacement-by-displacement derivation.
 *
 * The whole block is zero in the image - DBUF_$INIT builds the free list and
 * the counts at boot - so every definition here starts zeroed.
 */

#include "dbuf/dbuf_internal.h"

/*
 * dbuf_$eventcount - advanced whenever a buffer's reference count drops to
 * zero, waited on by DBUF_$GET_BLOCK.  EC_$INIT is called on it with
 * `pea (A5)` at 0x00E3AD08, so it sits at block + 0.
 *
 * Original address: 0xE78B58
 */
ec_$eventcount_t dbuf_$eventcount;

/*
 * DBUF - the buffer entry array, block + 0x10.  64 entries of 0x24 bytes fill
 * the block exactly up to DBUF_SPIN_LOCK at block + 0x910; DBUF_$INIT threads
 * the first dbuf_$count of them into the LRU list (0x00E3AC36-0x00E3ACE4).
 * The SAU2 map names the array's first entry DBUF.
 *
 * Original address: 0xE78B68
 */
dbuf_$entry_t DBUF[DBUF_MAX_BUFFERS];

/*
 * DBUF_SPIN_LOCK - guards the LRU list and dbuf_$waiters, block + 0x910.
 * DBUF_$SET_BUFF releases it with `pea (0x910,A5)` at 0x00E3A9C4.
 *
 * Original address: 0xE79468
 */
uint32_t DBUF_SPIN_LOCK;

/*
 * dbuf_$head - head of the LRU list, block + 0x914.  DBUF_$INIT points it at
 * DBUF[0] with `lea (0x10,A5),A3 / move.l A3,(0x914,A5)` (0x00E3ACFE).
 *
 * Original address: 0xE7946C
 */
uint32_t dbuf_$head;

/*
 * dbuf_$waiters - count of processes blocked in DBUF_$GET_BLOCK,
 * block + 0x918.  `clr.w (0x918,A5)` at 0x00E3AD0E.
 *
 * Original address: 0xE79470
 */
uint16_t dbuf_$waiters;

/*
 * dbuf_$count - buffers actually in the pool, block + 0x91A.  DBUF_$INIT
 * derives it from MMAP_$REAL_PAGES and clamps it to
 * [DBUF_MIN_BUFFERS, DBUF_MAX_BUFFERS] (0x00E3ABF4-0x00E3AC0E).
 *
 * Original address: 0xE79472
 */
uint16_t dbuf_$count;

/*
 * DBUF_$TROUBLE - one bit per volume, set when a writeback for that volume
 * failed.  Block + 0x91C, `clr.w (0x91c,A5)` at 0x00E3AD12.  Named by the
 * SAU2 map.
 *
 * Original address: 0xE79474
 */
uint16_t DBUF_$TROUBLE;

/*
 * ============================================================================
 * In-code status constants (literals inside DBUF_$SET_BUFF, 0xE3A8B6)
 * ============================================================================
 */

/*
 * OS_DBUF_bad_free_err - DBUF_$SET_BUFF's crash status when DBUF_FLAG_RELEASE
 * finds the entry's reference count already zero.  Reached by
 * `pea (0x52,PC)` at 0x00E3A990.  Image bytes at 0x00E3A9E4: 00 0c 00 02.
 *
 * Original address: 0xE3A9E4
 */
const status_$t OS_DBUF_bad_free_err = 0x000C0002;

/*
 * OS_DBUF_bad_ptr_err - DBUF_$SET_BUFF's crash status when the caller's
 * buffer pointer matches no entry.  Reached by `pea (0x16,PC)` at 0x00E3A9D0.
 * Image bytes at 0x00E3A9E8: 00 0c 00 01.
 *
 * Original address: 0xE3A9E8
 */
const status_$t OS_DBUF_bad_ptr_err = 0x000C0001;
