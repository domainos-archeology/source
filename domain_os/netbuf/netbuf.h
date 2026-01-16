/*
 * NETBUF - Network Buffer Management
 *
 * This module manages network data buffers for remote operations.
 */

#ifndef NETBUF_H
#define NETBUF_H

#include "base/base.h"

/*
 * NETBUF_$RTN_DAT - Return a network data buffer
 *
 * @param addr  Buffer address to return
 */
void NETBUF_$RTN_DAT(uint32_t addr);

/*
 * NETBUF_$GET_DAT - Get a network data buffer
 *
 * @param addr  Output pointer for buffer address
 */
void NETBUF_$GET_DAT(uint32_t *addr);

/*
 * NETBUF_$GETVA - Get virtual address for network buffer
 *
 * Maps a physical page number to a virtual address for network I/O.
 *
 * @param ppn_shifted  Physical page number shifted left by 10 (ppn << 10)
 * @param va_out       Output pointer for virtual address
 * @param status       Output status code
 *
 * Original address: 0x00E0EC78
 */
void NETBUF_$GETVA(uint32_t ppn_shifted, uint32_t *va_out, status_$t *status);

/*
 * NETBUF_$RTNVA - Return virtual address for network buffer
 *
 * Unmaps a virtual address previously obtained from NETBUF_$GETVA.
 *
 * @param va_ptr  Pointer to virtual address to return
 *
 * @return Physical page number (shifted by 10)
 *
 * Original address: 0x00E0ED26
 */
uint32_t NETBUF_$RTNVA(uint32_t *va_ptr);

/*
 * NETBUF_$RTN_HDR - Return a network header buffer
 *
 * Returns a header buffer to the pool.
 *
 * @param ppn_ptr  Pointer to physical page number (shifted by 10)
 *
 * Original address: 0x00E0EEB4
 */
void NETBUF_$RTN_HDR(uint32_t *ppn_ptr);

#endif /* NETBUF_H */
