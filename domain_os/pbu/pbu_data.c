/*
 * pbu/pbu_data.c - PBU subsystem global data definitions
 *
 * The PBU subsystem owns no storage of its own.  Its one data object,
 * PBU_$EC_ARRAY at 0x00E88460, is the typed view of ec/'s EC2_$PBU_ECS: the
 * SAU2 map has a single segment there ("D86 E88460 PBU_WIRED_DATA loaded at
 * 189C60, size = 300" / "D E88460 EC2_PBU size = 300") whose only interior
 * symbol is EC2_$PBU_ECS and which ends at PBU_$DATA_END (0x00E88760).
 * 0x300 bytes is PBU_EC_COUNT (32) records of sizeof(pbu_ec_entry_t) (0x18),
 * which is how PBU_$ADVANCE_EC_INT indexes it: eventcount ids 0x101..0x120
 * map onto records 0..31.  The region is zero-filled in the image.
 *
 * The definition lives in ec/ec_data.c; pbu/pbu.h aliases PBU_$EC_ARRAY onto
 * it so the image byte range has exactly one C object.
 */

#include "pbu/pbu_internal.h"
