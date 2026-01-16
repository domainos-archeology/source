/*
 * PKT_$DUMP_DATA - Release packet data buffers
 *
 * Returns data buffers allocated by PKT_$COPY_TO_PA back to the pool.
 * Each buffer holds up to 0x400 (1024) bytes, so we release buffers
 * until we've accounted for all the data length.
 *
 * The assembly shows:
 * 1. If first buffer is 0, return immediately
 * 2. Loop up to 4 times (D2 = 3, counting down)
 * 3. For each iteration, check if (uVar2 - 1) * 0x400 >= len
 *    If so, we've released enough buffers
 * 4. Call NETBUF_$RTN_DAT for each buffer
 *
 * Original address: 0x00E127E6
 */

#include "pkt/pkt_internal.h"

void PKT_$DUMP_DATA(uint32_t *buffers, int16_t len)
{
    int16_t i;
    uint16_t buf_num;

    /* If no buffers allocated, nothing to do */
    if (buffers[0] == 0) {
        return;
    }

    /*
     * Release buffers until we've accounted for all data.
     * Each buffer holds PKT_CHUNK_SIZE (0x400) bytes.
     * Loop up to 4 times (indices 0-3).
     */
    buf_num = 1;
    for (i = 3; i >= 0; i--) {
        /* Check if we've released enough buffers */
        /* (buf_num - 1) * 0x400 is the offset of the current buffer */
        if ((int32_t)len <= (int32_t)((buf_num - 1) * PKT_CHUNK_SIZE)) {
            return;
        }

        /* Return this buffer to the pool */
        NETBUF_$RTN_DAT(buffers[buf_num - 1]);

        buf_num++;
    }
}
