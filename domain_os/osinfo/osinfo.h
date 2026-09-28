// OSINFO subsystem header for Domain/OS
// Operating system information and memory management queries

#ifndef OSINFO_H
#define OSINFO_H

#include "base/base.h"

// =============================================================================
// Status Codes
// =============================================================================
#define status_$os_info_array_too_small 0x00200001
#define status_$os_info_invalid_asid 0x00200002
#define status_$os_info_page_found 0x00200003
#define status_$os_info_page_wired 0x00200004

// =============================================================================
// Segment Table Types
// =============================================================================

// Segment table type identifiers
#define SEG_TABLE_TYPE_AOTE 1 // Address Object Table Entry (0x80 bytes each)
#define SEG_TABLE_TYPE_AST 2  // Address Segment Table (0x14 bytes each)

// AOTE entry size: 0x80 = 128 bytes (32 longs)
#define AOTE_ENTRY_SIZE 0x80

// AST entry size: 0x14 = 20 bytes (5 longs)
#define AST_ENTRY_SIZE 0x14

// =============================================================================
// Memory Map Request Flags (in param_1 byte 1)
// =============================================================================
#define MMAP_FLAG_GET_COUNTERS 0x01 // Get paging counters
#define MMAP_FLAG_GET_GLOBAL 0x02   // Get global memory info
#define MMAP_FLAG_GET_WS_INFO 0x04  // Get working set info
#define MMAP_FLAG_GET_WS_LIST 0x08  // Get working set list
#define MMAP_FLAG_GET_PID 0x10      // Get process ID
#define MMAP_FLAG_SET_PARAMS 0x20   // Set paging parameters
#define MMAP_FLAG_FIND_PAGE 0x40    // Find physical page info

// =============================================================================
// Memory Map Set Operations (when MMAP_FLAG_SET_PARAMS is set)
// =============================================================================
#define MMAP_SET_WS_INTERVAL 0   // Set working set interval
#define MMAP_SET_IDLE_INTERVAL 1 // Set idle interval
#define MMAP_SET_WS_MAX 2        // Set working set max
#define MMAP_PURGE_WS 3          // Purge working set
#define MMAP_SET_WS_LIMIT 4      // Set working set limit

// =============================================================================
// External Memory Management Variables
// =============================================================================
// The AST_$*, PMAP_$* and MMAP_$* globals reported by OSINFO_$GET_MMAP are
// declared in ast/ast.h, pmap/pmap.h and mmap/mmap.h respectively
// (PROC1_$CURRENT in proc1/proc1.h).

// =============================================================================
// Paging Counters Structure (returned by MMAP_FLAG_GET_COUNTERS)
// Offset 0x00 in param_2, 0x42 bytes total
// =============================================================================
/* PACKED: m68k aligns 32-bit fields to 2 bytes, so the recovered offsets
 * below are only reproducible on a 4/8-byte-aligning host if the record is
 * packed.  Packing changes no m68k layout. */
typedef struct osinfo_paging_counters {
  uint32_t pur_l_cnt;        // 0x00: Local purge count
  uint32_t pur_r_cnt;        // 0x04: Remote purge count
  uint32_t page_flt_cnt;     // 0x08: Page fault count
  uint32_t ws_flt_cnt;       // 0x0C: Working set fault count
  uint32_t t_pur_scans;      // 0x10: Total purge scans
  uint32_t alloc_cnt;        // 0x14: Allocation count
  uint32_t alloc_pages;      // 0x18: Allocated pages
  uint32_t steal_cnt;        // 0x1C: Page steal count
  uint32_t ws_overflow;      // 0x20: Working set overflow
  uint32_t ws_scan_cnt;      // 0x24: Working set scan count
  uint32_t reserved_28;      // 0x28: Reserved
  uint32_t ast_alloc_cnt;    // 0x2C: AST allocation count
  uint32_t alloc_too_few;    // 0x30: Allocation failed count
  uint32_t reclaim_shar_cnt; // 0x34: Shared reclaim count
  uint32_t reclaim_pur_cnt;  // 0x38: Purge reclaim count
  uint32_t ws_remove;        // 0x3C: Working set remove count
  uint16_t scan_fract;       // 0x40: Scan fraction
} __attribute__((packed)) osinfo_paging_counters_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(osinfo_paging_counters_t, pur_l_cnt) == 0x00, "osinfo_paging_counters_t.pur_l_cnt");
_Static_assert(__builtin_offsetof(osinfo_paging_counters_t, pur_r_cnt) == 0x04, "osinfo_paging_counters_t.pur_r_cnt");
_Static_assert(__builtin_offsetof(osinfo_paging_counters_t, page_flt_cnt) == 0x08, "osinfo_paging_counters_t.page_flt_cnt");
_Static_assert(__builtin_offsetof(osinfo_paging_counters_t, ws_flt_cnt) == 0x0C, "osinfo_paging_counters_t.ws_flt_cnt");
_Static_assert(__builtin_offsetof(osinfo_paging_counters_t, t_pur_scans) == 0x10, "osinfo_paging_counters_t.t_pur_scans");
_Static_assert(__builtin_offsetof(osinfo_paging_counters_t, alloc_cnt) == 0x14, "osinfo_paging_counters_t.alloc_cnt");
_Static_assert(__builtin_offsetof(osinfo_paging_counters_t, alloc_pages) == 0x18, "osinfo_paging_counters_t.alloc_pages");
_Static_assert(__builtin_offsetof(osinfo_paging_counters_t, steal_cnt) == 0x1C, "osinfo_paging_counters_t.steal_cnt");
_Static_assert(__builtin_offsetof(osinfo_paging_counters_t, ws_overflow) == 0x20, "osinfo_paging_counters_t.ws_overflow");
_Static_assert(__builtin_offsetof(osinfo_paging_counters_t, ws_scan_cnt) == 0x24, "osinfo_paging_counters_t.ws_scan_cnt");
_Static_assert(__builtin_offsetof(osinfo_paging_counters_t, reserved_28) == 0x28, "osinfo_paging_counters_t.reserved_28");
_Static_assert(__builtin_offsetof(osinfo_paging_counters_t, ast_alloc_cnt) == 0x2C, "osinfo_paging_counters_t.ast_alloc_cnt");
_Static_assert(__builtin_offsetof(osinfo_paging_counters_t, alloc_too_few) == 0x30, "osinfo_paging_counters_t.alloc_too_few");
_Static_assert(__builtin_offsetof(osinfo_paging_counters_t, reclaim_shar_cnt) == 0x34, "osinfo_paging_counters_t.reclaim_shar_cnt");
_Static_assert(__builtin_offsetof(osinfo_paging_counters_t, reclaim_pur_cnt) == 0x38, "osinfo_paging_counters_t.reclaim_pur_cnt");
_Static_assert(__builtin_offsetof(osinfo_paging_counters_t, ws_remove) == 0x3C, "osinfo_paging_counters_t.ws_remove");
_Static_assert(__builtin_offsetof(osinfo_paging_counters_t, scan_fract) == 0x40, "osinfo_paging_counters_t.scan_fract");
_Static_assert(sizeof(osinfo_paging_counters_t) == 0x42, "osinfo_paging_counters_t size");

// =============================================================================
// Global Memory Info Structure (returned by MMAP_FLAG_GET_GLOBAL)
// Offset in param_3
// =============================================================================
typedef struct osinfo_global_info {
  uint32_t real_pages;           // 0x00: Total real pages
  uint32_t pageable_lower_limit; // 0x04: Pageable pages lower limit
  uint32_t remote_pages;         // 0x08: Remote pages
  uint32_t ws_data[5];           // 0x0C-0x1F: Working set data
  uint16_t ws_interval;          // 0x20: Current working set interval
  uint16_t wsl_hi_mark;          // 0x22: Working set list high mark
  uint16_t pid;                  // 0x24: Process ID (from GET_PID)
  uint16_t set_op;               // 0x26: Set operation code
  uint32_t set_value;            // 0x28: Set operation value
  // 0x28 is read and written as a longword (00e5c6fc / 00e5c7d6), 0x2E and
  // 0x30 as words (00e5c6f0, 00e5c95c), so only a single word sits at 0x2C.
  uint16_t reserved_2C;          // 0x2C: Reserved
  uint16_t asid;                 // 0x2E: Address space ID
  uint16_t ws_list_count;        // 0x30: Working set list count
} osinfo_global_info_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(osinfo_global_info_t, real_pages) == 0x00, "osinfo_global_info_t.real_pages");
_Static_assert(__builtin_offsetof(osinfo_global_info_t, pageable_lower_limit) == 0x04, "osinfo_global_info_t.pageable_lower_limit");
_Static_assert(__builtin_offsetof(osinfo_global_info_t, remote_pages) == 0x08, "osinfo_global_info_t.remote_pages");
_Static_assert(__builtin_offsetof(osinfo_global_info_t, ws_data) == 0x0C, "osinfo_global_info_t.ws_data");
_Static_assert(__builtin_offsetof(osinfo_global_info_t, ws_interval) == 0x20, "osinfo_global_info_t.ws_interval");
_Static_assert(__builtin_offsetof(osinfo_global_info_t, wsl_hi_mark) == 0x22, "osinfo_global_info_t.wsl_hi_mark");
_Static_assert(__builtin_offsetof(osinfo_global_info_t, pid) == 0x24, "osinfo_global_info_t.pid");
_Static_assert(__builtin_offsetof(osinfo_global_info_t, set_op) == 0x26, "osinfo_global_info_t.set_op");
_Static_assert(__builtin_offsetof(osinfo_global_info_t, set_value) == 0x28, "osinfo_global_info_t.set_value");
_Static_assert(__builtin_offsetof(osinfo_global_info_t, reserved_2C) == 0x2C, "osinfo_global_info_t.reserved_2C");
_Static_assert(__builtin_offsetof(osinfo_global_info_t, asid) == 0x2E, "osinfo_global_info_t.asid");
_Static_assert(__builtin_offsetof(osinfo_global_info_t, ws_list_count) == 0x30, "osinfo_global_info_t.ws_list_count");
/* m68k rounds struct size to 2 bytes (a host rounds to 4), so the sizeof
 * check is target-specific; every offset above is checked unconditionally. */
#if defined(ARCH_M68K)
_Static_assert(sizeof(osinfo_global_info_t) == 0x32,
               "osinfo_global_info_t: fields end at 0x31 (ws_list_count)");
#endif

// =============================================================================
// Function Declarations
// =============================================================================

// OSINFO_$GET_SEG_TABLE - Get segment table information
// @param type_ptr: Pointer to table type (1=AOTE, 2=AST)
// @param buffer: Buffer to receive table entries
// @param max_entries_ptr: Pointer to max entries caller can accept
// @param actual_entries_ptr: Pointer to receive actual entry count copied
// @param total_entries_ptr: Pointer to receive total entries available
// @param status: Pointer to receive status
extern void OSINFO_$GET_SEG_TABLE(short *type_ptr, void *buffer,
                                  short *max_entries_ptr,
                                  short *actual_entries_ptr,
                                  short *total_entries_ptr, status_$t *status);

// OSINFO_$GET_MMAP - Get/set memory map information
// @param flags: Request flags (MMAP_FLAG_*)
// @param counters: Buffer for paging counters (if MMAP_FLAG_GET_COUNTERS)
// @param info: Buffer for global info / set parameters
// @param ws_data: Buffer for working set data (if MMAP_FLAG_GET_WS_INFO)
// @param ws_list: Buffer for working set list (if MMAP_FLAG_GET_WS_LIST)
// @param uid_out: Buffer for UID output (if MMAP_FLAG_FIND_PAGE)
// @param status: Pointer to receive status
// The first argument is a POINTER to the request record; byte 1 of that
// record holds the MMAP_FLAG_* bits (`btst.b #n,(0x1,A1)`, 0x00E5C6B6).
extern void OSINFO_$GET_MMAP(void *flags_p, void *counters, void *info,
                             void *ws_data, void *ws_list, void *uid_out,
                             status_$t *status);

#endif /* OSINFO_H */
