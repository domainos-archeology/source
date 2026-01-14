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

#endif /* NETBUF_H */
