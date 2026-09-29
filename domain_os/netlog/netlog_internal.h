/*
 * NETLOG Internal Header
 *
 * Internal data structures and definitions for the NETLOG subsystem.
 * This header should only be included by NETLOG implementation files.
 *
 * Module data block NETLOG_$DATA: Claude Opus 5.5 (source-iq58).
 */

#ifndef NETLOG_INTERNAL_H
#define NETLOG_INTERNAL_H

#include "arch/arch.h"
#include "ml/ml.h"
#include "net_io/net_io.h"
#include "pkt/pkt.h"
#include "netlog/netlog.h"
#include "network/network.h"
#include "proc1/proc1.h"
#include "time/time.h"

/*
 * Maximum number of wired pages for code/data
 */
#define NETLOG_MAX_WIRED_PAGES 10

/*
 * Number of double-buffers (always 2: index 1 and 2)
 * Index 0 is unused; current_buf_index toggles between 1 and 2
 */
#define NETLOG_NUM_BUFFERS 2

/*
 * Packet type constants used in the packet header
 */
#define NETLOG_PKT_TYPE1 99 /* 0x63 */
#define NETLOG_PKT_TYPE2 1

/*
 * Protocol constant for PKT_$BLD_INTERNET_HDR
 */
#define NETLOG_PROTOCOL 0x3F6 /* 1014 - logging protocol */

/*
 * ============================================================================
 * NETLOG_$DATA - the NETLOG module block, 0x00E85684..0x00E85707
 * ============================================================================
 *
 * SAU2 map: "D E85684 NETLOG size = 84", interior symbols NETLOG_$DATA_START
 * (0xE85684, +0x00), NETLOG_$DONE_CNT (0xE856F0, +0x6C) and, one past the
 * end, NETLOG_$DATA_END (0xE85708).  Every NETLOG routine loads
 * "lea (0xe85684).l,A5" (NETLOG_$CNTL 0x00E7191C, NETLOG_$LOG_IT 0x00E71B40,
 * NETLOG_$SEND_PAGE 0x00E71C80), so each (off,A5) below is a field.  The
 * address is the block's image address - the ordering key of
 * tools/gen_layout_ld.py and documentation, not where the block is linked
 * (docs/design-per-process-data.md).
 *
 * Two double-buffer tables are Pascal [1..2] arrays indexed with
 * send_page_index / current_buf_index (1 or 2).  Following the design's
 * per-index convention each is declared at the lowest address the code can
 * reach - element 0, the bias slot the compiler folds into the displacement -
 * and indexed with the Pascal index:
 *
 *   buffer_va    (0x54,A5,D0*1), D0 = idx*4       0x00E71C46, 0x00E71D46
 *                element 1 at +0x58, element 2 at +0x5C; element 0 at +0x54
 *                is not otherwise used, so it is its own cell.
 *   buffer_ppn   (0x5c,A5,D0*1), D0 = idx*4       0x00E71D36
 *                element 1 at +0x60 (clr/push 0x00E71988, 0x00E71A52),
 *                element 2 at +0x64; element 0 overlays buffer_va[2].
 *   page_counts  (0x6e,A0), A0 = A5 + idx*2       0x00E71B9C, 0x00E71C14,
 *                0x00E71C98, 0x00E7195C; element 1 at +0x70, element 2 at
 *                +0x72 (clr.w 0x00E719A4/0x00E719A8); element 0 overlays the
 *                low-order word of done_cnt.
 *
 * Where a bias slot overlays another object the block is a union of one arm
 * per table (as name/name.h and smd/smd_internal.h do).  No process-visible
 * index is ever 0, so nothing writes through a bias slot.
 *
 * Image contents (`gsk read 0xE85684 0x84`): the pkt_info template's first
 * 0x10 bytes, 00 04 00 02 00 02 80 31 00 01 00 00 ff ff 00 00; every other
 * byte zero (netlog/netlog_data.c).
 *
 * Every field is pointer-free (the buffer and ring addresses are 32-bit VAs),
 * so every assert below is unconditional.
 */
#define NETLOG_$DATA_SIZE 0x84          /* map: NETLOG size = 84 */

typedef struct netlog_$data_t {
  union {
    struct {
      /*
       * +0x00 packet info template.  NETLOG_$SEND_PAGE hands the block base
       * itself to PKT_$BLD_INTERNET_HDR as the pkt_info argument ("pea (A5)"
       * at 0x00E71CE4).
       */
      pkt_$info_t pkt_info;

      /*
       * +0x20 wired page handles, 0-based.  NETLOG_$CNTL unwires them with
       * "lea (0x4,A5),A2 / move.l (0x1c,A2),-(SP) / addq.l #4,A2"
       * (0x00E719B6) and MST_$WIRE_AREA fills them from "pea (0x20,A5)"
       * (0x00E719E8) and "pea (0x20,A5,D1w*0x1)" with D1 = wired_page_count*4
       * (0x00E71A16).  Capacity 10 ("moveq #0xa,D0 / sub.w (0x78,A5),D0w" at
       * 0x00E71A02).
       */
      uint32_t wired_pages[NETLOG_MAX_WIRED_PAGES];

      /* +0x48 packet header template, 10 bytes ("pea (0x48,A5)" at
       * 0x00E71CDE with length 10) */
      uint16_t pkt_type1;       /* +0x48: 99 (0x00E71AC0) */
      uint16_t pkt_type2;       /* +0x4A: 1 (0x00E71AC6) */
      uint32_t pkt_done_cnt;    /* +0x4C: done_cnt snapshot (0x00E71C86) */
      uint16_t pkt_entry_cnt;   /* +0x50: entry count snapshot (0x00E71C98) */
      uint16_t _unknown_52;     /* +0x52: not referenced */

      /* +0x54 buffer VAs, Pascal [1..2] at +0x58; see the block comment */
      uint32_t buffer_va[NETLOG_NUM_BUFFERS + 1];
    };
    struct {
      uint8_t  _ppn_bias[0x5C];
      /* +0x5C buffer PPNs, Pascal [1..2] at +0x60; element 0 = buffer_va[2] */
      uint32_t buffer_ppn[NETLOG_NUM_BUFFERS + 1];
      uint32_t spin_lock;       /* +0x68: "pea (0x68,A5)" 0x00E71B74 */
      /* +0x6C map NETLOG_$DONE_CNT: pages completed (0x00E71968, 0x00E71C22) */
      uint32_t done_cnt;
    };
    struct {
      uint8_t  _count_bias[0x6E];
      /* +0x6E entry counts, Pascal [1..2] at +0x70; element 0 = the low-order
       * word of done_cnt */
      uint16_t page_counts[NETLOG_NUM_BUFFERS + 1];
      uint32_t current_buf_ptr;   /* +0x74: VA of the filling page (0x00E71BC6) */
      uint16_t wired_page_count;  /* +0x78 */
      uint16_t send_page_index;   /* +0x7A: 1 or 2 */
      uint16_t current_buf_index; /* +0x7C: 1 or 2 */
      int8_t   initialized;       /* +0x7E: Domain boolean ("st (0x7e,A5)") */
      uint8_t  _unknown_7f;       /* +0x7F: not referenced */
      int8_t   ok_to_send;        /* +0x80: Domain boolean ("st (0x80,A5)") */
      uint8_t  _unknown_81[3];    /* +0x81..+0x83: not referenced */
    };
  };
} netlog_$data_t;

_Static_assert(sizeof(netlog_$data_t) == NETLOG_$DATA_SIZE, "NETLOG block: map size 0x84");
_Static_assert(offsetof(netlog_$data_t, pkt_info) == 0x00, "pkt_info (pea (A5))");
_Static_assert(offsetof(netlog_$data_t, wired_pages) == 0x20, "wired_pages");
_Static_assert(offsetof(netlog_$data_t, pkt_type1) == 0x48, "pkt_type1");
_Static_assert(offsetof(netlog_$data_t, pkt_type2) == 0x4A, "pkt_type2");
_Static_assert(offsetof(netlog_$data_t, pkt_done_cnt) == 0x4C, "pkt_done_cnt");
_Static_assert(offsetof(netlog_$data_t, pkt_entry_cnt) == 0x50, "pkt_entry_cnt");
_Static_assert(offsetof(netlog_$data_t, buffer_va) == 0x54, "buffer_va bias base");
_Static_assert(offsetof(netlog_$data_t, buffer_ppn) == 0x5C, "buffer_ppn bias base");
_Static_assert(offsetof(netlog_$data_t, spin_lock) == 0x68, "spin_lock");
_Static_assert(offsetof(netlog_$data_t, done_cnt) == 0x6C, "done_cnt (NETLOG_$DONE_CNT)");
_Static_assert(offsetof(netlog_$data_t, page_counts) == 0x6E, "page_counts bias base");
_Static_assert(offsetof(netlog_$data_t, current_buf_ptr) == 0x74, "current_buf_ptr");
_Static_assert(offsetof(netlog_$data_t, wired_page_count) == 0x78, "wired_page_count");
_Static_assert(offsetof(netlog_$data_t, send_page_index) == 0x7A, "send_page_index");
_Static_assert(offsetof(netlog_$data_t, current_buf_index) == 0x7C, "current_buf_index");
_Static_assert(offsetof(netlog_$data_t, initialized) == 0x7E, "initialized");
_Static_assert(offsetof(netlog_$data_t, ok_to_send) == 0x80, "ok_to_send");
/* strides: lsl.l #2 (buffer_va, buffer_ppn, wired_pages), add.l D0,D0 (page_counts) */
_Static_assert(sizeof(((netlog_$data_t *)0)->wired_pages[0]) == 4, "wired_pages stride");
_Static_assert(sizeof(((netlog_$data_t *)0)->buffer_va[0]) == 4, "buffer_va stride");
_Static_assert(sizeof(((netlog_$data_t *)0)->buffer_ppn[0]) == 4, "buffer_ppn stride");
_Static_assert(sizeof(((netlog_$data_t *)0)->page_counts[0]) == 2, "page_counts stride");
/* element 1 of each Pascal [1..2] table: */
_Static_assert(offsetof(netlog_$data_t, buffer_va[1]) == 0x58, "buffer_va[1]");
_Static_assert(offsetof(netlog_$data_t, buffer_ppn[1]) == 0x60, "buffer_ppn[1]");
_Static_assert(offsetof(netlog_$data_t, buffer_ppn[NETLOG_NUM_BUFFERS + 1]) == 0x68,
               "buffer_ppn[2] ends at spin_lock");
_Static_assert(offsetof(netlog_$data_t, page_counts[1]) == 0x70, "page_counts[1]");
_Static_assert(offsetof(netlog_$data_t, page_counts[NETLOG_NUM_BUFFERS + 1]) == 0x74,
               "page_counts[2] ends at current_buf_ptr");
_Static_assert(offsetof(netlog_$data_t, wired_pages[NETLOG_MAX_WIRED_PAGES]) == 0x48,
               "wired_pages[9] ends at the header template");

MODULE_DATA_DECLARE(netlog_$data_t, NETLOG_$DATA, 0x00E85684);

/*
 * Helper macro to calculate an entry address in a buffer.
 *
 * entry_index is the 1-based counter value (1 to 39).  The original builds
 * `A0 = current_buf_ptr + count*26` (0x00E71BB6..0x00E71BCA) and then writes
 * every field at a NEGATIVE displacement from -0x1A to -0x02, so the record
 * really starts one entry lower, at (count - 1) * 26.
 */
#define NETLOG_ENTRY_ADDR(buf_ptr, entry_index)                                \
  ((netlog_entry_t *)((char *)(buf_ptr) +                                      \
                      (((entry_index) - 1) * NETLOG_ENTRY_SIZE)))

/*
 * Helper to switch between buffer indices (1 <-> 2)
 * 3 - 1 = 2, 3 - 2 = 1
 */
#define NETLOG_SWITCH_BUFFER(idx) (3 - (idx))

/*
 * The two ranges NETLOG_$CNTL wires are held as Pascal by-reference constant
 * cells behind NETLOG_$CNTL's rts (`gsk read 0x00E71B24`):
 *
 *   00e71b24  00 0a          word  10       max wired pages
 *   00e71b28  00 e7 1d 7a    long  0xE71D7A code end   (NETLOG_$PROC_END)
 *   00e71b2c  00 e8 56 84    long  0xE85684 data start (NETLOG_$DATA_START)
 *   00e71b30  00 e8 57 08    long  0xE85708 data end   (NETLOG_$DATA_END)
 *   00e71b34  00 e7 19 14    long  0xE71914 code start (NETLOG_$PROC_START)
 *
 * The pushes are `pea (0x142,PC)`/`pea (0x13a,PC)` (0x00E719F0/0x00E719EC)
 * for the code range and `pea (0x10c,PC)`/`pea (0x114,PC)` (0x00E71A1E/
 * 0x00E71A1A) for the data range.  The cells are VAs of linked objects, so
 * netlog/cntl.c initialises them with ARCH_PTR_TO_VA_STATIC; these are the
 * image values that macro documents (and yields on the host).
 */
#define NETLOG_WIRE_CODE_START_VA   0xE71914u   /* 0x00E71B34 */
#define NETLOG_WIRE_CODE_END_VA     0xE71D7Au   /* 0x00E71B28 */
#define NETLOG_WIRE_DATA_START_VA   0xE85684u   /* 0x00E71B2C */
#define NETLOG_WIRE_DATA_END_VA     0xE85708u   /* 0x00E71B30 */

/*
 * Internal function prototypes
 */

/* None currently - all functions are in the public header */

#endif /* NETLOG_INTERNAL_H */
