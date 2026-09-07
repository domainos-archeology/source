/*
 * NETLOG Internal Header
 *
 * Internal data structures and definitions for the NETLOG subsystem.
 * This header should only be included by NETLOG implementation files.
 */

#ifndef NETLOG_INTERNAL_H
#define NETLOG_INTERNAL_H

#include "ml/ml.h"
#include "net_io/net_io.h"
#include "pkt/pkt.h"
#include "netlog/netlog.h"
#include "network/network.h"
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
 * NETLOG internal data structure
 *
 * This structure holds all internal state for the NETLOG subsystem.
 * On m68k, it is located at 0xE85684 and accessed via A5 base register.
 *
 * Size: ~0x84 bytes (132 bytes)
 */
typedef struct netlog_data_t {
  /*
   * Packet info template (0x00 - 0x1F)
   *
   * NETLOG_$SEND_PAGE hands the module data base itself to
   * PKT_$BLD_INTERNET_HDR as the pkt_info argument ("pea (A5)" at
   * 0x00E71CE4 with A5 = 0xE85684), so the record's first 0x20 bytes are a
   * pkt_$info_t.
   */
  pkt_$info_t pkt_info; /* 0x00 */

  /*
   * Wired page handles (0x20 - 0x47)
   *
   * One array, not two: NETLOG_$CNTL unwires it with "lea (0x4,A5),A2 /
   * move.l (0x1c,A2),-(SP) / addq.l #4,A2" (0x00E719B6, i.e. A5+0x20+4*i)
   * and MST_$WIRE_AREA fills it from "pea (0x20,A5)" (0x00E719E8) and
   * "pea (0x20,A5,D1w*0x1)" with D1 = wired_page_count*4 (0x00E71A16).
   * Capacity is 10 ("moveq #0xa,D0 / sub.w (0x78,A5),D0w" at 0x00E71A02).
   */
  uint32_t wired_pages[NETLOG_MAX_WIRED_PAGES]; /* 0x20 */

  /*
   * Packet header template (0x48 - 0x53)
   */
  uint16_t pkt_type1;     /* 0x48: Packet type field 1 (99) */
  uint16_t pkt_type2;     /* 0x4A: Packet type field 2 (1) */
  uint32_t pkt_done_cnt;  /* 0x4C: DONE_CNT snapshot for packet */
  uint16_t pkt_entry_cnt; /* 0x50: Entry count snapshot */
  uint16_t _pad_52;       /* 0x52: Padding */

  /*
   * Buffer addresses (0x54 - 0x67)
   * Two sets of buffers for double-buffering
   */
  uint32_t
      buffer_va[NETLOG_NUM_BUFFERS + 1]; /* 0x54: Virtual addresses [0,1,2] */
                                         /* Note: index 0 unused */
  uint32_t
      buffer_ppn[NETLOG_NUM_BUFFERS]; /* 0x60: Physical page numbers [0,1] */

  /*
   * Spin lock for thread safety (0x68)
   */
  uint32_t spin_lock; /* 0x68: Spin lock variable */

  /*
   * Page tracking (0x6C - 0x77)
   */
  uint32_t done_cnt; /* 0x6C: Total completed pages count */
  /* Only two words exist: NETLOG_$CNTL clears 0x70 and 0x72 (00e719a4 /
   * 00e719a8) and NETLOG_$LOG_IT bumps `(0x6e,A0)` with A0 = A5 + 2*index
   * (00e71b9c), i.e. element `index` sits at 0x6E + 2*index for index 1..2.
   * The array is therefore Pascal 1-based: page_counts[index - 1]. */
  uint16_t page_counts[NETLOG_NUM_BUFFERS]; /* 0x70: Entry counts, 1-based */
  uint32_t current_buf_ptr; /* 0x74: Pointer to current buffer */

  /*
   * State tracking (0x78 - 0x80)
   */
  uint16_t wired_page_count;  /* 0x78: Number of wired pages */
  uint16_t send_page_index;   /* 0x7A: Index of page to send (1 or 2) */
  uint16_t current_buf_index; /* 0x7C: Current buffer index (1 or 2) */
  int8_t initialized;         /* 0x7E: Initialization flag (0xFF = yes) */
  int8_t _pad_7f;             /* 0x7F: Padding (0x7E and 0x80 are both bytes) */
  int8_t ok_to_send;          /* 0x80: OK to send packets flag */
} netlog_data_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(netlog_data_t, pkt_info) == 0x00, "netlog_data_t.pkt_info");
_Static_assert(__builtin_offsetof(netlog_data_t, wired_pages) == 0x20, "netlog_data_t.wired_pages");
_Static_assert(__builtin_offsetof(netlog_data_t, pkt_type1) == 0x48, "netlog_data_t.pkt_type1");
_Static_assert(__builtin_offsetof(netlog_data_t, pkt_type2) == 0x4A, "netlog_data_t.pkt_type2");
_Static_assert(__builtin_offsetof(netlog_data_t, pkt_done_cnt) == 0x4C, "netlog_data_t.pkt_done_cnt");
_Static_assert(__builtin_offsetof(netlog_data_t, pkt_entry_cnt) == 0x50, "netlog_data_t.pkt_entry_cnt");
_Static_assert(__builtin_offsetof(netlog_data_t, _pad_52) == 0x52, "netlog_data_t._pad_52");
_Static_assert(__builtin_offsetof(netlog_data_t, buffer_va) == 0x54, "netlog_data_t.buffer_va");
_Static_assert(__builtin_offsetof(netlog_data_t, buffer_ppn) == 0x60, "netlog_data_t.buffer_ppn");
_Static_assert(__builtin_offsetof(netlog_data_t, spin_lock) == 0x68, "netlog_data_t.spin_lock");
_Static_assert(__builtin_offsetof(netlog_data_t, done_cnt) == 0x6C, "netlog_data_t.done_cnt");
_Static_assert(__builtin_offsetof(netlog_data_t, page_counts) == 0x70, "netlog_data_t.page_counts");
_Static_assert(__builtin_offsetof(netlog_data_t, current_buf_ptr) == 0x74, "netlog_data_t.current_buf_ptr");
_Static_assert(__builtin_offsetof(netlog_data_t, wired_page_count) == 0x78, "netlog_data_t.wired_page_count");
_Static_assert(__builtin_offsetof(netlog_data_t, send_page_index) == 0x7A, "netlog_data_t.send_page_index");
_Static_assert(__builtin_offsetof(netlog_data_t, current_buf_index) == 0x7C, "netlog_data_t.current_buf_index");
_Static_assert(__builtin_offsetof(netlog_data_t, initialized) == 0x7E, "netlog_data_t.initialized");
_Static_assert(__builtin_offsetof(netlog_data_t, _pad_7f) == 0x7F, "netlog_data_t._pad_7f");
_Static_assert(__builtin_offsetof(netlog_data_t, ok_to_send) == 0x80, "netlog_data_t.ok_to_send");
/* m68k rounds struct size to 2 bytes (a host rounds to 4), so the sizeof
 * check is target-specific; every offset above is checked unconditionally. */
#if defined(ARCH_M68K)
_Static_assert(sizeof(netlog_data_t) == 0x82,
               "netlog_data_t: fields end at 0x80 (ok_to_send)");
#endif

/*
 * Architecture-specific access macros
 */
#if defined(ARCH_M68K)
#define NETLOG_DATA ((netlog_data_t *)0xE85684)
#else
extern netlog_data_t netlog_data;
#define NETLOG_DATA (&netlog_data)
#endif

/*
 * Current process ID access
 * On m68k, this is at 0xE20609 (low byte of PROC1_$CURRENT at 0xE20608)
 */
#if defined(ARCH_M68K)
#define NETLOG_GET_CURRENT_PID() (*(uint8_t *)0xE20609)
#else
/* Include proc1 header for PROC1_$CURRENT */
#include "proc1/proc1.h"
#define NETLOG_GET_CURRENT_PID() ((uint8_t)(PROC1_$CURRENT & 0xFF))
#endif

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
 * Code/data boundaries used for wiring (NETLOG_$CNTL) and the packet info
 * template used by NETLOG_$SEND_PAGE.
 *
 * On m68k these are absolute addresses of the NETLOG/AUDIT code and data
 * areas; on other architectures they are link-time symbols.
 */
/*
 * The two ranges NETLOG_$CNTL wires are held as Pascal by-reference constant
 * cells behind NETLOG_$CNTL's rts (`gsk read 0x00E71B24`):
 *
 *   00e71b24  00 0a          word  10       max wired pages
 *   00e71b28  00 e7 1d 7a    long  0xE71D7A code end
 *   00e71b2c  00 e8 56 84    long  0xE85684 data start
 *   00e71b30  00 e8 57 08    long  0xE85708 data end
 *   00e71b34  00 e7 19 14    long  0xE71914 code start
 *
 * The pushes are `pea (0x142,PC)`/`pea (0x13a,PC)` (0x00E719F0/0x00E719EC)
 * for the code range and `pea (0x10c,PC)`/`pea (0x114,PC)` (0x00E71A1E/
 * 0x00E71A1A) for the data range.  0xE71914 is NETLOG_$CNTL itself and
 * 0xE71D7A is just past NETLOG_$SEND_PAGE's rts; 0xE85684..0xE85708 is
 * netlog_data_t.  Nothing about this range is AUDIT's.
 *
 * These are raw target virtual addresses in the image, so they are the same
 * on every build; a real retarget has to repoint them at link symbols.
 */
#define NETLOG_WIRE_CODE_START_VA   0xE71914u   /* 0x00E71B34 */
#define NETLOG_WIRE_CODE_END_VA     0xE71D7Au   /* 0x00E71B28 */
#define NETLOG_WIRE_DATA_START_VA   0xE85684u   /* 0x00E71B2C */
#define NETLOG_WIRE_DATA_END_VA     0xE85708u   /* 0x00E71B30 */

#if defined(ARCH_M68K)
    #define AUDIT_PKT_INFO          ((void*)0xE248FC)   /* Packet info template */
#else
    extern char AUDIT_PKT_INFO_SYM;
    #define AUDIT_PKT_INFO          (&AUDIT_PKT_INFO_SYM)
#endif

/*
 * Internal function prototypes
 */

/* None currently - all functions are in the public header */

#endif /* NETLOG_INTERNAL_H */
