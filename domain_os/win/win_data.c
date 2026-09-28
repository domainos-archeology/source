/*
 * win/win_data.c - the WIN module's constant cells
 *
 * The 0x78-byte module block itself (`D E2B89C WIN_ size = 78`) is reached
 * through WIN_DATA_BASE (a fixed address on the target, an array the host
 * tests supply); only the `pea (d,PC)` cells of the code region live here.
 */

#include "win/win_internal.h"

/*
 * 0x00E1940A: 00 00 - the word every ANSI command in this module hands
 * WIN_$ANSI_COMMAND as its input-parameter address (gsk read 0xe19408:
 * 4e 75 | 00 00 | 00 08 00 04 | 00 08 00 22).  Reached as
 *   pea (0xce,PC)   0x00E1933A  WIN_$CHECK_DISK_STATUS (0x0F)
 *   pea (0x7a,PC)   0x00E1938E  WIN_$CHECK_DISK_STATUS (clearing command)
 *   pea (0x5a,PC)   0x00E193AE  WIN_$CHECK_DISK_STATUS (0x01)
 *   pea (-0x10e,PC) 0x00E19516  win_$reinit_drive (0x04)
 *   pea (-0x7c8,PC) 0x00E19BD0  WIN_$SPIN_DOWN (0x55)
 * Only SPIN CONTROL (>= 0x40) actually reads the byte, which is 0.
 */
uint16_t WIN_ANSI_IN_PARAM = 0x0000;
