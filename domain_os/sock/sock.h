/*
 * SOCK - Socket Management Module (Public Header)
 *
 * This module provides socket management for network communication in Domain/OS.
 * Sockets are message queues that allow network packets to be delivered to
 * processes. Each socket has an associated event count for synchronization.
 *
 * Socket Number Allocation:
 *   - Sockets 0-31: Reserved for well-known services (statically bound)
 *   - Sockets 32-223: Dynamically allocated from free list
 *
 * Return Value Convention:
 *   - Negative (< 0, typically 0xFF): Success
 *   - Zero or positive (>= 0): Failure or empty queue
 */

#ifndef SOCK_H
#define SOCK_H

#include "base/base.h"
#include "ec/ec.h"

/*
 * Socket flags for SOCK_$OPEN and SOCK_$ALLOCATE
 */
#define SOCK_FLAG_USER          0x40    /* User-mode socket (bit 6) */
#define SOCK_FLAG_KERNEL        0x00    /* Kernel-mode socket */

/*
 * Maximum valid socket number
 */
#define SOCK_MAX_NUMBER         0xDF    /* 223 */

/*
 * SOCK_$INIT - Initialize socket subsystem
 *
 * Initializes all socket descriptors, event counts, and the free list.
 * Must be called during kernel initialization before any other SOCK functions.
 *
 * Called by: NET_$INIT (or equivalent network initialization)
 *
 * Original address: 0x00E2FDF0
 */
void SOCK_$INIT(void);

/*
 * SOCK_$OPEN - Open a socket with a specific socket number
 *
 * Opens a socket for a well-known service (socket numbers 0-31) or
 * claims a specific socket number in the dynamic range (32-223).
 * The socket must not already be in use.
 *
 * @param sock_num        Socket number to open (1-223)
 * @param proto_bufpages  Packed value: (protocol << 16) | buffer_pages
 *                        - High word (bits 16-31): protocol type identifier
 *                        - Low word (bits 0-15): buffer page allocation
 * @param max_queue       Maximum receive queue depth
 *
 * @return Negative (0xFF) on success, 0 on failure (socket already in use)
 *
 * Note: If buffer_pages is non-zero, calls NETBUF_$ADD_PAGES to allocate buffers.
 *
 * Original address: 0x00E15D8C
 */
int8_t SOCK_$OPEN(uint16_t sock_num, uint32_t proto_bufpages, uint32_t max_queue);

/*
 * SOCK_$ALLOCATE - Allocate a socket from the free pool
 *
 * Allocates a socket with an automatically assigned socket number from
 * the dynamic range (32-223). The socket is taken from the free list.
 *
 * @param sock_ret        Output: allocated socket number (0 on failure)
 * @param proto_bufpages  Packed value: (protocol << 16) | buffer_pages
 *                        - High word (bits 16-31): protocol type identifier
 *                        - Low word (bits 0-15): buffer page allocation
 * @param max_queue       Maximum receive queue depth
 *
 * @return Negative (0xFF) on success, 0 on failure (no free sockets)
 *
 * Original address: 0x00E15E62
 */
int8_t SOCK_$ALLOCATE(uint16_t *sock_ret, uint32_t proto_bufpages, uint32_t max_queue);

/*
 * SOCK_$ALLOCATE_USER - Allocate a user-mode socket
 *
 * Allocates a socket for user-mode use. User-mode sockets are tracked
 * separately and have a limit on the total number that can be allocated.
 * The socket is marked with the SOCK_FLAG_USER bit.
 *
 * Note: Parameters are passed as 16-bit pairs due to Pascal's 16-bit integer
 * limitations on some platforms. The function combines them internally:
 *   proto_bufpages = (proto_hi << 16) | proto_lo
 *   max_queue = (queue_hi << 16) | queue_lo
 *
 * @param sock_ret   Output: allocated socket number (0 on failure)
 * @param proto_hi   High word of proto_bufpages (protocol type)
 * @param proto_lo   Low word of proto_bufpages (buffer pages)
 * @param queue_hi   High word of max_queue
 * @param queue_lo   Low word of max_queue
 *
 * @return Negative (0xFF) on success, 0 on failure (no free user sockets)
 *
 * Original address: 0x00E15F14
 */
int8_t SOCK_$ALLOCATE_USER(uint16_t *sock_ret,
                           uint16_t proto_hi, uint16_t proto_lo,
                           uint16_t queue_hi, uint16_t queue_lo);

/*
 * SOCK_$CLOSE - Close a socket
 *
 * Closes an open socket, draining any queued packets and returning
 * allocated buffers to the pool. For dynamic sockets (>= 32), returns
 * the socket to the free list for reuse.
 *
 * @param sock_num      Socket number to close
 *
 * Original address: 0x00E15F72
 */
void SOCK_$CLOSE(uint16_t sock_num);

/*
 * SOCK_$GET - Get next packet from socket receive queue
 *
 * Retrieves the next packet from a socket's receive queue. The packet
 * information is copied to the provided buffer.
 *
 * @param sock_num      Socket number
 * @param pkt_info      Output: packet information buffer (40+ bytes)
 *
 * @return Negative (0xFF) if packet retrieved, 0 if queue empty
 *
 * Original address: 0x00E16070
 */
int8_t SOCK_$GET(uint16_t sock_num, void *pkt_info);

/*
 * SOCK_$PUT - Put packet on socket receive queue
 *
 * Queues a packet for delivery to a socket. If successful, advances
 * the socket's event count to wake any waiting processes.
 *
 * @param sock_num      Socket number
 * @param pkt_ptr       Pointer to packet buffer pointer
 * @param flags         Flags (bit 7 = copy queue count to packet header)
 * @param ec_param1     Event count parameter 1
 * @param ec_param2     Event count parameter 2
 *
 * @return Negative (0xFF) if packet queued, 0 on error (socket full or closed)
 *
 * Original address: 0x00E1614E
 */
int8_t SOCK_$PUT(uint16_t sock_num, void **pkt_ptr, uint8_t flags,
                 uint16_t ec_param1, uint16_t ec_param2);

/*
 * sock_table_base - The socket table (0xE27510, see sock_internal.h)
 *
 * Layout:
 *   +0x0000: header (free list head at +0x0C)
 *   +0x001C: 224 socket descriptors, 0x1C bytes each
 *   +0x18A0: socket pointer table (slot 0 = spinlock, slot n = socket n)
 *   +0x1C24: user socket limit counter
 */
#define SOCK_TABLE_SIZE         0x1C28  /* Rounded up for alignment */
extern uint8_t sock_table_base[SOCK_TABLE_SIZE];

/*
 * SOCK_$EVENT_COUNTERS - Socket event counter array
 *
 * Array of pointers to socket event counters.  This is the socket pointer
 * table starting at its slot 1 (0xE28DB4 = sock_table_base + 0x18A4);
 * slot 0 of the table (0xE28DB0) holds the socket spinlock.  Note that
 * several users index this from 0xE28DB4 with an offset of -4, i.e.
 * SOCK_$EVENT_COUNTERS[sock - 1] is the entry for socket "sock".
 *
 * Original address: 0xE28DB4
 */
#define SOCK_$EVENT_COUNTERS    ((ec_$eventcount_t **)(sock_table_base + 0x18A4))

/*
 * =============================================================================
 * Recovered record layouts (verified against the SOCK_$GET disassembly)
 * =============================================================================
 */

/*
 * sock_$sock_t - a socket descriptor as seen through SOCK_$EVENT_COUNTERS
 *
 * SOCK_$EVENT_COUNTERS[n] points at the event count that begins the socket
 * descriptor, so the pointer may be treated either as an ec_$eventcount_t *
 * (EC_$WAIT, EC_$ADVANCE) or as a sock_$sock_t *.  SOCK_$GET reads the
 * queue depth as a byte at +0x15 (0x00E160A6) and ROUTE_$PROCESS uses the
 * same byte to bucket its packet statistics (0x00E874D2, 0x00E87644).
 *
 * This is the public spelling of sock_ec_view_t in sock/sock_internal.h.
 * TODO(source-s8k4): fold the internal copy onto this one.
 */
typedef struct sock_$sock_t {
    ec_$eventcount_t    ec;             /* 0x00: event count (12 bytes) */
    uint32_t            queue_head;     /* 0x0C: head of the receive queue */
    uint32_t            queue_tail;     /* 0x10: tail of the receive queue */
    uint8_t             protocol;       /* 0x14: protocol type */
    uint8_t             queue_count;    /* 0x15: packets currently queued */
    uint16_t            flags;          /* 0x16: flags and socket number */
    uint16_t            max_queue;      /* 0x18: maximum queue depth */
    uint16_t            buffer_pages;   /* 0x1A: buffer pages */
} sock_$sock_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(sock_$sock_t, queue_head)  == 0x0C, "sock_$sock_t.queue_head");
_Static_assert(offsetof(sock_$sock_t, queue_tail)  == 0x10, "sock_$sock_t.queue_tail");
_Static_assert(offsetof(sock_$sock_t, protocol)    == 0x14, "sock_$sock_t.protocol");
_Static_assert(offsetof(sock_$sock_t, queue_count) == 0x15, "sock_$sock_t.queue_count");
_Static_assert(offsetof(sock_$sock_t, flags)       == 0x16, "sock_$sock_t.flags");
_Static_assert(sizeof(sock_$sock_t) == 0x1C, "sock_$sock_t must be 0x1C bytes");
#endif

/*
 * sock_$pkt_info_t - the record SOCK_$GET fills in for its caller (0x40 bytes)
 *
 * Every field is copied out of the netbuf header of the dequeued packet
 * (0x00E160EC - 0x00E1613E); the source offset inside the netbuf page is
 * given in each comment.  The 0x0A and 0x2E holes are never written.
 *
 * The hop array holds n_hops words; the field after it starts at 0x2A, so
 * at most 11 hops fit.
 *
 * TODO(source-s8k4): sock/sock_internal.h's sock_pkt_info_t declares
 * hops[12] and so misplaces every field from +0x2A on; replace it with
 * this record.
 */
typedef struct sock_$pkt_info_t {
    void       *hdr;            /* 0x00 <- netbuf+0x3B8: header buffer VA */
    uint32_t    src_addr;       /* 0x04 <- netbuf+0x3BC */
    uint16_t    src_port;       /* 0x08 <- netbuf+0x3C0 */
    uint16_t    _hole_0a;       /* 0x0A: not written by SOCK_$GET */
    uint32_t    dst_addr;       /* 0x0C <- netbuf+0x3C4 */
    uint16_t    flags;          /* 0x10 <- netbuf+0x3C8; bit 1 = XNS ("standard")
                                 *       routing, tested by ROUTE_$PROCESS as
                                 *       btst.b #1,(0x11,rec) at 0x00E874EA */
    uint16_t    n_hops;         /* 0x12 <- netbuf+0x3CA */
    uint16_t    hops[11];       /* 0x14 <- netbuf+0x3CC.. (n_hops entries) */
    uint16_t    data_len;       /* 0x2A <- netbuf+0x3E8: payload byte count */
    uint16_t    hdr_len;        /* 0x2C <- netbuf+0x3EA: header byte count */
    uint16_t    _hole_2e;       /* 0x2E: not written by SOCK_$GET */
    uint32_t    data_pages[4];  /* 0x30 <- netbuf+0x3EC: payload page VAs */
} sock_$pkt_info_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(sock_$pkt_info_t, dst_addr)   == 0x0C, "sock_$pkt_info_t.dst_addr");
_Static_assert(offsetof(sock_$pkt_info_t, flags)      == 0x10, "sock_$pkt_info_t.flags");
_Static_assert(offsetof(sock_$pkt_info_t, n_hops)     == 0x12, "sock_$pkt_info_t.n_hops");
_Static_assert(offsetof(sock_$pkt_info_t, hops)       == 0x14, "sock_$pkt_info_t.hops");
_Static_assert(offsetof(sock_$pkt_info_t, data_len)   == 0x2A, "sock_$pkt_info_t.data_len");
_Static_assert(offsetof(sock_$pkt_info_t, hdr_len)    == 0x2C, "sock_$pkt_info_t.hdr_len");
_Static_assert(offsetof(sock_$pkt_info_t, data_pages) == 0x30, "sock_$pkt_info_t.data_pages");
_Static_assert(sizeof(sock_$pkt_info_t) == 0x40, "sock_$pkt_info_t must be 0x40 bytes");
#endif

#endif /* SOCK_H */
