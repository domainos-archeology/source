/*
 * EC Internal Header
 *
 * Internal data structures and globals for the EC subsystem.
 * This header should only be included by ec/ source files.
 */

#ifndef EC_INTERNAL_H
#define EC_INTERNAL_H

#include "ec/ec.h"
#include "as/as.h"   /* AS_$PROTECTION */

/*
 * ============================================================================
 * Internal Global Data (EC2 subsystem state)
 * ============================================================================
 */

/*
 * ============================================================================
 * EC2 module data block cells
 *
 * Every EC2_ routine loads 0xE7C06C into A5 (EC2_$REGISTER_EC1 at
 * 0x00E42944, EC2_$ALLOCATE_EC1 at 0x00E429E2), and the SAU2 map has
 * `D E7C06C EC2 size = EA0` - so the block runs 0xE7C06C..0xE7CF0C:
 *
 *   +0x000  EC2_WAITER_TABLE_BASE   ec2_waiter_t[225]   0xE7C06C
 *   +0xA84  DAT_00e7caf0            uint16_t            0xE7CAF0
 *   +0xA8C  DAT_00e7caf8            void *[257]         0xE7CAF8
 *   +0xE90  DAT_00e7cefc            uint32_t            0xE7CEFC
 *   +0xE94  DAT_00e7cf00            uint32_t            0xE7CF00
 *   +0xE98  _DAT_00e7cf04           uint32_t            0xE7CF04
 *   +0xE9A  DAT_00e7cf06            uint16_t            0xE7CF06
 *   +0xE9C  DAT_00e7cf08            uint16_t            0xE7CF08
 *
 * (+0xE9C..+0xEA0 is the block's trailing pad.)  The map names none of these,
 * so the tree keeps the DAT_ spellings.
 * ============================================================================
 */

/*
 * EC2 registration table: EC1 pointers indexed by registration id.
 *
 * Base 0xE7CAF8 (block + 0xA8C), stride 4: EC2_$REGISTER_EC1 stores with
 * `move.l (0x8,A6),(0xa8c,A0)` after `lsl.l #0x2,D0 / lea (0x0,A5,D0*0x1),A0`
 * (0x00E429A4-0x00E429AE) and refuses to grow past index 0x100
 * (`cmpi.l #0x100,(0xe98,A5)`, 0x00E4298A).  Indices 0..0x100 inclusive is
 * 257 slots of 4 bytes = 0x404, which reaches exactly to DAT_00e7cefc at
 * block + 0xE90.
 */
#define EC2_REGISTERED_EC1_SLOTS 257
extern void *DAT_00e7caf8[EC2_REGISTERED_EC1_SLOTS];

/*
 * Maximum registered EC1 index.  EC2_$INIT_S sets it to 1
 * (`moveq #0x1,D0 / move.l D0,(0xe98,A1)`, 0x00E309CC); EC2_$REGISTER_EC1
 * pre-increments it and stores the new EC1 at that slot.
 * Located at 0xE7CF04 (block + 0xE98).
 */
extern uint32_t _DAT_00e7cf04;

/*
 * Registration slot 1 is not an EC1 pointer: EC2_$INIT_S points it at
 * _DAT_00e7cf04 itself (`lea (0xe98,A1),A1 / move.l A1,(0xa90,A3)`,
 * 0x00E309D2-0x00E309DC), so EC2 index 1 reads back as an eventcount whose
 * value is the number of registered EC1s.  Block + 0xA90 == 0xE7CAFC is
 * DAT_00e7caf8[1], so it is not a separate object.
 */
#define DAT_00e7cafc (DAT_00e7caf8[1])

/*
 * Registration count.  Cleared by EC2_$INIT_S (`clr.w (0xa84,A1)`,
 * 0x00E309C2).
 * Located at 0xE7CAF0 (block + 0xA84).
 */
extern uint16_t DAT_00e7caf0;

/*
 * Number of registration slots EC2_$REGISTER_EC1 scans looking for an EC1 it
 * has already registered (`move.w (0xe9a,A5),D0w / subq.w #0x1,D0w`,
 * 0x00E42960).  EC2_$INIT_S does not write it.
 * Located at 0xE7CF06 (block + 0xE9A).
 */
extern uint16_t DAT_00e7cf06;

/*
 * EC2 waiter free-list head.  EC2_$INIT_S sets it to 1
 * (`move.w #0x1,(0xe9c,A1)`, 0x00E309C6).
 * Located at 0xE7CF08 (block + 0xE9C).
 */
extern uint16_t DAT_00e7cf08;

/*
 * EC2 PBU-pool allocation bitmap (one bit per EC2_$PBU_ECS record).  Cleared
 * by EC2_$INIT_S (`clr.l (0xe94,A3)`, 0x00E309E0).
 * Located at 0xE7CF00 (block + 0xE94).
 */
extern uint32_t DAT_00e7cf00;

/*
 * EC2 PBU-pool pending-release bitmap (released while waiters remain).
 * Cleared by EC2_$INIT_S (`clr.l (0xe90,A3)`, 0x00E309E4).
 * Located at 0xE7CEFC (block + 0xE90).
 */
extern uint32_t DAT_00e7cefc;

/*
 * ============================================================================
 * External References (from other subsystems)
 * ============================================================================
 */

#endif /* EC_INTERNAL_H */
