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
 * Maximum valid socket number.
 *
 * SOCK_$INIT (0x00E2FDF8 `move.w #0xdf,D2w` + dbf) runs 224 passes with
 * sock_num starting at 1, so the descriptors cover 1..0xE0, and the only
 * bounds check in the subsystem agrees: SOCK_$PUT_INT rejects `<= 0` and
 * accepts up to 0xE0 (0x00E161AE `cmpi.w #0xe0,D0w` / `bls`).  MSG_$CLOSEI
 * and MSG_$WAITI use the same `ble #0xe0`.
 */
#define SOCK_MAX_NUMBER         0xE0    /* 224 */

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
 * @param proto_bufpages  Packed pair of words: high word = the queue depth
 *                        limit written to sock_$sock_t.max_queue (D2b at
 *                        0x00E15E2E), low word = the netbuf HEADER page count
 *                        written to hdr_pages (D3b at 0x00E15E20)
 * @param max_queue       Packed pair of words: high word = the netbuf DATA
 *                        page count written to data_pages (D4b at
 *                        0x00E15E1C), low word = the maximum accepted packet
 *                        data length written to max_data_len (D5w at
 *                        0x00E15E24)
 *
 * @return Negative (0xFF) on success, 0 on failure (socket already in use)
 *
 * Note: If either page count is non-zero, calls NETBUF_$ADD_PAGES.
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
 * @param proto_bufpages  Packed pair of words: high word = the queue depth
 *                        limit written to sock_$sock_t.max_queue (D2b at
 *                        0x00E15EDA), low word = the netbuf HEADER page count
 *                        written to hdr_pages (D3b at 0x00E15ED2)
 * @param max_queue       Packed pair of words: high word = the netbuf DATA
 *                        page count written to data_pages (D4b at
 *                        0x00E15ECE), low word = the maximum accepted packet
 *                        data length written to max_data_len (D5w at
 *                        0x00E15ED6)
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
 * @param proto_hi   Queue depth limit          (-> sock_$sock_t.max_queue)
 * @param proto_lo   Netbuf header page count    (-> sock_$sock_t.hdr_pages)
 * @param queue_hi   Netbuf data page count      (-> sock_$sock_t.data_pages)
 * @param queue_lo   Maximum packet data length  (-> sock_$sock_t.max_data_len)
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
 * Aliases for the same socket pointer table used by other subsystems.  They
 * used to be re-declared in app/app_internal.h, network/network_internal.h and
 * rem_file/rem_file_internal.h with three different types; the storage is
 * owned here (bead source-3uo).
 *
 *   SOCK_$TABLE_BASE  - slot 0 of the table (0xE28DB0, the socket spinlock);
 *                       app/ indexes from its ADDRESS by sock_num*4.
 *   SOCK_$SOCKET_PTR  - slot 1 onwards (0xE28DB4), i.e. SOCK_$EVENT_COUNTERS.
 */
#define SOCK_$TABLE_BASE        (*(void **)(sock_table_base + 0x18A0))
#define SOCK_$SOCKET_PTR        ((void **)(sock_table_base + 0x18A4))

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
 * (EC_$WAIT, EC_$ADVANCE) or as a sock_$sock_t *.  The descriptors are
 * spaced 0x1C bytes apart in the table (SOCK_$INIT 0x00E2FE5A), and the EC
 * of descriptor n sits 4 bytes into descriptor n's slot (0x00E2FE16), so the
 * last four bytes of one socket's view overlap the next slot's first four.
 *
 * Field meanings recovered from SOCK_$PUT_INT_INT (0x00E161F8):
 *   0x14 is the queue depth LIMIT and 0x15 the current depth - the admission
 *   test is "clr.w D5w / move.b (0x15,A0),D5b / clr.w D6w /
 *   move.b (0x14,A0),D6b / cmp.w D6w,D5w / bcs" at 0x00E1624C-0x00E1625A,
 *   i.e. accept only while queue_count < max_queue.
 *   0x18 is the maximum packet data length: "move.w (0x2a,A2),D1w /
 *   cmp.w (0x18,A0),D1w / bls" at 0x00E1623C compares it against the
 *   sock_$pkt_info_t data length.
 *   0x1A / 0x1B are the two netbuf page counts SOCK_$ALLOCATE hands to
 *   NETBUF_$ADD_PAGES (0x00E15ECE/D2, pushed as D3 then D4 at 0x00E15EFE) and
 *   SOCK_$CLOSE hands to NETBUF_$DEL_PAGES (0x00E1600A-0x00E16016): 0x1A is
 *   the header count, 0x1B the data count.
 *
 * ROUTE_$PROCESS buckets its packet statistics by the byte at +0x15
 * (0x00E874D2, 0x00E87644).
 */
/*
 * sock_$sock_t.flags (descriptor offset 0x16)
 *
 * The flags word encodes both status flags and the socket number:
 *   Bits 0-12:  Socket number (0x1FFF mask)
 *   Bit 13:     Socket allocated (SOCK_FLAG_ALLOCATED)
 *   Bit 14:     User-mode socket (SOCK_FLAG_USER_MODE)
 *   Bit 15:     Socket open/ready (SOCK_FLAG_OPEN)
 *
 * Public rather than internal because the field is: route_$init_routing
 * clears bit 15 with "bclr.b #0x7,(0x16,A2)" at 0x00E69DCC.
 */
#define SOCK_FLAG_NUMBER_MASK   0x1FFF  /* Bits 0-12: socket number */
#define SOCK_FLAG_ALLOCATED     0x2000  /* Bit 13: socket is allocated */
#define SOCK_FLAG_USER_MODE     0x4000  /* Bit 14: user-mode socket */
#define SOCK_FLAG_OPEN          0x8000  /* Bit 15: socket is open */

/* Byte-level flag access (for bset/bclr instructions) */
#define SOCK_BFLAG_ALLOCATED    0x20    /* Bit 5 of high byte = bit 13 */
#define SOCK_BFLAG_USER_MODE    0x40    /* Bit 6 of high byte = bit 14 */
#define SOCK_BFLAG_OPEN         0x80    /* Bit 7 of high byte = bit 15 */

typedef struct sock_$sock_t {
    ec_$eventcount_t    ec;             /* 0x00: event count (12 bytes) */
    uint32_t            queue_head;     /* 0x0C: head of the receive queue;
                                         *       doubles as the free-list link
                                         *       (SOCK_$INIT 0x00E2FE46) */
    uint32_t            queue_tail;     /* 0x10: tail of the receive queue */
    uint8_t             max_queue;      /* 0x14: queue depth limit; cleared to 0
                                         *       while the socket is closed */
    uint8_t             queue_count;    /* 0x15: packets currently queued */
    uint16_t            flags;          /* 0x16: flags and socket number */
    uint16_t            max_data_len;   /* 0x18: largest accepted data length */
    uint8_t             hdr_pages;      /* 0x1A: netbuf header pages owned */
    uint8_t             data_pages;     /* 0x1B: netbuf data pages owned */
} sock_$sock_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(sock_$sock_t, queue_head)   == 0x0C, "sock_$sock_t.queue_head");
_Static_assert(offsetof(sock_$sock_t, queue_tail)   == 0x10, "sock_$sock_t.queue_tail");
_Static_assert(offsetof(sock_$sock_t, max_queue)    == 0x14, "sock_$sock_t.max_queue");
_Static_assert(offsetof(sock_$sock_t, queue_count)  == 0x15, "sock_$sock_t.queue_count");
_Static_assert(offsetof(sock_$sock_t, flags)        == 0x16, "sock_$sock_t.flags");
_Static_assert(offsetof(sock_$sock_t, max_data_len) == 0x18, "sock_$sock_t.max_data_len");
_Static_assert(offsetof(sock_$sock_t, hdr_pages)    == 0x1A, "sock_$sock_t.hdr_pages");
_Static_assert(offsetof(sock_$sock_t, data_pages)   == 0x1B, "sock_$sock_t.data_pages");
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
 */
typedef struct sock_$pkt_info_t {
    uint32_t    hdr;            /* 0x00 <- netbuf+0x3B8: header buffer VA.
                                 * A target VA, not a C pointer: a real
                                 * pointer would break the 0x40-byte layout
                                 * on a 64-bit host.  Use ARCH_VA_TO_PTR. */
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

/*
 * sock_$pkt_info_t.flags
 *
 * Both readers test bit 1 with a byte btst on the LOW half of the word:
 * ROUTE_$PROCESS "btst.b #1,(0x11,rec)" at 0x00E874EA and RIP_$SERVER
 * "btst.b #0x1,(-0x5f,A6)" at 0x00E68A28.
 *
 * The port demux routines build the word the same way - "move.w #0x2,(rec+0x10)"
 * followed by byte bsets on the word's LOW half at rec+0x11, so a "bset.b #n"
 * there is word bit n:
 *   MAC_$DEMUX 0x00E0BC62  move.w #0x2,(-0x30,A6)
 *              0x00E0BC6E  bset.b #0x0,(-0x2f,A6)   from rcv_pkt.is_local
 *              0x00E0BC7C  bset.b #0x2,(-0x2f,A6)   from its own third argument
 *   APP_$DEMUX 0x00E00ADA  move.w #0x2,(-0x30,A6)
 *              0x00E00AE4  bset.b #0x2,(-0x2f,A6)   from its own fourth argument
 * Nothing in this image reads bit 0 or bit 2 back.
 */
#define SOCK_PKT_FLAG_LOCAL 0x0001  /* the frame was originated by this node */
#define SOCK_PKT_FLAG_XNS 0x0002    /* frame arrived over XNS ("standard") routing */
#define SOCK_PKT_FLAG_DEMUX_BOOL 0x0004 /* the boolean the port demux was handed;
                                         * RING_$RECEIVE_PACKET takes it from bit
                                         * 3 of the ring header byte at +0x07
                                         * ("btst.l #0x3,D3 / sne" 0x00E7650E) */

/* No pointer fields, so the layout holds on the host too. */
_Static_assert(offsetof(sock_$pkt_info_t, hdr)        == 0x00, "sock_$pkt_info_t.hdr");
_Static_assert(offsetof(sock_$pkt_info_t, src_addr)   == 0x04, "sock_$pkt_info_t.src_addr");
_Static_assert(offsetof(sock_$pkt_info_t, src_port)   == 0x08, "sock_$pkt_info_t.src_port");
_Static_assert(offsetof(sock_$pkt_info_t, dst_addr)   == 0x0C, "sock_$pkt_info_t.dst_addr");
_Static_assert(offsetof(sock_$pkt_info_t, flags)      == 0x10, "sock_$pkt_info_t.flags");
_Static_assert(offsetof(sock_$pkt_info_t, n_hops)     == 0x12, "sock_$pkt_info_t.n_hops");
_Static_assert(offsetof(sock_$pkt_info_t, hops)       == 0x14, "sock_$pkt_info_t.hops");
_Static_assert(offsetof(sock_$pkt_info_t, data_len)   == 0x2A, "sock_$pkt_info_t.data_len");
_Static_assert(offsetof(sock_$pkt_info_t, hdr_len)    == 0x2C, "sock_$pkt_info_t.hdr_len");
_Static_assert(offsetof(sock_$pkt_info_t, data_pages) == 0x30, "sock_$pkt_info_t.data_pages");
_Static_assert(sizeof(sock_$pkt_info_t) == 0x40, "sock_$pkt_info_t must be 0x40 bytes");

/*
 * SOCK_$PUT - Put packet on socket receive queue
 *
 * Queues a packet for delivery to a socket. If successful, advances
 * the socket's event count to wake any waiting processes.
 *
 * The record is passed straight through by value: SOCK_$PUT does
 * `move.l (0xa,A6),-(SP)` (0x00E16164), SOCK_$PUT_INT re-pushes it with
 * `pea (A2)` (0x00E161E4), and SOCK_$PUT_INT_INT then USES it as the record
 * (`movea.l (0xc,A6),A2` at 0x00E16206, `move.w (0x2a,A2),D1w` at
 * 0x00E1623C reads .data_len).  There is no extra level of indirection.
 *
 * @param sock_num      Socket number (1..SOCK_MAX_NUMBER)
 * @param pkt_info      The packet record to queue
 * @param flags         Domain boolean; true (negative) means "copy the
 *                      socket's queue_count into the header buffer at +0x0F"
 * @param ec_param1     Event count parameter 1
 * @param ec_param2     Event count parameter 2
 *
 * @return Negative (0xFF) if packet queued, 0 on error (socket full or closed)
 *
 * Original address: 0x00E1614E
 */
int8_t SOCK_$PUT(uint16_t sock_num, sock_$pkt_info_t *pkt_info, int8_t flags,
                 uint16_t ec_param1, uint16_t ec_param2);

#endif /* SOCK_H */
