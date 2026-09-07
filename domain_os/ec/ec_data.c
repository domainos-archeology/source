/*
 * ec/ec_data.c - EC subsystem data cells
 *
 * The EC2 module data block is `D E7C06C EC2 size = EA0` in the SAU2 map; the
 * two eventcount pools are separate segments (`D E20F6C EC2_ASM size = 300`
 * and `D E88460 EC2_PBU size = 300`).  All of it is zero in the image -
 * EC2_$INIT_S (0x00E30970) builds the waiter free list, the registration
 * table and the bitmaps at boot - so every definition here starts zeroed.
 *
 * See ec/ec.h and ec/ec_internal.h for the displacement-by-displacement
 * derivation of each object's base, stride and count.
 */

#include "ec/ec_internal.h"

/*
 * EC2_$WAIT_ECS - the 64 per-process level-1 eventcounts EC2_$WAIT blocks on.
 *
 * Original address: 0xE20F6C (0x300 bytes)
 */
uint8_t EC2_$WAIT_ECS[EC2_WAIT_ECS_COUNT * EC2_WAIT_EC_SIZE];

/*
 * EC2_WAITER_TABLE_BASE - the 225 EC2 waiter records, EC2 block + 0.
 *
 * Original address: 0xE7C06C (0xA84 bytes)
 */
ec2_waiter_t EC2_WAITER_TABLE_BASE[EC2_WAITER_TABLE_SLOTS];

/*
 * DAT_00e7caf0 - registration count, EC2 block + 0xA84.
 *
 * Original address: 0xE7CAF0
 */
uint16_t DAT_00e7caf0;

/*
 * DAT_00e7caf8 - the EC1 registration table, EC2 block + 0xA8C.  Slot 1 is
 * DAT_00e7cafc (see ec/ec_internal.h).
 *
 * Original address: 0xE7CAF8 (0x404 bytes)
 */
void *DAT_00e7caf8[EC2_REGISTERED_EC1_SLOTS];

/*
 * DAT_00e7cefc - PBU-pool pending-release bitmap, EC2 block + 0xE90.
 *
 * Original address: 0xE7CEFC
 */
uint32_t DAT_00e7cefc;

/*
 * DAT_00e7cf00 - PBU-pool allocation bitmap, EC2 block + 0xE94.
 *
 * Original address: 0xE7CF00
 */
uint32_t DAT_00e7cf00;

/*
 * _DAT_00e7cf04 - highest registered EC1 index, EC2 block + 0xE98.
 *
 * Original address: 0xE7CF04
 */
uint32_t _DAT_00e7cf04;

/*
 * DAT_00e7cf06 - registration-scan bound, EC2 block + 0xE9A.
 *
 * Original address: 0xE7CF06
 */
uint16_t DAT_00e7cf06;

/*
 * DAT_00e7cf08 - EC2 waiter free-list head, EC2 block + 0xE9C.
 *
 * Original address: 0xE7CF08
 */
uint16_t DAT_00e7cf08;

/*
 * EC2_$PBU_ECS - the 32-record pool of level-1 eventcounts handed out as EC2
 * indices 0x101..0x120.  Zero in the image (it is wired data, `D86 E88460
 * PBU_WIRED_DATA loaded at 189C60, size = 300`).
 *
 * Original address: 0xE88460 (0x300 bytes)
 */
uint8_t EC2_$PBU_ECS[EC2_PBU_EC_COUNT * EC2_PBU_EC_SIZE];
