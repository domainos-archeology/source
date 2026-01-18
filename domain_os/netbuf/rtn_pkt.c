/*
 * NETBUF_$RTN_PKT - Return a complete packet's buffers
 *
 * Convenience function to return header, VA, and data buffers.
 *
 * Original address: 0x00E0F0C6
 *
 * This function handles returning all the buffers associated with a packet:
 * - Header buffer (if non-zero)
 * - VA buffer (if non-zero)
 * - Data buffers (looped based on total data length)
 *
 * The dat_len parameter is the total data length. Data buffers are 1KB each,
 * so we iterate through dat_arr returning one buffer per 1KB (0x400 bytes).
 */

#include "netbuf/netbuf_internal.h"

void NETBUF_$RTN_PKT(uint32_t *hdr_ptr, uint32_t *va_ptr,
                     uint32_t *dat_arr, int16_t dat_len)
{
    /* Return header buffer if present */
    if (*hdr_ptr != 0) {
        NETBUF_$RTN_HDR(hdr_ptr);
    }

    /* Return VA buffer if present */
    if (*va_ptr != 0) {
        NETBUF_$RTNVA(va_ptr);
    }

    /* Return data buffers
     * Loop while dat_len > 0, decrementing by 0x400 (1KB) each iteration
     */
    while (dat_len > 0) {
        NETBUF_$RTN_DAT(*dat_arr);
        dat_arr++;
        dat_len -= 0x400;
    }
}
