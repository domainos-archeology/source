/*
 * SOCK - Global Data
 *
 * Module data block SOCK_$DATA: Claude Opus 5.5 (source-gy7x).
 *
 * SOCK_$DATA, map "D E27510 SOCK size = 1C28": the A5 block of every SOCK
 * routine (layout, biases and asserts in sock/sock.h), a MODULE_DATA block
 * linked in the map's order after GPU_ASM and before TESTPAGE.  The address
 * is the ordering key, not the link address.
 */

#include "sock/sock_internal.h"

/*
 * Image contents, `gsk read 0xE27510 7208`: every byte is zero except
 *
 *   0x00E29134  +0x1C24  00 40      user_limit = 64
 *
 * The descriptors, the free list, the lock and the pointer table are all
 * built at run time by SOCK_$INIT (0x00E2FDF0).
 */
MODULE_DATA_DEFINE_INIT(sock_$data_t, SOCK_$DATA, 0x00E27510, {
    .user_limit = 0x0040,
});
