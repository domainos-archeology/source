/*
 * PROC2_$GET_BOOT_FLAGS - Return the boot flags word
 *
 * Re-emitted from the image (0x00E41B56..0x00E41B70, 28 bytes).
 *
 *   00e41b5c  lea (0xe7be84).l,A5
 *   00e41b62  movea.l (0x8,A6),A0        ; flags_ret
 *   00e41b66  move.w (0x1e4,A5),(A0)     ; PROC2_$UNWIRED_DATA.boot_flags (0xE7C068)
 *
 * Only reference: the SVC table entry at 0x00E7B43E.
 *
 * Original address: 0x00e41b56
 */

#include "proc2/proc2_internal.h"

void PROC2_$GET_BOOT_FLAGS(int16_t *flags_ret)
{
    *flags_ret = PROC2_$UNWIRED_DATA.boot_flags;      /* 0x00E41B66 */
}
