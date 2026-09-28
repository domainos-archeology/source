/*
 * MMAP_$REMOTE_POOL - Remote page-pool policy (identity stub)
 *
 * Original address: 0x00E59110 (12 bytes; `E59110 MMAP_$REMOTE_POOL`, the
 * only symbol of the second `MMAP_UNWIRED size = C` segment).  Callers:
 * 0x00E0F520, 0x00E115B4, 0x00E11946.
 *
 * 0x00E59110-0x00E5911A: `link.w A6,#-0xC`, `move.l (0x8,A6),D0`, `unlk`,
 * `rts` - the one longword argument is returned unchanged.
 */

#include "mmap/mmap_internal.h"

uint32_t MMAP_$REMOTE_POOL(uint32_t param)
{
    return param;                                            /* 0x00E59114 */
}
