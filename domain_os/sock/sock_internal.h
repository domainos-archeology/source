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
 * Module data: SOCK_$DATA (sock/sock.h), map "D E27510 SOCK size = 1C28".
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

/* The sock_$sock_t.flags masks live in sock/sock.h with the record. */

/*
 * The descriptor of socket n is SOCK_$DATA.socket[n] and its table entry
 * SOCK_$DATA.socket_ptr[n] (sock/sock.h); SOCK_$INIT points the one at the
 * other (0x00E2FE1A).
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
/* Owned by netbuf/netbuf.h; see NETBUF_HDR_EC_PARAM1 / NETBUF_HDR_EC_PARAM2 */
#define NETBUF_OFFSET_EC_PARAM1     NETBUF_HDR_EC_PARAM1
#define NETBUF_OFFSET_EC_PARAM2     NETBUF_HDR_EC_PARAM2
#define NETBUF_OFFSET_NEXT          0x3E4   /* Next buffer in queue */
#define NETBUF_OFFSET_DATA_LEN      0x3E8   /* Data length (4 bytes) */
#define NETBUF_OFFSET_DATA_PTRS     0x3EC   /* Data page pointers (16 bytes) */

/*
 * The record SOCK_$GET fills in for its caller is sock_$pkt_info_t
 * (sock/sock.h); it is 0x40 bytes with at most 11 hop words.
 */

/*
 * Internal Helper Macros
 */

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

/* SOCK_$PUT_INT (0x00E16190) is declared in sock/sock.h (net_io uses it). */

/*
 * Put packet on socket queue (lowest level).
 * 0x00E161F8: 0x08 sock_view, 0x0C pkt_info, 0x10 flags(b), 0x12/0x14 words.
 */
int16_t SOCK_$PUT_INT_INT(sock_$sock_t *sock_view, sock_$pkt_info_t *pkt_info,
                          int8_t flags, uint16_t ec_param1, uint16_t ec_param2);

#endif /* SOCK_INTERNAL_H */
