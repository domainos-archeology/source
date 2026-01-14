/*
 * MST - Memory Segment Table
 *
 * The MST subsystem manages virtual memory address spaces. It provides:
 * - Address Space ID (ASID) allocation for processes
 * - Mapping between virtual addresses and segment numbers
 * - Segment table management for private and global memory regions
 *
 * Memory Layout (M68020):
 * - Segments 0x000-0x677: Private A (process-local)
 * - Segments 0x678-0x757: Global A (shared)
 * - Segments 0x758-0x75f: Private B (8 segments)
 * - Segments 0x760-0x7ff: Global B (shared)
 * - Segment 0x800+: Beyond addressable memory
 *
 * Each segment covers 32KB (0x8000 bytes) of virtual address space.
 * Segment number = virtual_address >> 15
 *
 * Virtual address layout:
 *   bits 31-15: segment number
 *   bits 14-10: page within segment (5 bits = 32 pages per segment)
 *   bits 9-0:   offset within page (1KB pages)
 */

#ifndef MST_H
#define MST_H

#include "base/base.h"
#include "ml/ml.h"

/*
 * MST status codes (module 0x04 = MST)
 */
#define status_$reference_to_illegal_address 0x00040004 /* Invalid VA */
#define status_$mst_object_not_found 0x00040001 /* Object UID not found */
#define status_$no_asid_available 0x00040006    /* No free ASIDs */
#define status_$no_space_available 0x00040003   /* Segment table full */
#define status_$mst_segment_modified 0x0004000a /* COW segment modified */
#define status_$mst_access_violation 0x00040005 /* Access rights violation */

/*
 * Lock identifiers used with ML_$LOCK/ML_$UNLOCK (declared in ml/ml.h)
 */
#define MST_LOCK_ASID 0x0c /* ASID allocation lock */
#define MST_LOCK_AST 0x12  /* AST (Active Segment Table) lock */
#define MST_LOCK_MMU 0x14  /* MMU operations lock */

/*
 * Segment table configuration
 * These are the default values; M68020 systems use different values
 * set in MST_$PRE_INIT
 */
typedef struct {
  uint16_t seg_tn;        /* 0x148: Total number of segments */
  uint16_t global_b_size; /* 0x14a: Size of global B region */
  uint16_t _reserved_14c;
  uint16_t seg_global_b;         /* 0x14e: First segment in global B */
  uint16_t seg_global_b_offset;  /* 0x150: Offset for global B mapping */
  uint16_t seg_high;             /* 0x152: Highest segment number */
  uint16_t seg_private_b;        /* 0x154: First segment in private B */
  uint16_t seg_private_b_end;    /* 0x156: Last segment in private B */
  uint16_t private_a_size;       /* 0x158: Size of private A region */
  uint16_t seg_private_a_end;    /* 0x15a: Last segment in private A */
  uint16_t seg_global_a;         /* 0x15c: First segment in global A */
  uint16_t global_a_size;        /* 0x15e: Size of global A region */
  uint16_t seg_global_a_end;     /* 0x160: Last segment in global A */
  uint16_t seg_private_b_offset; /* 0x162: Offset for private B mapping */
} mst_config_t;

/*
 * MST entry - describes a single segment mapping
 * Each entry is 16 bytes and describes the mapping for one segment
 */
typedef struct {
  uid_t uid;            /* 0x00: Object UID for this segment */
  uint16_t area_id;     /* 0x08: Area identifier */
  uint16_t flags;       /* 0x0a: Flags and cached AST index */
  uint8_t page_info;    /* 0x0c: Page count info */
  uint8_t _reserved[3]; /* 0x0d-0x0f */
} mst_entry_t;

/*
 * MSTE flags field bits
 */
#define MSTE_FLAG_AST_MASK 0x01ff      /* Cached AST entry index */
#define MSTE_FLAG_WRITABLE 0x0002      /* Segment is writable */
#define MSTE_FLAG_COPY_ON_WRITE 0x0008 /* Copy-on-write enabled */
#define MSTE_FLAG_MODIFIED 0x4000      /* Segment has been modified */
#define MSTE_FLAG_ACTIVE 0x8000        /* Segment is currently active */

/*
 * Global variables (extern declarations)
 * Actual addresses are in the kernel data segment around 0xe24xxx
 */
extern uint16_t MST__SEG_TN;               /* Total number of segments */
extern uint16_t MST__GLOBAL_A_SIZE;        /* Global A segment count */
extern uint16_t MST__SEG_GLOBAL_A;         /* First global A segment */
extern uint16_t MST__SEG_GLOBAL_A_END;     /* Last global A segment */
extern uint16_t MST__PRIVATE_A_SIZE;       /* Private A segment count */
extern uint16_t MST__SEG_PRIVATE_A_END;    /* Last private A segment */
extern uint16_t MST__SEG_PRIVATE_B;        /* First private B segment */
extern uint16_t MST__SEG_PRIVATE_B_END;    /* Last private B segment */
extern uint16_t MST__SEG_PRIVATE_B_OFFSET; /* Private B offset in tables */
extern uint16_t MST__SEG_GLOBAL_B;         /* First global B segment */
extern uint16_t MST__SEG_GLOBAL_B_OFFSET;  /* Global B offset in tables */
extern uint16_t MST__SEG_HIGH;             /* Highest segment number */
extern uint16_t MST__SEG_MEM_TOP;          /* Top of addressable memory */
extern uint16_t MST__GLOBAL_B_SIZE;        /* Global B segment count */
extern uint16_t MST__TOUCH_COUNT;          /* Touch-ahead page count */
extern uint16_t MST__MST_PAGES_WIRED;      /* Number of wired MST pages */
extern uint16_t MST__MST_PAGES_LIMIT;      /* Maximum MST pages to wire */

/*
 * ASID list - bitmap tracking allocated ASIDs
 * Bit set = ASID is allocated
 */
extern uint8_t MST__ASID_LIST[8]; /* Bitmap for 58 ASIDs (0-57) */
#define MST_MAX_ASIDS 58          /* 0x3a */

/*
 * Per-ASID base table
 * Maps ASID to starting index in the MST page table array
 */
extern uint16_t MST_ASID_BASE[MST_MAX_ASIDS];

/*
 * MST base - array of segment table indices, one word per segment
 * Located at 0xee5800
 */
extern uint16_t MST[];

/*
 * Page table area base
 * Located at 0xef6400
 * Contains mst_entry_t structures for each segment
 * Segment entries are at offset (MST[segno] * 0x400) relative to this base
 */
#define MST_PAGE_TABLE_BASE 0xef6400

/*
 * Function prototypes
 */

/* Initialization */
void MST_$PRE_INIT(void);
void MST_$INIT(void);
void MST_$DISKLESS_INIT(void);

/* ASID management */
uint16_t MST_$ALLOC_ASID(status_$t *status_ret);
void MST_$DEALLOCATE_ASID(uint16_t asid, status_$t *status_ret);
void MST_$FREE_ASID(uint16_t asid, status_$t *status_ret);

/* Address translation */
uint16_t MST_$VA_TO_SEGNO(uint32_t virtual_addr, uint16_t *segno_out,
                          uint16_t default_result);

/* Bit manipulation for ASID bitmap */
void MST_$SET(void *bitmap, uint16_t size, uint16_t bit_index);
void MST_$SET_CLEAR(void *bitmap, uint16_t size, uint16_t bit_index);

/* Memory mapping */
uint32_t MST_$TOUCH(uint32_t virtual_addr, status_$t *status_ret,
                    int16_t wire_flag);
void MST_$MAP(uid_$t *uid, uint32_t *start_va_ptr, uint32_t *length_ptr,
              uint16_t *area_id_ptr, uint32_t *area_size_ptr,
              uint8_t *rights_ptr, int32_t *mapped_len, status_$t *status_ret);
void MST_$MAP_AT(void);
void MST_$MAP_CANNED_AT(void);
void MST_$MAP_AREA(void);
void MST_$MAP_AREA_AT(void);
void MST_$MAP_GLOBAL(uid_$t *uid, uint32_t *start_va_ptr, uint32_t *length_ptr,
                     uint16_t *area_id_ptr, uint32_t *area_size_ptr,
                     uint8_t *rights_ptr, int32_t *mapped_len,
                     status_$t *status_ret);
void MST_$MAP_TOP(uid_$t *uid, uint32_t *start_va_ptr, uint32_t *length_ptr,
                  uint16_t *area_id_ptr, uint32_t *area_size_ptr,
                  uint8_t *rights_ptr, int32_t *mapped_len,
                  status_$t *status_ret);
void MST_$MAP_INITIAL_AREA(void);
void MST_$MAPS(void);
void MST_$MAPS_AT(void);
void MST_$REMAP(void);
void MST_$REMAP_PRIVI(void);
void MST_$GROW_AREA(void);

/* Unmapping */
void MST_$UNMAP(uid_$t *uid, uint32_t *start_va_ptr, uint32_t *length_ptr,
                status_$t *status_ret);
void MST_$UNMAP_GLOBAL(void);
void MST_$UNMAPS(void);
void MST_$UNMAP_AND_FREE_AREA(void);
void MST_$UNMAPS_AND_FREE_AREA(void);
void MST_$UNMAP_ALL(void);
void MST_$UNMAP_PRIVI(int16_t mode, uid_t *uid, uint32_t start, uint32_t size,
                      uint16_t asid, status_$t *status_ret);

/* Segment operations */
uint32_t MST_$FIND(uint32_t virt_addr, uint16_t flags);
void MST_$REMOVE_SEG(uint32_t param_1, uint32_t param_2, uint16_t param_3,
                     uint16_t param_4, uint8_t flags);
uint32_t MST_$WIRE(uint32_t vpn, status_$t *status_ret);
void MST_$WIRE_AREA(void);
void MST_$INVALIDATE(void);
void MST_$CHANGE_RIGHTS(void);
void MST_$SET_GUARD(void);

/* Query functions */
void MST_$GET_UID(uint32_t *va_ptr, uid_$t *uid_out, uint32_t *adjusted_va,
                  status_$t *status_ret);
void MST_$GET_UID_ASID(uint16_t *asid_p, uint32_t *va_ptr, uid_$t *uid_out,
                       uint32_t *adjusted_va, status_$t *status_ret);
void MST_$GET_VA_INFO(uint16_t *asid_p, uint32_t *va_ptr, uid_$t *uid_out,
                      uint32_t *adjusted_va, void *param_5, int8_t *active_flag,
                      int8_t *modified_flag, status_$t *status_ret);
void MST_$GET_PRIVATE_SIZE(void);

/* Touch-ahead control */
void MST_$PRIV_SET_TOUCH_AHEAD_CNT(void);
void MST_$SET_TOUCH_AHEAD_CNT(void);

/* Fork support */
void MST_$FORK(void);

/* Internal helper functions */
void mst_$unwire_page(void);
void mst_$unwire_asid_pages(uint16_t start, uint16_t end);

#endif /* MST_H */
