/*
 * rem_name/rem_name_data.c - REM_NAME module data (SAU2 map, D 0xE7DBB8 size 0x40)
 *
 * Split out of the single name/rem_name.c (bead source-ev4k); the
 * initialiser below is unchanged.
 */

#include "rem_name/rem_name_internal.h"

/*
 * REM_NAME data area - complete structure at 0xE7DBB8 (rem_name_data_t is
 * declared in name/name_internal.h).  Initial contents from the image:
 *   00 10 00 02 00 02 80 31 ff ff 00 00 ff ff 00 00   config[0..7]
 *   00 .. 00                                          config[8..14], reserved1
 *   00 00 09 60  00 00 04 b0  00 00 00 00  80 00 00 00
 *   00 00 00 00  00 00 00 00  00 10  00 00  00
 */
rem_name_data_t rem_name_$data = {
    .config = { 0x0010, 0x0002, 0x0002, 0x8031, 0xFFFF, 0x0000, 0xFFFF, 0x0000,
                0, 0, 0, 0, 0, 0, 0 },
    .reserved1 = 0,
    .server_timeout = 0x00000960,
    .reserved2 = 0x000004B0,
    .time_heard_from_server = 0,
    .last_status = 0x80000000,
    .curr_node = 0,
    .curr_net = 0,
    .service_delay = 0x0010,
    .retry_count = 0,
    .heard_from_server = 0,
};
