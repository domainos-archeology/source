/*
 * APP - Application Protocol Module (Internal Header)
 *
 * Internal data structures and functions for the APP subsystem.
 * This module provides application-level network protocol handling,
 * building on top of the XNS IDP protocol layer.
 *
 * The APP module handles:
 * - Receiving and parsing network packets from sockets
 * - Demultiplexing packets to appropriate handlers
 * - Opening standard application channels
 *
 * Memory layout (m68k):
 *   - APP globals: 0xE1DC0C
 *   - Exclusion lock: 0xE1DC0C
 *   - Temp buffer: 0xE1DC24 (0x394 bytes)
 *   - STD IDP channel: 0xE1DC20
 */

#ifndef APP_INTERNAL_H
#define APP_INTERNAL_H

#include "app/app.h"
#include "ml/ml.h"
#include "netbuf/netbuf.h"
#include "network/network.h"
#include "os/os.h"
#include "pkt/pkt.h"
#include "ring/ring.h"   /* RING_$FILE_OVERFLOW, RING_$OVERFLOW_OVERFLOW */
#include "route/route.h"
#include "sock/sock.h"
#include "xns/xns.h"

/*
 * ============================================================================
 * Constants
 * ============================================================================
 */

/* Maximum packet data size before requiring temp buffer */
#define APP_MAX_INLINE_SIZE 0x3B8 /* 952 bytes */

/* Standard application protocol number (XNS) */
#define APP_STD_PROTOCOL 0x0499 /* Standard app protocol */

/* Socket type for file overflow */
#define APP_SOCK_TYPE_FILE 2
#define APP_SOCK_TYPE_OVERFLOW 6

/* Network type constants */
#define APP_NET_TYPE_LOCAL 1  /* Local network (same node) */
#define APP_NET_TYPE_REMOTE 2 /* Remote network */

/* Address size for local network */
#define APP_ADDR_SIZE_LOCAL 4
#define APP_ADDR_SIZE_REMOTE 0x29 /* ')' - special marker */

/*
 * ============================================================================
 * Status Codes
 * ============================================================================
 */


/*
 * ============================================================================
 * Data Structures
 * ============================================================================
 */

/*
 * APP receive result structure
 *
 * Filled in by APP_$RECEIVE with parsed packet information.
 * This structure is passed to the caller and contains pointers
 * to the packet header and data areas.
 *
 * Size: 44 bytes (0x2C)
 */
typedef struct app_receive_result_t {
  void *hdr_ptr;          /* 0x00: Pointer to packet header */
  void *data_ptr;         /* 0x04: Pointer to packet data */
  uint32_t src_uid_high;  /* 0x08: Source UID high word */
  uint32_t src_uid_low;   /* 0x0C: Source UID low word */
  uint32_t dest_uid_high; /* 0x10: Destination UID high word */
  uint32_t dest_uid_low;  /* 0x14: Destination UID low word */
  uint32_t src_node;      /* 0x18: Source node ID */
  uint32_t dest_node;     /* 0x1C: Destination node ID */
  uint32_t routing_key;   /* 0x20: Routing key */
  uint16_t sock_num;      /* 0x24: Socket number */
  uint16_t flags;         /* 0x26: Flags */
  uint16_t info;          /* 0x28: Protocol info */
  uint16_t _reserved;     /* 0x2A: Reserved */
} app_receive_result_t;

/* Layout recovered from the disassembly -- see the field comments above. */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(app_receive_result_t, hdr_ptr) == 0x00, "app_receive_result_t.hdr_ptr");
_Static_assert(__builtin_offsetof(app_receive_result_t, data_ptr) == 0x04, "app_receive_result_t.data_ptr");
_Static_assert(__builtin_offsetof(app_receive_result_t, src_uid_high) == 0x08, "app_receive_result_t.src_uid_high");
_Static_assert(__builtin_offsetof(app_receive_result_t, src_uid_low) == 0x0C, "app_receive_result_t.src_uid_low");
_Static_assert(__builtin_offsetof(app_receive_result_t, dest_uid_high) == 0x10, "app_receive_result_t.dest_uid_high");
_Static_assert(__builtin_offsetof(app_receive_result_t, dest_uid_low) == 0x14, "app_receive_result_t.dest_uid_low");
_Static_assert(__builtin_offsetof(app_receive_result_t, src_node) == 0x18, "app_receive_result_t.src_node");
_Static_assert(__builtin_offsetof(app_receive_result_t, dest_node) == 0x1C, "app_receive_result_t.dest_node");
_Static_assert(__builtin_offsetof(app_receive_result_t, routing_key) == 0x20, "app_receive_result_t.routing_key");
_Static_assert(__builtin_offsetof(app_receive_result_t, sock_num) == 0x24, "app_receive_result_t.sock_num");
_Static_assert(__builtin_offsetof(app_receive_result_t, flags) == 0x26, "app_receive_result_t.flags");
_Static_assert(__builtin_offsetof(app_receive_result_t, info) == 0x28, "app_receive_result_t.info");
_Static_assert(__builtin_offsetof(app_receive_result_t, _reserved) == 0x2A, "app_receive_result_t._reserved");
_Static_assert(sizeof(app_receive_result_t) == 0x2C, "app_receive_result_t size");
#endif

/*
 * APP packet header structure
 *
 * Header prepended to application protocol data.
 *
 * Size: 24 bytes (0x18)
 */
/* PACKED: m68k aligns 32-bit fields to 2 bytes, so the recovered offsets
 * below are only reproducible on a 4/8-byte-aligning host if the record is
 * packed.  Packing changes no m68k layout. */
typedef struct app_pkt_hdr_t {
  uint16_t type;         /* 0x00: Packet type (0x118 for std) */
  uint16_t data_len;     /* 0x02: Data length */
  uint16_t template_len; /* 0x04: Template length */
  uint16_t protocol;     /* 0x06: Protocol number */
  uint32_t src_node;     /* 0x08: Source node ID (24-bit) */
  uint16_t src_sock;     /* 0x0C: Source socket */
  uint32_t dest_node;    /* 0x0E: Destination node ID (24-bit) */
  uint16_t dest_sock;    /* 0x12: Destination socket */
  uint8_t net_type;      /* 0x14: Network type */
  uint8_t addr_size;     /* 0x15: Address size indicator */
  uint8_t flags;         /* 0x16: Flags */
  uint8_t _reserved;     /* 0x17: Reserved */
} __attribute__((packed)) app_pkt_hdr_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(app_pkt_hdr_t, type) == 0x00, "app_pkt_hdr_t.type");
_Static_assert(__builtin_offsetof(app_pkt_hdr_t, data_len) == 0x02, "app_pkt_hdr_t.data_len");
_Static_assert(__builtin_offsetof(app_pkt_hdr_t, template_len) == 0x04, "app_pkt_hdr_t.template_len");
_Static_assert(__builtin_offsetof(app_pkt_hdr_t, protocol) == 0x06, "app_pkt_hdr_t.protocol");
_Static_assert(__builtin_offsetof(app_pkt_hdr_t, src_node) == 0x08, "app_pkt_hdr_t.src_node");
_Static_assert(__builtin_offsetof(app_pkt_hdr_t, src_sock) == 0x0C, "app_pkt_hdr_t.src_sock");
_Static_assert(__builtin_offsetof(app_pkt_hdr_t, dest_node) == 0x0E, "app_pkt_hdr_t.dest_node");
_Static_assert(__builtin_offsetof(app_pkt_hdr_t, dest_sock) == 0x12, "app_pkt_hdr_t.dest_sock");
_Static_assert(__builtin_offsetof(app_pkt_hdr_t, net_type) == 0x14, "app_pkt_hdr_t.net_type");
_Static_assert(__builtin_offsetof(app_pkt_hdr_t, addr_size) == 0x15, "app_pkt_hdr_t.addr_size");
_Static_assert(__builtin_offsetof(app_pkt_hdr_t, flags) == 0x16, "app_pkt_hdr_t.flags");
_Static_assert(__builtin_offsetof(app_pkt_hdr_t, _reserved) == 0x17, "app_pkt_hdr_t._reserved");
_Static_assert(sizeof(app_pkt_hdr_t) == 0x18, "app_pkt_hdr_t size");

/*
 * XNS IDP open parameters
 *
 * Parameters passed to XNS_IDP_$OS_OPEN
 */
typedef struct xns_idp_open_params_t {
  uint32_t protocol;   /* 0x00: Protocol (high word) and flags (low) */
  void *demux_handler; /* 0x04: Demultiplex handler function */
  uint32_t net_info;   /* 0x08: Network info from ROUTE_$PORTP */
} xns_idp_open_params_t;

/* Layout recovered from the disassembly -- see the field comments above. */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(xns_idp_open_params_t, protocol) == 0x00, "xns_idp_open_params_t.protocol");
_Static_assert(__builtin_offsetof(xns_idp_open_params_t, demux_handler) == 0x04, "xns_idp_open_params_t.demux_handler");
_Static_assert(__builtin_offsetof(xns_idp_open_params_t, net_info) == 0x08, "xns_idp_open_params_t.net_info");
#endif

/*
 * ============================================================================
 * Global Variable Declarations
 * ============================================================================
 */

#if defined(ARCH_M68K)

/* Exclusion lock for APP operations (at 0xE1DC0C) */
#define APP_$EXCLUSION_LOCK (*(ml_$exclusion_t *)0xE1DC0C)

/* Standard IDP channel number (at 0xE1DC20) */
#define APP_$STD_IDP_CHANNEL (*(uint16_t *)0xE1DC20)

/* Temporary buffer for large packets (at 0xE1DC24, 0x394 bytes) */
#define APP_$TEMP_BUFFER ((uint8_t *)0xE1DC24)

#else
/* Non-m68k: extern declarations */
extern ml_$exclusion_t APP_$EXCLUSION_LOCK;
extern uint16_t APP_$STD_IDP_CHANNEL;
extern uint8_t APP_$TEMP_BUFFER[];
#endif

#endif /* APP_INTERNAL_H */
