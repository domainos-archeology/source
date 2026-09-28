/*
 * MMAP_$ALLOC_FREE - Allocate pages from the global free pool
 *
 * Original address: 0x00E0D870 (128 bytes; `E0D870 MMAP_$ALLOC_FREE` in the
 * SAU2 map).  Callers: 0x00E00D72, 0x00E30AC6.
 *
 * Frame (0x00E0D870-0x00E0D87E): `link.w A6,#-8`, D2/A5 saved, A5 = the
 * MMAP_ block (0xE23284).  Arguments, (0x8,A6) being argument 1:
 *   (0x8,A6)  vpn_array  longword pointer, handed straight on to
 *                        mmap_$alloc_pages_from_wsl
 *   (0xC,A6)  count      word (D2)
 *
 * Pascal function: the result is D0.w.
 *
 * 0x00E0D882-0x00E0D88C  token = ML_$SPIN_LOCK(&MMAP_GLOBALS.lock), kept
 *                        in (-2,A6)
 * 0x00E0D890-0x00E0D8A4  if MMAP_$WSL[0].page_count (0x30,A5 = 0xE232B4)
 *                        is zero: unlock and return 0
 * 0x00E0D8A6-0x00E0D8B4  D2 = min(count, page_count) - `andi.l #0xffff,D2`
 *                        zero-extends the word, `cmp.l/bls` keeps count when
 *                        count <= page_count, else D2 = page_count
 * 0x00E0D8B6-0x00E0D8C6  mmap_$alloc_pages_from_wsl(&MMAP_$WSL[0],
 *                        vpn_array, (word)D2)
 * 0x00E0D8CA-0x00E0D8D6  ML_$SPIN_UNLOCK(&MMAP_GLOBALS.lock, token)
 * 0x00E0D8D8-0x00E0D8E4  MMAP_$ALLOC_CNT++ (0x28,A5); MMAP_$ALLOC_PAGES
 *                        (0x24,A5) += (word)D2; result = (word)D2
 *
 * The counter updates at 0x00E0D8D8 happen after the lock is dropped, as
 * in the image.
 */

#include "mmap/mmap_internal.h"

uint16_t MMAP_$ALLOC_FREE(uint32_t *vpn_array, uint16_t count)
{
    ml_$spin_token_t token;
    uint32_t to_alloc;   /* D2 */

    token = ML_$SPIN_LOCK(&MMAP_GLOBALS.lock);              /* 0x00E0D882 */

    if (MMAP_$WSL[MMAP_WSL_POOL_FREE].page_count == 0) {    /* 0x00E0D890 */
        ML_$SPIN_UNLOCK(&MMAP_GLOBALS.lock, token);         /* 0x00E0D896 */
        return 0;                                            /* 0x00E0D8A2 */
    }

    /* 0x00E0D8A6-0x00E0D8B4: clamp the request to the pool size */
    to_alloc = count;
    if (to_alloc > MMAP_$WSL[MMAP_WSL_POOL_FREE].page_count) {
        to_alloc = MMAP_$WSL[MMAP_WSL_POOL_FREE].page_count;
    }

    /* 0x00E0D8B6-0x00E0D8C6 */
    mmap_$alloc_pages_from_wsl(&MMAP_$WSL[MMAP_WSL_POOL_FREE], vpn_array,
                               (uint16_t)to_alloc);

    ML_$SPIN_UNLOCK(&MMAP_GLOBALS.lock, token);             /* 0x00E0D8CA */

    MMAP_$ALLOC_CNT++;                                       /* 0x00E0D8D8 */
    MMAP_$ALLOC_PAGES += (uint16_t)to_alloc;                 /* 0x00E0D8DC */

    return (uint16_t)to_alloc;                               /* 0x00E0D8E4 */
}
