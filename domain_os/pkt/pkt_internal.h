/*
 * PKT - Packet Building Module (Internal)
 *
 * This module provides functions for building and managing network packet
 * headers for Domain/OS internet protocol communication.
 *
 * Internal data structures and global variables.
 *
 * Module data block PKT_$DATA: Claude Opus 5.5 (source-r3tc).
 */

#ifndef PKT_INTERNAL_H
#define PKT_INTERNAL_H

#include "arch/arch.h"
#include "app/app.h"
#include "ec/ec.h"
#include "fim/fim.h"
#include "ml/ml.h"
#include "net_io/net_io.h"
#include "netbuf/netbuf.h"
#include "network/network.h"
#include "os/os.h"
#include "pkt/pkt.h"
#include "proc1/proc1.h"
#include "rip/rip.h"
#include "route/route.h"
#include "sock/sock.h"
#include "time/time.h"
#include "network/network.h"   /* NETWORK_$LOOPBACK_FLAG */

/*
 * Constants
 */
#define PKT_MAX_MISSING_NODES 10 /* Maximum tracked missing nodes */
/* Maximum short packet ID before wrap; 0xFA00, the "cmpi.w #-0x600" operand
 * at 0x00E124B4 read as unsigned. */
#define PKT_MAX_SHORT_ID 64000
#define PKT_CHUNK_SIZE 0x400     /* 1KB chunk size for data buffers */
#define PKT_MAX_DATA_CHUNKS 4    /* Maximum data buffer chunks */
#define PKT_MAX_HEADER 0x3B8     /* Maximum header size (952 bytes) */

#define PKT_PING_SOCKET 0x0D /* Socket number for ping service */

/*
 * Missing node tracking entry
 * Each entry tracks a node that failed to respond and when it was last seen
 * Size: 8 bytes
 */
typedef struct pkt_$missing_entry_t {
  uint32_t node_id;    /* 0x00: Node ID */
  uint32_t seq_number; /* 0x04: Visibility sequence number */
} pkt_$missing_entry_t;

/*
 * ============================================================================
 * PKT_$DATA - the PKT module block, 0x00E24C9C..0x00E24D43
 * ============================================================================
 *
 * SAU2 map: "D E24C9C PKT size = A8", interior symbols PKT_$MISSING_NODE
 * (0xE24C9C, +0x00) and PKT_$N_MISSING (0xE24CF4, +0x58).  Every PKT function
 * that touches it establishes A5 with "lea (0xe24c9c).l,A5" (0x00E1249A,
 * 0x00E124E8, 0x00E12524, 0x00E12656, 0x00E128C0, 0x00E128FE, 0x00E129A6,
 * 0x00E12BC0), so each (off,A5) below is a field.  The address is the
 * block's image address - the ordering key of tools/gen_layout_ld.py and
 * documentation, not where the block is linked
 * (docs/design-per-process-data.md).  The map places it after PEB_PARITY
 * (0xE24C78) and before PMAP_ (0xE24D44).
 *
 * missing_nodes is a Pascal [1..10] table whose element 1 is at +0x00 (the
 * map's PKT_$MISSING_NODE): PKT_$NOTE_VISIBLE addresses element k with
 * D = k*8 and "(-0x8,A5,D6*0x1)" / "(-0x4,A5,D6*0x1)" (0x00E12934,
 * 0x00E1295C, 0x00E12986, 0x00E1298E), and both it and
 * PKT_$RECENTLY_MISSING walk it from "lea (0x8,A5),A0" through "(-0x8,A0)"
 * (0x00E1291A, 0x00E128D6).  Element 0 would sit at A5-8, inside the
 * preceding PEB_PARITY segment, so - like P2_INFO_ENTRY and PMAP_SEGMAP_ROW -
 * the table is declared from element 1 and PKT_MISSING_ENTRY(k) applies the
 * bias once.  Nothing ever touches element 0.
 *
 * Every field is pointer-free, so every assert below is unconditional.
 * Image contents: pkt/pkt_data.c.
 */
#define PKT_$DATA_SIZE 0xA8     /* map: PKT size = A8 */

typedef struct pkt_$data_t {
  /* +0x00 map PKT_$MISSING_NODE: Pascal [1..10], see PKT_MISSING_ENTRY */
  pkt_$missing_entry_t missing_nodes[PKT_MAX_MISSING_NODES];

  /* +0x50 spin lock for id generation ("pea (0x50,A5)" in PKT_$NEXT_ID /
   * PKT_$NEXT_LONG_ID) */
  uint32_t spin_lock;

  /* +0x54 visibility sequence counter ("addq.l #0x1,(0x54,A5)" 0x00E12946) */
  uint32_t visibility_seq;
  /* +0x58 map PKT_$N_MISSING: entries in use in missing_nodes */
  int16_t n_missing;
  /*
   * +0x5A: the 2-byte request header both ping paths send.  Its address is
   * pushed as PKT_$SEND_INTERNET's "template" argument with a length of 2 by
   * PKT_$LIKELY_TO_ANSWER (0x00E12A9A "pea (0x5a,A5)" / 0x00E12A96
   * "move.w #0x2,-(SP)") and by PKT_$PING_SERVER (0x00E12D02 / 0x00E12CFE).
   * Image value at 0x00E24CF6 is 0x0001.
   */
  uint16_t ping_req_hdr;
  /*
   * +0x5C: short packet ID counter, cycling 1..64000.
   *
   * UNSIGNED: PKT_$NEXT_ID wraps it with "cmpi.w #-0x600,(0x5c,A5) / bls"
   * (0x00E124B4), and `bls` is the unsigned low-or-same branch, so the test
   * is (uint16)short_id > 0xFA00.  A signed word could never exceed 64000 and
   * the counter would run on into negative ids instead of wrapping.
   */
  uint16_t short_id;
  uint16_t pad_5e;         /* +0x5E: not referenced */
  int32_t long_id;         /* +0x60: long packet ID counter */

  /* +0x64 default send flags; NET_IO_$SEND's flags argument
   * (0x00E12732 "move.w (0x64,A5),-(SP)") */
  uint16_t default_flags;
  uint16_t pad_66;         /* +0x66: not referenced */

  /*
   * +0x68: the packet-info record PKT_$LIKELY_TO_ANSWER hands to
   * PKT_$SEND_INTERNET for its ping request (0x00E12AA0 "pea (0x68,A5)").
   */
  pkt_$info_t ping_template;

  /*
   * +0x88: the packet-info record PKT_$PING_SERVER hands to
   * PKT_$SEND_INTERNET for its reply.  The reply flags are stored into its
   * first word (0x00E12CE6 "move.w D4w,(0x88,A5)") and its address is then
   * pushed (0x00E12D0A "pea (0x88,A5)").  The two records are byte-identical
   * in the image apart from that first word (0x0010 vs 0x0020).
   */
  pkt_$info_t ping_reply_info;
} pkt_$data_t;

_Static_assert(sizeof(pkt_$missing_entry_t) == 8, "missing_nodes stride (lsl.l #0x3 at 0x00E12932)");
_Static_assert(offsetof(pkt_$data_t, missing_nodes) == 0x00, "missing_nodes[1] (PKT_$MISSING_NODE)");
_Static_assert(offsetof(pkt_$data_t, missing_nodes[PKT_MAX_MISSING_NODES]) == 0x50,
               "missing_nodes[10] ends at spin_lock");
_Static_assert(offsetof(pkt_$data_t, spin_lock) == 0x50, "pkt_$data_t.spin_lock");
_Static_assert(offsetof(pkt_$data_t, visibility_seq) == 0x54, "pkt_$data_t.visibility_seq");
_Static_assert(offsetof(pkt_$data_t, n_missing) == 0x58, "pkt_$data_t.n_missing (PKT_$N_MISSING)");
_Static_assert(offsetof(pkt_$data_t, ping_req_hdr) == 0x5A, "pkt_$data_t.ping_req_hdr");
_Static_assert(offsetof(pkt_$data_t, short_id) == 0x5C, "pkt_$data_t.short_id");
_Static_assert(offsetof(pkt_$data_t, long_id) == 0x60, "pkt_$data_t.long_id");
_Static_assert(offsetof(pkt_$data_t, default_flags) == 0x64, "pkt_$data_t.default_flags");
_Static_assert(offsetof(pkt_$data_t, ping_template) == 0x68, "pkt_$data_t.ping_template");
_Static_assert(offsetof(pkt_$data_t, ping_reply_info) == 0x88, "pkt_$data_t.ping_reply_info");
_Static_assert(sizeof(pkt_$data_t) == PKT_$DATA_SIZE, "PKT block: map size 0xA8");

MODULE_DATA_DECLARE(pkt_$data_t, PKT_$DATA, 0x00E24C9C);

/*
 * PKT_MISSING_ENTRY(k) - element k (1..10) of the Pascal [1..10]
 * missing_nodes table, an lvalue of type pkt_$missing_entry_t.  The -1 is the
 * compiler's -8 displacement bias, applied here once (see the block comment).
 */
#define PKT_MISSING_ENTRY(k)    (PKT_$DATA.missing_nodes[(k) - 1])

/*
 * pkt_$no_data - the all-zero longword at 0x00E12BB4, in the code region
 * between PKT_$LIKELY_TO_ANSWER's rts and PKT_$PING_SERVER.
 *
 * Both ping senders pass its address as PKT_$SEND_INTERNET's "data" argument
 * with a data length of zero, so the buffer is never read:
 *   0x00E12A92  pea (0x120,PC)     0x00E12A92 + 2 + 0x120 = 0x00E12BB4
 *   0x00E12CFA  pea (-0x148,PC)    0x00E12CFA + 2 - 0x148 = 0x00E12BB4
 */
static const uint32_t pkt_$no_data = 0;

/*
 * pkt_$internet_hdr_t - the fields PKT reads out of a received internet
 * packet header (the reply record APP_$RECEIVE leaves in
 * app_$receive_rec_t.reply).
 */
typedef struct pkt_$internet_hdr_t {
  uint16_t reserved_00;    /* 0x00 */
  uint16_t hdr_len;        /* 0x02: request header length (0x00E12C8E) */
  uint16_t data_len;       /* 0x04: data length (0x00E12B02, 0x00E12C6E) */
  int16_t request_id;      /* 0x06: request id (0x00E12B0C, 0x00E12C86) */
  uint8_t reserved_08[6];  /* 0x08 */
  uint32_t src_node;       /* 0x0E: source node (0x00E12C74) */
  uint16_t src_sock;       /* 0x12: source socket (0x00E12C7A) */
  uint8_t flags;           /* 0x14: request flags (0x00E12C82) */
} __attribute__((packed)) pkt_$internet_hdr_t;

_Static_assert(offsetof(pkt_$internet_hdr_t, request_id) == 0x06, "pkt_$internet_hdr_t.request_id");
_Static_assert(offsetof(pkt_$internet_hdr_t, flags) == 0x14, "pkt_$internet_hdr_t.flags");

/*
 * External references needed by PKT
 *
 * ROUTE_$PORTP (0x00E26EE8) is the array of route_$port_t pointers declared by
 * route/route.h; PKT_$LIKELY_TO_ANSWER indexes it with the port number that
 * RIP_$FIND_NEXTHOP returned (0x00E129FA - 0x00E12A08).
 */

/*
 * Internal function prototypes
 */

/*
 * PKT_$PING_SERVER - Ping server process entry point
 *
 * This is the main loop for the ping server process that responds
 * to network ping requests.
 *
 * Original address: 0x00E12BB8
 */
void PKT_$PING_SERVER(void);

#endif /* PKT_INTERNAL_H */
