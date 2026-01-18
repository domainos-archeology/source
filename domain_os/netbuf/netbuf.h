/*
 * NETBUF - Network Buffer Management
 *
 * This module manages network buffers for the Domain/OS kernel. It provides
 * two types of buffers:
 *
 * 1. Header Buffers (HDR): 1KB buffers for packet headers
 *    - Managed via a free list linked through offset 0x3e4 in each buffer
 *    - Each buffer contains the physical address at offset 0x3fc
 *    - Allocated via NETBUF_$GET_HDR, returned via NETBUF_$RTN_HDR
 *
 * 2. Data Buffers (DAT): Page-sized buffers for packet data
 *    - Tracked by page number, with free list stored in MMAPE next_vpn field
 *    - Allocated via NETBUF_$GET_DAT, returned via NETBUF_$RTN_DAT
 *
 * Virtual Address Mapping:
 *    - VA slots (192 max) map physical pages to virtual addresses
 *    - VA base address: 0xD64C00
 *    - Each slot is 1KB (0x400 bytes)
 *
 * Key data structures at 0xE245A8:
 *    - VA slot array (192 entries)
 *    - Spin lock, counters, free list heads
 *
 * Original source was likely Pascal, converted to C.
 */

#ifndef NETBUF_H
#define NETBUF_H

#include "base/base.h"

/* Maximum number of VA slots */
#define NETBUF_VA_SLOTS         192     /* 0xC0 */

/* Maximum header buffers that can be allocated */
#define NETBUF_HDR_MAX          176     /* 0xB0 */

/* Header buffer size */
#define NETBUF_HDR_SIZE         1024    /* 0x400 bytes */

/* Maximum pages to allocate at once */
#define NETBUF_MAX_ALLOC        128     /* 0x80 */

/* Header buffer offsets */
#define NETBUF_HDR_NEXT_OFF     0x3e4   /* Next pointer in free list */
#define NETBUF_HDR_DATA_OFF     0x3e8   /* Start of data area (zeroed on init) */
#define NETBUF_HDR_PHYS_OFF     0x3fc   /* Physical address storage */

/* Status codes */
#define status_$network_out_of_blocks   0x00110003

/*
 * ============================================================================
 * Public Function Prototypes
 * ============================================================================
 */

/*
 * NETBUF_$INIT - Initialize network buffer subsystem
 *
 * Initializes the VA slot free list and allocates initial buffers.
 * Called during kernel initialization.
 *
 * Original address: 0x00E2F630
 */
void NETBUF_$INIT(void);

/*
 * NETBUF_$ADD_PAGES - Add pages to buffer pools
 *
 * Allocates physical pages and adds them to the header and data buffer pools.
 *
 * @param counts     Packed counts: high word = header count, low word = data count
 *
 * Original address: 0x00E0E928
 */
void NETBUF_$ADD_PAGES(uint32_t counts);

/*
 * NETBUF_$DEL_PAGES - Delete pages from buffer pools
 *
 * Removes pages from the header and data buffer pools and frees them.
 *
 * @param hdr_count  Number of header buffers to remove
 * @param dat_count  Number of data buffers to remove
 *
 * Original address: 0x00E0EB26
 */
void NETBUF_$DEL_PAGES(int16_t hdr_count, int16_t dat_count);

/*
 * NETBUF_$GETVA - Get virtual address for network buffer
 *
 * Maps a physical page to a virtual address in the network buffer space.
 * Uses the VA slot array to track mappings.
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
 * Returns the VA slot to the free list.
 *
 * @param va_ptr  Pointer to virtual address to return
 *
 * @return Physical page number shifted by 10
 *
 * Original address: 0x00E0ED26
 */
uint32_t NETBUF_$RTNVA(uint32_t *va_ptr);

/*
 * NETBUF_$GET_HDR_COND - Conditionally get a header buffer
 *
 * Attempts to get a header buffer without blocking.
 *
 * @param phys_out  Output pointer for physical address
 * @param va_out    Output pointer for virtual address
 *
 * @return Non-zero if buffer was obtained, 0 if pool is empty
 *
 * Original address: 0x00E0ED6C
 */
int8_t NETBUF_$GET_HDR_COND(uint32_t *phys_out, uint32_t *va_out);

/*
 * NETBUF_$GET_HDR - Get a header buffer (blocking)
 *
 * Gets a header buffer, blocking if none are available. If running
 * as a type-7 process, waits on the delay queue. Otherwise allocates
 * a new buffer.
 *
 * @param phys_out  Output pointer for physical address
 * @param va_out    Output pointer for virtual address
 *
 * Original address: 0x00E0EDD6
 */
void NETBUF_$GET_HDR(uint32_t *phys_out, uint32_t *va_out);

/*
 * NETBUF_$RTN_HDR - Return a header buffer
 *
 * Returns a header buffer to the free pool.
 *
 * @param va_ptr  Pointer to virtual address to return
 *
 * Original address: 0x00E0EEB4
 */
void NETBUF_$RTN_HDR(uint32_t *va_ptr);

/*
 * NETBUF_$GET_DAT_COND - Conditionally get a data buffer
 *
 * Attempts to get a data buffer without blocking.
 *
 * @param addr_out  Output pointer for buffer address (ppn << 10)
 *
 * @return Non-zero if buffer was obtained, 0 if pool is empty
 *
 * Original address: 0x00E0EF28
 */
int8_t NETBUF_$GET_DAT_COND(uint32_t *addr_out);

/*
 * NETBUF_$GET_DAT - Get a data buffer (blocking)
 *
 * Gets a data buffer, blocking if none are available. If running
 * as a type-7 process, waits on the delay queue. Otherwise allocates
 * a new page.
 *
 * @param addr_out  Output pointer for buffer address (ppn << 10)
 *
 * Original address: 0x00E0EFA4
 */
void NETBUF_$GET_DAT(uint32_t *addr_out);

/*
 * NETBUF_$RTN_DAT - Return a data buffer
 *
 * Returns a data buffer to the pool if under limit, otherwise frees it.
 *
 * @param addr  Buffer address (ppn << 10)
 *
 * Original address: 0x00E0F046
 */
void NETBUF_$RTN_DAT(uint32_t addr);

/*
 * NETBUF_$RTN_PKT - Return a complete packet's buffers
 *
 * Convenience function to return header, VA, and data buffers.
 *
 * @param hdr_ptr   Pointer to header VA (may be 0)
 * @param va_ptr    Pointer to VA buffer (may be 0)
 * @param dat_arr   Array of data buffer addresses
 * @param dat_len   Total data length (determines number of data buffers)
 *
 * Original address: 0x00E0F0C6
 */
void NETBUF_$RTN_PKT(uint32_t *hdr_ptr, uint32_t *va_ptr,
                     uint32_t *dat_arr, int16_t dat_len);

#endif /* NETBUF_H */
