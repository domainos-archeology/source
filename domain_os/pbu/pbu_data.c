/*
 * pbu/pbu_data.c - PBU subsystem global data definitions
 *
 * Original M68K address:
 *   PBU_$EC_ARRAY  0x00E88460, 0x300 bytes
 */

#include "pbu/pbu_internal.h"

/*
 * PBU_$EC_ARRAY - the pool of eventcounts PBU units advance.
 *
 * The SAU2 map has the storage as "D86 E88460 PBU_WIRED_DATA loaded at
 * 189C60, size = 300" / "D E88460 EC2_PBU size = 300", with the one interior
 * symbol EC2_$PBU_ECS at 0x00E88460 and PBU_$DATA_END at 0x00E88760.  0x300
 * bytes is exactly PBU_EC_COUNT (32) entries of sizeof(pbu_ec_entry_t)
 * (0x18), which is how PBU_$ADVANCE_EC_INT indexes it: eventcount ids
 * 0x101..0x120 map onto entries 0..31.  The region is zero-filled in the
 * image.
 *
 * TODO(source-wk2f, 0x00E88460): the tree names this one object three ways --
 * PBU_$EC_ARRAY here, and EC2_$PBU_ECS / EC2_PBU_ECS_BASE in ec/ec.h.  The
 * map name is EC2_$PBU_ECS; the three should collapse onto it, but ec/ is
 * being worked separately, so the alias is kept for now.
 */
pbu_ec_entry_t PBU_$EC_ARRAY[PBU_EC_COUNT] = { 0 };
#if defined(ARCH_M68K)
_Static_assert(sizeof(PBU_$EC_ARRAY) == 0x300,
               "PBU_$EC_ARRAY: map segment EC2_PBU 0x00E88460 size = 300");
#endif
