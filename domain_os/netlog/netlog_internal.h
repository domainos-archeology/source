/*
 * NETLOG Internal Header
 *
 * Internal data structures and definitions for the NETLOG subsystem.
 * This header should only be included by NETLOG implementation files.
 */

#ifndef NETLOG_INTERNAL_H
#define NETLOG_INTERNAL_H

#include "ml/ml.h"
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
   * Wired page handles (0x00 - 0x27)
   * Space for up to 10 wired page handles (4 bytes each)
   */
  uint32_t wired_pages[NETLOG_MAX_WIRED_PAGES]; /* 0x00 */

  /*
   * MST wire area data (0x28 - 0x47)
   * Used by MST_$WIRE_AREA for wiring code/data pages
   */
  uint32_t wire_area_data[8]; /* 0x28 */

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
  uint16_t page_counts[NETLOG_NUM_BUFFERS + 1]; /* 0x70: Entry counts [0,1,2] */
                                                /* Note: index 0 is padding */
  uint32_t current_buf_ptr; /* 0x74: Pointer to current buffer */

  /*
   * State tracking (0x78 - 0x80)
   */
  uint16_t wired_page_count;  /* 0x78: Number of wired pages */
  uint16_t send_page_index;   /* 0x7A: Index of page to send (1 or 2) */
  uint16_t current_buf_index; /* 0x7C: Current buffer index (1 or 2) */
  int8_t initialized;         /* 0x7E: Initialization flag (0xFF = yes) */
  int8_t ok_to_send;          /* 0x80: OK to send packets flag */
} netlog_data_t;

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
 * Helper macro to calculate entry address in buffer
 *
 * entry_index is 1-based (1 to 39)
 * Entry address = buffer_base + (entry_index * 26)
 */
#define NETLOG_ENTRY_ADDR(buf_ptr, entry_index)                                \
  ((netlog_entry_t *)((char *)(buf_ptr) + ((entry_index) * NETLOG_ENTRY_SIZE)))

/*
 * Helper to switch between buffer indices (1 <-> 2)
 * 3 - 1 = 2, 3 - 2 = 1
 */
#define NETLOG_SWITCH_BUFFER(idx) (3 - (idx))

/*
 * Internal function prototypes
 */

/* None currently - all functions are in the public header */

#endif /* NETLOG_INTERNAL_H */
