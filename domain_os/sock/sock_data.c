/*
 * SOCK - Global Data
 *
 * This file contains global data definitions for the SOCK subsystem.
 *
 * Original addresses (m68k):
 *   - Socket table base:     0xE27510
 *   - Free list head:        0xE2751C (base + 0x0C)
 *   - Socket descriptors:    0xE2752C (base + 0x1C) - 224 sockets * 0x1C bytes
 *   - Spinlock:              0xE28DB0 (base + 0x18A0)
 *   - Pointer array:         0xE28DB4 (base + 0x18A4) - 224 pointers
 *   - User socket limit:     0xE29134 (base + 0x1C24)
 */

#include "sock/sock_internal.h"

/*
 * Socket Table Memory Layout:
 *
 * The socket table is a contiguous block of memory with the following layout:
 *   +0x0000: Header area (28 bytes)
 *     +0x0C: Free list head pointer
 *   +0x001C: Socket descriptor array (224 * 28 = 6272 bytes)
 *   +0x18A0: Spinlock (4 bytes, reuses socket 0 pointer slot)
 *   +0x18A4: Pointer array (224 * 4 = 896 bytes)
 *   +0x1C24: User socket limit counter (2 bytes)
 *
 * Total size: approximately 0x1C26 bytes (SOCK_TABLE_SIZE rounds up).
 *
 * SOCK_$EVENT_COUNTERS (0xE28DB4) is an alias into this table; see sock.h.
 */
uint8_t sock_table_base[SOCK_TABLE_SIZE];
