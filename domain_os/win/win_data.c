/*
 * win/win_data.c - the WIN module's constant cells
 *
 * The 0x78-byte module block (`D E2B89C WIN_ size = 78`, reached through
 * WIN_DATA_BASE) and the `pea (d,PC)` cells of the code region.
 */

#include "win/win_internal.h"

/*
 * WIN_$DATA, 0x00E2B89C..0x00E2B913 (`gsk read 0xE2B89C 0x78`), non-zero
 * cells only:
 *
 *   +0x10  00 e1 9b c0  00 00 00 00  00 e1 9c e8  00 00 00 00
 *   +0x20  00 e1 97 76  00 e1 9d 54  00 e1 9d 62  00 00 00 00
 *          spin_down = WIN_$SPIN_DOWN, shutdown none, dinit = WIN_$DINIT,
 *          do_io = WIN_$DO_IO, error_que = WIN_$ERROR_QUE,
 *          get_stats = WIN_$GET_STATS
 *   +0x68  00 00 00 01
 *   +0x72  ff ff  ff ff    current head, current cylinder
 */
MODULE_DATA_DEFINE_INIT(win_$data_t, WIN_$DATA, 0x00E2B89C, {
    .image = {
        .jump_table = {
            ARCH_PTR_TO_VA_STATIC(WIN_$SPIN_DOWN, 0x00E19BC0),
            0,
            ARCH_PTR_TO_VA_STATIC(WIN_$DINIT, 0x00E19CE8),
            0,
            ARCH_PTR_TO_VA_STATIC(WIN_$DO_IO, 0x00E19776),
            ARCH_PTR_TO_VA_STATIC(WIN_$ERROR_QUE, 0x00E19D54),
            ARCH_PTR_TO_VA_STATIC(WIN_$GET_STATS, 0x00E19D62),
            0,
        },
        ._unknown_68 = 1,
        .cur_head = -1,
        .cur_cyl  = -1,
    },
});

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
