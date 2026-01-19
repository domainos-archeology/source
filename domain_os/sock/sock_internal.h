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

#include "sock.h"
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
 * Socket EC View Structure (28 bytes / 0x1C)
 *
 * This structure represents the view of a socket from the EC pointer,
 * which is what's stored in the socket pointer table. The actual socket
 * descriptor in memory starts 4 bytes before this structure.
 *
 * Memory layout note: Socket descriptors are 0x1C bytes apart in the array,
 * but max_queue/buffer_pages extend 4 bytes past the EC base, overlapping
 * with the next socket's reserved prefix. This is intentional.
 *
 * Offsets shown are relative to the EC pointer (what's stored in ptr table).
 */
typedef struct sock_ec_view {
    ec_$eventcount_t    ec;             /* +0x00: Event count (12 bytes) */
    uint32_t            queue_head;     /* +0x0C: Head of receive queue (or free list next) */
    uint32_t            queue_tail;     /* +0x10: Tail of receive queue */
    uint8_t             protocol;       /* +0x14: Protocol type */
    uint8_t             queue_count;    /* +0x15: Number of packets in queue */
    uint16_t            flags;          /* +0x16: Flags and socket number */
    uint16_t            max_queue;      /* +0x18: Maximum queue depth */
    uint8_t             buffer_pages_hi;/* +0x1A: Buffer pages (high byte) */
    uint8_t             buffer_pages_lo;/* +0x1B: Buffer pages (low byte) */
} sock_ec_view_t;

/* The EC view is 28 bytes (12 + 4 + 4 + 1 + 1 + 2 + 2 + 1 + 1) */
#define SOCK_EC_VIEW_SIZE   0x1C

/* Verify structure size */
_Static_assert(sizeof(sock_ec_view_t) == SOCK_EC_VIEW_SIZE,
               "sock_ec_view_t size mismatch");

/*
 * Socket Descriptor in Array
 *
 * The actual socket descriptor starts 4 bytes before the EC. This 4-byte
 * prefix holds the max_queue/buffer_pages from the previous socket (or
 * is unused for the first socket).
 */
typedef struct sock_descriptor {
    uint32_t            prev_overflow;  /* +0x00: Prev socket's max_queue/buffer_pages */
    sock_ec_view_t      view;           /* +0x04: The EC view (28 bytes, extends to +0x20) */
} sock_descriptor_t;

/*
 * Note: sizeof(sock_descriptor_t) is 0x20 (32 bytes), but sockets are
 * spaced 0x1C (28 bytes) apart in the array. The last 4 bytes of each
 * socket's view overlap with the next socket's prev_overflow field.
 */

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
 * Packet Info Structure
 *
 * This structure is used by SOCK_$GET to return packet information
 * to the caller. It mirrors the network buffer header layout.
 */
typedef struct sock_pkt_info {
    uint32_t    hdr_ptr;        /* +0x00: Header pointer */
    uint32_t    src_addr;       /* +0x04: Source address */
    uint16_t    src_port;       /* +0x08: Source port */
    uint16_t    pad1;           /* +0x0A: Padding */
    uint32_t    dst_addr;       /* +0x0C: Destination address */
    uint16_t    dst_port;       /* +0x10: Destination port */
    uint16_t    hop_count;      /* +0x12: Number of hops */
    uint16_t    hops[12];       /* +0x14: Hop array (up to 12 hops) */
    uint16_t    pad2;           /* +0x2A: Padding for alignment */
    uint32_t    data_len;       /* +0x2A: Data length (actually at +0x2A in original) */
    uint32_t    data_ptrs[4];   /* +0x30: Data page pointers */
} sock_pkt_info_t;

/*
 * Socket Table Base
 *
 * The socket table is located at a fixed address in the kernel.
 * All socket operations reference this base address.
 */
extern uint8_t sock_table_base[];

/*
 * Internal Helper Macros
 */

/* Get pointer to socket EC view from socket number */
#define SOCK_GET_VIEW_PTR(sock_num) \
    (*(sock_ec_view_t **)((uint8_t *)sock_table_base + \
                          SOCK_TABLE_PTR_ARRAY + ((sock_num) * 4)))

/* Get pointer to spinlock */
#define SOCK_GET_LOCK() \
    ((void *)((uint8_t *)sock_table_base + SOCK_TABLE_LOCK))

/* Get pointer to free list head (stores EC view pointers) */
#define SOCK_GET_FREE_LIST() \
    ((sock_ec_view_t **)((uint8_t *)sock_table_base + SOCK_TABLE_FREE_LIST))

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
int16_t SOCK_$PUT_INT_INT(sock_ec_view_t *sock_view, void **pkt_ptr,
                          int8_t flags, uint16_t ec_param1, uint16_t ec_param2);

#endif /* SOCK_INTERNAL_H */
