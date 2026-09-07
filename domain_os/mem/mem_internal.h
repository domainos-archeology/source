/*
 * mem/mem_internal.h - Memory support subsystem internal API
 *
 * Internal declarations shared only by the MEM subsystem's implementation
 * files.  Every non-test .c file in mem/ includes this header first.
 */

#ifndef MEM_INTERNAL_H
#define MEM_INTERNAL_H

#include "mem/mem.h"

/*
 * Per-page parity error tracking entry (MEM_$PAGE_ERRORS, 0xE22942).
 * Each entry is 18 bytes:
 *   - 4 bytes: Physical address
 *   - 2 bytes: Error count
 *   - 12 bytes: Reserved/padding
 */
typedef struct {
    uint32_t    phys_addr;      /* Physical address of failing page */
    uint16_t    error_count;    /* Number of errors at this address */
    uint8_t     reserved[12];   /* Padding to 18 bytes */
} mem_$page_error_t;

/*
 * Memory parity log tracking (from MEM subsystem)
 *
 * Tracks parity errors by memory board and by page.
 * Located at 0xE22930 on m68k.
 */

/* Number of per-page error tracking records */
#define MEM_PARITY_PAGE_RECORDS 4

/* Memory parity record for tracking errors per page */
/* PACKED: m68k aligns 32-bit fields to 2 bytes, so the recovered offsets
 * below are only reproducible on a 4/8-byte-aligning host if the record is
 * packed.  Packing changes no m68k layout. */
typedef struct mem_parity_record_t {
  uint32_t phys_addr;   /* 0x00: Physical address */
  uint16_t count;       /* 0x04: Error count for this page */
  uint8_t reserved[12]; /* 0x06: Padding to 0x12 bytes */
} __attribute__((packed)) mem_parity_record_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(mem_parity_record_t, phys_addr) == 0x00, "mem_parity_record_t.phys_addr");
_Static_assert(__builtin_offsetof(mem_parity_record_t, count) == 0x04, "mem_parity_record_t.count");
_Static_assert(__builtin_offsetof(mem_parity_record_t, reserved) == 0x06, "mem_parity_record_t.reserved");

/* Memory parity log globals structure */
/* PACKED: m68k aligns 32-bit fields to 2 bytes, so the recovered offsets
 * below are only reproducible on a 4/8-byte-aligning host if the record is
 * packed.  Packing changes no m68k layout. */
typedef struct mem_parity_log_t {
  uint16_t reserved_00[4]; /* 0x00: Reserved */
  uint16_t board1_count;   /* 0x08: Errors on board 1 (< 0x300000) */
  uint16_t board2_count;   /* 0x0A: Errors on board 2 (>= 0x300000) */
  uint16_t reserved_0c[3]; /* 0x0C: Reserved */
  mem_parity_record_t
      records[MEM_PARITY_PAGE_RECORDS]; /* 0x12: Per-page records */
} __attribute__((packed)) mem_parity_log_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(mem_parity_log_t, reserved_00) == 0x00, "mem_parity_log_t.reserved_00");
_Static_assert(__builtin_offsetof(mem_parity_log_t, board1_count) == 0x08, "mem_parity_log_t.board1_count");
_Static_assert(__builtin_offsetof(mem_parity_log_t, board2_count) == 0x0A, "mem_parity_log_t.board2_count");
_Static_assert(__builtin_offsetof(mem_parity_log_t, reserved_0c) == 0x0C, "mem_parity_log_t.reserved_0c");
_Static_assert(__builtin_offsetof(mem_parity_log_t, records) == 0x12, "mem_parity_log_t.records");

#if defined(ARCH_M68K)
#define MEM_PARITY_LOG (*(mem_parity_log_t *)0xE22930)
#define MEM_BOARD1_COUNT (*(uint16_t *)0xE2293A)
#define MEM_BOARD2_COUNT (*(uint16_t *)0xE2293C)
#define MEM_PARITY_RECORDS ((mem_parity_record_t *)0xE22942)
#else
extern mem_parity_log_t mem_parity_log;
#define MEM_PARITY_LOG mem_parity_log
#define MEM_BOARD1_COUNT mem_parity_log.board1_count
#define MEM_BOARD2_COUNT mem_parity_log.board2_count
#define MEM_PARITY_RECORDS mem_parity_log.records
#endif

/* Memory board boundary (3MB mark) */
#define MEM_BOARD_BOUNDARY 0x300000


/*
 * TODO(source-eqom, 0xE22942): mem_parity_record_t above and mem_$page_error_t
 * describe the same 18-byte record at 0xE22942, and on a non-m68k build
 * MEM_PARITY_RECORDS resolves to mem_parity_log.records rather than to
 * MEM_$PAGE_ERRORS; the two views need collapsing into one object.  Carried
 * over unchanged from parity/parity_internal.h so this pass changes no layout.
 */

#endif /* MEM_INTERNAL_H */
