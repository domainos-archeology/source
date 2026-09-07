/*
 * SOCK - Socket Management Module (Internal Header)
 *
 * This module provides socket management for network communication in Domain/OS.
 * Sockets are used for inter-process communication and network protocols.
 *
 * Socket Number Allocation:
 *   - Sockets 0-31: Reserved for well-known services (statically allocated)
 *   - Sockets 32-223: Dynamically allocated from free list
 *   - Total: 224 sockets (0x00 - 0xDF)
 *
 * Memory Layout (base at 0xE27510):
 *   - Base + 0x00:    Socket table header
 *   - Base + 0x0C:    Free list head pointer
 *   - Base + 0x1C:    First socket descriptor
 *   - Base + 0x18A0:  Spinlock (reuses socket 0 pointer slot)
 *   - Base + 0x18A4:  Socket pointer array (sockets 1-223)
 *   - Base + 0x1C24:  User socket limit counter
 */

#ifndef SOCK_INTERNAL_H
#define SOCK_INTERNAL_H

#include "sock/sock.h"
#include "ec/ec.h"
#include "ml/ml.h"
#include "netbuf/netbuf.h"

/*
 * Socket Constants
 */
#define SOCK_MAX_SOCKETS        224     /* Total number of sockets (0x00-0xDF) */
#define SOCK_RESERVED_MIN       0       /* First reserved socket */
#define SOCK_RESERVED_MAX       31      /* Last reserved socket (well-known ports) */
#define SOCK_DYNAMIC_MIN        32      /* First dynamically allocatable socket */
#define SOCK_DYNAMIC_MAX        223     /* Last dynamically allocatable socket (0xDF) */

#define SOCK_DESC_SIZE          0x1C    /* Size of socket descriptor (28 bytes) */

/*
 * Socket Flags (at descriptor offset 0x16)
 *
 * The flags word encodes both status flags and the socket number:
 *   Bits 0-12:  Socket number (0x1FFF mask)
 *   Bit 13:     Socket allocated (SOCK_FLAG_ALLOCATED)
 *   Bit 14:     User-mode socket (SOCK_FLAG_USER_MODE)
 *   Bit 15:     Socket open/ready (SOCK_FLAG_OPEN)
 */
#define SOCK_FLAG_NUMBER_MASK   0x1FFF  /* Bits 0-12: socket number */
#define SOCK_FLAG_ALLOCATED     0x2000  /* Bit 13: socket is allocated */
#define SOCK_FLAG_USER_MODE     0x4000  /* Bit 14: user-mode socket */
#define SOCK_FLAG_OPEN          0x8000  /* Bit 15: socket is open */

/* Byte-level flag access (for bset/bclr instructions) */
#define SOCK_BFLAG_ALLOCATED    0x20    /* Bit 5 of high byte = bit 13 */
#define SOCK_BFLAG_USER_MODE    0x40    /* Bit 6 of high byte = bit 14 */
#define SOCK_BFLAG_OPEN         0x80    /* Bit 7 of high byte = bit 15 */

/*
 * Socket Table Offsets (relative to sock_table_base)
 */
#define SOCK_TABLE_FREE_LIST    0x0C    /* Offset to free list head */
#define SOCK_TABLE_FIRST_DESC   0x1C    /* Offset to first socket descriptor */
#define SOCK_TABLE_LOCK         0x18A0  /* Offset to spinlock */
#define SOCK_TABLE_PTR_ARRAY    0x18A0  /* Offset to pointer array (slot 0 = lock) */
#define SOCK_TABLE_USER_LIMIT   0x1C24  /* Offset to user socket limit counter */

/*
 * The socket descriptor record itself is sock_$sock_t in sock/sock.h; the
 * table entry for socket n is SOCK_GET_VIEW_PTR(n) (the pointer array slot
 * written by SOCK_$INIT at 0x00E2FE1A).  Descriptor slots are SOCK_DESC_SIZE
 * apart and the record starts 4 bytes into each slot (0x00E2FE16), so the
 * last four bytes of one record overlap the next slot's first four.
 */
#define SOCK_EC_VIEW_SIZE   0x1C

/*
 * Network Buffer Header Offsets (for packet queue operations)
 *
 * Network buffers are large (1KB pages) structures. The socket subsystem
 * uses offsets near the end of the first page for queue linkage and
 * packet metadata.
 */
#define NETBUF_OFFSET_HDR_PTR       0x3B8   /* Pointer to header */
#define NETBUF_OFFSET_SRC_ADDR      0x3BC   /* Source address (4 bytes) */
#define NETBUF_OFFSET_SRC_PORT      0x3C0   /* Source port (2 bytes) */
#define NETBUF_OFFSET_DST_ADDR      0x3C4   /* Destination address (4 bytes) */
#define NETBUF_OFFSET_DST_PORT      0x3C8   /* Destination port (2 bytes) */
#define NETBUF_OFFSET_HOP_COUNT     0x3CA   /* Hop count (2 bytes) */
#define NETBUF_OFFSET_HOP_ARRAY     0x3CC   /* Hop array (variable) */
#define NETBUF_OFFSET_EC_PARAM1     0x3E0   /* Event count param 1 */
#define NETBUF_OFFSET_EC_PARAM2     0x3E2   /* Event count param 2 */
#define NETBUF_OFFSET_NEXT          0x3E4   /* Next buffer in queue */
#define NETBUF_OFFSET_DATA_LEN      0x3E8   /* Data length (4 bytes) */
#define NETBUF_OFFSET_DATA_PTRS     0x3EC   /* Data page pointers (16 bytes) */

/*
 * The record SOCK_$GET fills in for its caller is sock_$pkt_info_t
 * (sock/sock.h); it is 0x40 bytes with at most 11 hop words.
 */

/*
 * Socket Table Base: sock_table_base, declared in sock/sock.h.
 *
 * The socket table is located at a fixed address in the kernel (0xE27510).
 * All socket operations reference this base address.
 */

/*
 * Internal Helper Macros
 */

/* Get pointer to socket EC view from socket number */
#define SOCK_GET_VIEW_PTR(sock_num) \
    (*(sock_$sock_t **)((uint8_t *)sock_table_base + \
                          SOCK_TABLE_PTR_ARRAY + ((sock_num) * 4)))

/* Get pointer to spinlock */
#define SOCK_GET_LOCK() \
    ((void *)((uint8_t *)sock_table_base + SOCK_TABLE_LOCK))

/* Get pointer to free list head (stores EC view pointers) */
#define SOCK_GET_FREE_LIST() \
    ((sock_$sock_t **)((uint8_t *)sock_table_base + SOCK_TABLE_FREE_LIST))

/* Get pointer to user socket limit counter */
#define SOCK_GET_USER_LIMIT() \
    ((uint16_t *)((uint8_t *)sock_table_base + SOCK_TABLE_USER_LIMIT))

/* Extract socket number from flags */
#define SOCK_GET_NUMBER(flags)  ((flags) & SOCK_FLAG_NUMBER_MASK)

/* Check if socket is allocated */
#define SOCK_IS_ALLOCATED(flags) (((flags) & SOCK_FLAG_ALLOCATED) != 0)

/* Check if socket is open */
#define SOCK_IS_OPEN(flags) (((flags) & SOCK_FLAG_OPEN) != 0)

/* Check if socket is user-mode */
#define SOCK_IS_USER_MODE(flags) (((flags) & SOCK_FLAG_USER_MODE) != 0)

/*
 * Internal Function Prototypes
 */

/* Put packet on socket queue (internal, returns event count pointer) */
int8_t SOCK_$PUT_INT(uint16_t sock_num, void **pkt_ptr, uint8_t flags,
                     uint16_t ec_param1, uint16_t ec_param2,
                     ec_$eventcount_t **ec_ret);

/* Put packet on socket queue (lowest level) */
int16_t SOCK_$PUT_INT_INT(sock_$sock_t *sock_view, void **pkt_ptr,
                          int8_t flags, uint16_t ec_param1, uint16_t ec_param2);

#endif /* SOCK_INTERNAL_H */
