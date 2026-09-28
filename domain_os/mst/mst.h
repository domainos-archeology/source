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
#include "ast/ast.h"   /* locate_request_t, used by MST_$REMOVE_SEG */

/*
 * MST status codes (module 0x04 = MST)
 */
#define status_$reference_to_illegal_address 0x00040004 /* Invalid VA */
#define status_$mst_object_not_found 0x00040001 /* Object UID not found */
#define status_$no_asid_available 0x00040006    /* No free ASIDs */
#define status_$no_space_available 0x00040003   /* Segment table full */
#define status_$mst_guard_fault 0x0004000a /* "guard fault" (SR10.4 stcodes 4000a) */
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
  uint16_t _reserved_14c;        /* 0x14c: written 0x7e0 by MST_$PRE_INIT */
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
 * The comment offsets above are MST module-data offsets (A5-relative), not
 * offsets within this record: MST_$PRE_INIT writes the 14 config words to the
 * absolute addresses 0xE2444A..0xE24464 (00e30a04..00e30a6c), so the record
 * begins at module offset MST_CONFIG_BASE_OFF and each _Static_assert
 * subtracts that base.
 */
#define MST_CONFIG_BASE_OFF 0x148
_Static_assert(__builtin_offsetof(mst_config_t, seg_tn) == 0x148 - MST_CONFIG_BASE_OFF,
               "mst_config_t.seg_tn");
_Static_assert(__builtin_offsetof(mst_config_t, global_b_size) == 0x14A - MST_CONFIG_BASE_OFF,
               "mst_config_t.global_b_size");
_Static_assert(__builtin_offsetof(mst_config_t, _reserved_14c) == 0x14C - MST_CONFIG_BASE_OFF,
               "mst_config_t._reserved_14c");
_Static_assert(__builtin_offsetof(mst_config_t, seg_global_b) == 0x14E - MST_CONFIG_BASE_OFF,
               "mst_config_t.seg_global_b");
_Static_assert(__builtin_offsetof(mst_config_t, seg_global_b_offset) == 0x150 - MST_CONFIG_BASE_OFF,
               "mst_config_t.seg_global_b_offset");
_Static_assert(__builtin_offsetof(mst_config_t, seg_high) == 0x152 - MST_CONFIG_BASE_OFF,
               "mst_config_t.seg_high");
_Static_assert(__builtin_offsetof(mst_config_t, seg_private_b) == 0x154 - MST_CONFIG_BASE_OFF,
               "mst_config_t.seg_private_b");
_Static_assert(__builtin_offsetof(mst_config_t, seg_private_b_end) == 0x156 - MST_CONFIG_BASE_OFF,
               "mst_config_t.seg_private_b_end");
_Static_assert(__builtin_offsetof(mst_config_t, private_a_size) == 0x158 - MST_CONFIG_BASE_OFF,
               "mst_config_t.private_a_size");
_Static_assert(__builtin_offsetof(mst_config_t, seg_private_a_end) == 0x15A - MST_CONFIG_BASE_OFF,
               "mst_config_t.seg_private_a_end");
_Static_assert(__builtin_offsetof(mst_config_t, seg_global_a) == 0x15C - MST_CONFIG_BASE_OFF,
               "mst_config_t.seg_global_a");
_Static_assert(__builtin_offsetof(mst_config_t, global_a_size) == 0x15E - MST_CONFIG_BASE_OFF,
               "mst_config_t.global_a_size");
_Static_assert(__builtin_offsetof(mst_config_t, seg_global_a_end) == 0x160 - MST_CONFIG_BASE_OFF,
               "mst_config_t.seg_global_a_end");
_Static_assert(__builtin_offsetof(mst_config_t, seg_private_b_offset) == 0x162 - MST_CONFIG_BASE_OFF,
               "mst_config_t.seg_private_b_offset");
_Static_assert(sizeof(mst_config_t) == 0x1C, "mst_config_t size (14 words)");

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

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(mst_entry_t, uid) == 0x00, "mst_entry_t.uid");
_Static_assert(__builtin_offsetof(mst_entry_t, area_id) == 0x08, "mst_entry_t.area_id");
_Static_assert(__builtin_offsetof(mst_entry_t, flags) == 0x0A, "mst_entry_t.flags");
_Static_assert(__builtin_offsetof(mst_entry_t, page_info) == 0x0C, "mst_entry_t.page_info");
_Static_assert(__builtin_offsetof(mst_entry_t, _reserved) == 0x0D, "mst_entry_t._reserved");

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
extern uint16_t MST_$SEG_TN;               /* Total number of segments */
extern uint16_t MST_$GLOBAL_A_SIZE;        /* Global A segment count */
extern uint16_t MST_$SEG_GLOBAL_A;         /* First global A segment */
extern uint16_t MST_$SEG_GLOBAL_A_END;     /* Last global A segment */
extern uint16_t MST_$PRIVATE_A_SIZE;       /* Private A segment count */
extern uint16_t MST_$SEG_PRIVATE_A_END;    /* Last private A segment */
extern uint16_t MST_$SEG_PRIVATE_B;        /* First private B segment */
extern uint16_t MST_$SEG_PRIVATE_B_END;    /* Last private B segment */
extern uint16_t MST_$SEG_PRIVATE_B_OFFSET; /* Private B offset in tables */
extern uint16_t MST_$SEG_GLOBAL_B;         /* First global B segment */
extern uint16_t MST_$SEG_GLOBAL_B_OFFSET;  /* Global B offset in tables */
extern uint16_t MST_$SEG_HIGH;             /* Highest segment number */
extern uint16_t MST_$SEG_MEM_TOP;          /* Top of addressable memory */
extern uint16_t MST_$GLOBAL_B_SIZE;        /* Global B segment count */
extern uint16_t MST_$TOUCH_COUNT;          /* Touch-ahead page count */
extern uint16_t MST_$MST_PAGES_WIRED;      /* Number of wired MST pages */
extern uint16_t MST_$MST_PAGES_LIMIT;      /* Maximum MST pages to wire */

/*
 * ASID list - bitmap tracking allocated ASIDs
 * Bit set = ASID is allocated
 */
extern uint8_t MST_$ASID_LIST[8]; /* Bitmap for 58 ASIDs (0-57) */
#define MST_MAX_ASIDS 58          /* 0x3a */

/*
 * Per-ASID base table
 * Maps ASID to starting index in the MST page table array
 */
extern uint16_t MST_ASID_BASE[MST_MAX_ASIDS];

/*
 * MST base - array of segment table indices, one word per segment
 *
 * Located at 0xEE5800.  The SAU2 map places MST at EE5800 and the next
 * object, PIT_PAGES, at EE6400, so the table is 0xC00 bytes = 0x600 words.
 * It lives in the uninitialised VM_TABLES region (Ghidra cannot read the
 * bytes), and MST_$INIT allocates and zeroes it a 0x400-byte page at a time
 * (`movea.l #0xee5800,A2` at 0x00E30BFA).
 */
#define MST_TABLE_ENTRIES 0x600
extern uint16_t MST[MST_TABLE_ENTRIES];

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
void MST_$DISKLESS_INIT(int16_t flag, uint32_t mother_node, uint32_t node_me);

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

/*
 * MST_$MAP - Map a file into memory
 *
 * Maps a file object into the current address space.
 *
 * Parameters:
 *   uid         - UID of file to map
 *   start_ptr   - Pointer to starting offset in file (input)
 *   length_ptr  - Pointer to length to map (input)
 *   mode_ptr    - Pointer to mapping mode (input, uint16_t)
 *   extend_ptr  - Pointer to extend value (input)
 *   concur_ptr  - Pointer to concurrency flags (input, uint8_t)
 *   map_info    - Output buffer for mapping info (4 bytes)
 *   status_ret  - Status return
 *
 * Returns:
 *   Pointer to mapped memory (in A0 register)
 *
 * Original address: 0x00E4386C
 */
void *MST_$MAP(uid_t *uid, uint32_t *start_ptr, uint32_t *length_ptr,
               uint16_t *mode_ptr, uint32_t *extend_ptr,
               uint8_t *concur_ptr, void *map_info, status_$t *status_ret);
/*
 * MST_$MAP_AT (0x00E42F54) - map an object at a caller-supplied address.
 *
 * Nine var parameters; the widths come from the routine's own forwarding
 * prologue at 0x00E42F62-0x00E42FA0, which dereferences each one before
 * pushing it on to 0x00E43182:
 *
 *   +0x08 va          `movea.l (0x8,A6),A0` / `move.l (A0),-(SP)`   longword
 *   +0x0C uid         pushed as a pointer (`move.l (0xc,A6),-(SP)`)
 *   +0x10 start       `movea.l (0x10,A6),A4` / `move.l (A4),-(SP)`  longword
 *   +0x14 length      `movea.l (0x14,A6),A3` / `move.l (A3),-(SP)`  longword
 *   +0x18 mode        `movea.l (0x18,A6),A1` / `move.w (A1),-(SP)`  WORD
 *   +0x1C extend      `movea.l (0x1c,A6),A2` / `move.l (A2),-(SP)`  longword
 *   +0x20 concurrency `movea.l (0x20,A6),A0` / `move.b (A0),-(SP)`  BYTE
 *   +0x24 map_info    pushed as a pointer
 *   +0x28 status      pushed as a pointer
 *
 * The parameter types stay `void *` because callers hand it differently
 * shaped scratch cells; the list above is what the callee actually reads.
 * FLOP_$BOOT's `concurrency` cell is the 0x00 byte at 0x00E32538, NOT the
 * 0xFF byte at 0x00E32542 that MST_$MAP gets (source-y89n).
 */
void MST_$MAP_AT(void *start, uid_t *uid, void *param1, void *param2, void *param3,
                 void *param4, void *param5, void *result, status_$t *status);
/*
 * MST_$MAP_CANNED_AT (0x00E30FAA) - map a canned object at a fixed address.
 *
 * Parameter shape recovered from the callee's own frame (A6 displacements at
 * 0x00E30FB2 onwards) and from OS_$INIT's six call sites:
 *
 *   +0x08 va      longword  where to map it
 *   +0x0C uid     longword  pointer to the object's UID
 *   +0x10 offset  longword  byte offset within the object
 *   +0x14 size    longword  number of bytes
 *   +0x18 flags   longword  0x00170001 read/write, 0x00130001 read-only
 *   +0x1C wire    word      Domain boolean, pushed with `st`/`clr.w`
 *   +0x1E touch   word      Domain boolean, read by the callee as
 *                           `move.b (0x1e,A6),D3b` (the slot's high byte)
 *   +0x20 desc    longword  location descriptor
 *   +0x24 status  longword  status return
 *
 * The two booleans were previously merged into a single longword parameter,
 * which lost the distinction between `clr.l` (both false) and
 * `clr.w`/`st` (one of each).
 */
void MST_$MAP_CANNED_AT(uint32_t va, uid_t *uid, uint32_t offset,
                        uint32_t size, uint32_t flags, boolean wire,
                        boolean touch, uint32_t desc, status_$t *status);
void MST_$MAP_AREA(void);
void MST_$MAP_AREA_AT(void *addr_ptr, void *size_ptr, void *param1, void *param2,
                      void *param3, status_$t *status);
void MST_$MAP_GLOBAL(uid_t *uid, uint32_t *start_va_ptr, uint32_t *length_ptr,
                     uint16_t *area_id_ptr, uint32_t *area_size_ptr,
                     uint8_t *rights_ptr, int32_t *mapped_len,
                     status_$t *status_ret);
void MST_$MAP_TOP(uid_t *uid, uint32_t *start_va_ptr, uint32_t *length_ptr,
                  uint16_t *area_id_ptr, uint32_t *area_size_ptr,
                  uint8_t *rights_ptr, int32_t *mapped_len,
                  status_$t *status_ret);
/*
 * MST_$MAP_INITIAL_AREA (0x00E42E9E) frame:
 *   +0x08 code_desc   longword
 *   +0x0C asid        word
 *   +0x0E parent_uid  pointer
 *   +0x12 map_param   longword
 *   +0x16 area_kind   word      (`move.w (0x16,A6),-(SP)` at 0x00E42F08)
 *   +0x18 touch       boolean   (`move.b (0x18,A6),D4b` at 0x00E42EB4)
 *   +0x1A status      pointer
 * PROC2_$CREATE pushes `move.l #0x70000` (area_kind 7, touch false);
 * PROC2_$COMPLETE_VFORK pushes `st` then `move.w #7` (area_kind 7, touch
 * true).  The two used to be merged into one longword parameter.
 */
void MST_$MAP_INITIAL_AREA(uint32_t code_desc, uint16_t asid, uid_t *parent_uid,
                           uint32_t map_param, int16_t area_kind, boolean touch,
                           status_$t *status);
/*
 * MST_$MAPS - map an object, searching the private space from the top
 *
 * Original address: 0x00E43982 (82 bytes).  The whole body is one forwarding
 * call to mst_$alloc_segs (bsr.w 0x00E43182 at 0x00E439C4).
 *
 * Parameter widths, read off the ten accesses the prologue makes to the
 * argument block (frame is `link.w A6,-0x4`, so arguments start at A6+0x08):
 *
 *   +0x08 asid          word      move.w (0x8,A6),-(SP)   0x00E439AA
 *   +0x0A direction     BYTE      move.b (0xa,A6),-(SP)   0x00E43998
 *   +0x0C uid           longword  move.l (0xc,A6),-(SP)   0x00E439BA
 *   +0x10 start_va      longword  move.l (0x10,A6),-(SP)  0x00E439B6
 *   +0x14 length        longword  move.l (0x14,A6),-(SP)  0x00E439B2
 *   +0x18 area_id       word      move.w (0x18,A6),-(SP)  0x00E439A6
 *   +0x1A area_size     longword  move.l (0x1a,A6),-(SP)  0x00E439AE
 *   +0x1E access_rights BYTE      move.b (0x1e,A6),-(SP)  0x00E4399C
 *   +0x20 map_info      longword  move.l (0x20,A6),-(SP)  0x00E43994
 *   +0x24 status        longword  move.l (0x24,A6),-(SP)  0x00E43990
 *
 * The two BYTE parameters are Domain Pascal BOOLEANs.  A byte pushed with
 * `st -(SP)` or `move.b Dn,-(SP)` decrements A7 by two and writes the byte
 * at the resulting (even) address, so it lands in the high half of the word
 * slot - exactly the half the callee reads with `move.b (0xa,A6)`.  A caller
 * therefore passes a plain boolean (0xFF / true), not the word 0xFF00.
 *
 * Returns the mapped virtual address in A0 (mst_$alloc_segs' own A0 result,
 * stored to (-0x4,A6) at 0x00E439C8 and left in A0 across the epilogue).
 */
void *MST_$MAPS(int16_t asid, boolean direction, uid_t *uid, uint32_t start_va,
                uint32_t length, int16_t area_id, uint32_t area_size,
                boolean access_rights, void *map_info, status_$t *status);
void MST_$MAPS_AT(void);
void MST_$REMAP(void);
/*
 * MST_$REMAP_PRIVI - Remap a privileged memory segment
 *
 * Remaps a segment in the current address space. Returns the
 * mapped base address in A0 (m68k calling convention).
 *
 * Parameters:
 *   config1     - Configuration data pointer
 *   va_ptr      - Pointer to current virtual address (in/out)
 *   config2     - Configuration data pointer
 *   offset_ptr  - Pointer to file offset to map
 *   config3     - Configuration data pointer
 *   result_ptr  - Output: size of mapped region
 *   status_ret  - Status return
 *
 * Returns: mapped base address (via A0 register)
 *
 * Original address: 0x00E43A0C
 * Size: 302 bytes
 */
void *MST_$REMAP_PRIVI(void *config1, uint32_t *va_ptr, void *config2,
                        uint32_t *offset_ptr, void *config3,
                        uint32_t *result_ptr, status_$t *status_ret);
void MST_$GROW_AREA(void);

/* Unmapping */

/*
 * MST_$UNMAP - Unmap a file from memory
 *
 * Unmaps a previously mapped file object from the current address space.
 *
 * Parameters:
 *   uid         - UID of file to unmap
 *   start_ptr   - Pointer to starting VA to unmap (as returned/saved from MST_$MAP)
 *   map_info    - Pointer to map_info (as returned from MST_$MAP)
 *   status_ret  - Status return
 *
 * Original address: 0x00E4472E
 */
void MST_$UNMAP(uid_t *uid, uint32_t *start_ptr, uint32_t *map_info,
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
/*
 * MST_$REMOVE_SEG (0x00E0E0D6) - release an ASTE's pages.
 *
 * The first argument is the 12-byte AST_$LOCATE_ASTE request record, passed
 * BY REFERENCE: 0x00E0E0EA "move.l (0x8,A6),-(SP)" hands this longword
 * straight to AST_$LOCATE_ASTE, which dereferences it
 * (0x00E0705E "movea.l (0x8,A6),A2" then "(0xa,A2)"/"(0x8,A2)").  The only
 * caller, MST_$UNMAP_PRIVI, builds it on its own stack and passes its
 * address (0x00E44A30 "pea (-0x400,A2)").
 *
 * Arguments 2-4 (A6+0x0C long, A6+0x10 word, A6+0x12 word) are never read by
 * the body; `flags` is the byte at A6+0x14, forwarded to AST_$RELEASE_PAGES.
 */
void MST_$REMOVE_SEG(locate_request_t *request, uint32_t param_2,
                     uint16_t param_3, uint16_t param_4, boolean flags);
uint32_t MST_$WIRE(uint32_t vpn, status_$t *status_ret);
/*
 * MST_$WIRE_AREA - wire every page of [*start_va_ptr, *end_va_ptr]
 *
 * Recovered from 0x00E44BA4:
 *   (0x08,A6) A0 -> longword start VA          `move.l (A0),D3`
 *   (0x0C,A6) A1 -> longword end VA            `move.l (A1),D1`
 *   (0x10,A6) A2 -> array of longwords, written 1-based as
 *                   `move.l D0,(-0x4,A2,D1w*1)` with D1 = count*4
 *   (0x14,A6) A3 -> word page limit            `cmp.w (A3),D1w`, CRASH_SYSTEM
 *                   (0x00E44BEA) if the count would exceed it
 *   (0x18,A6) A4 -> word page count, cleared at entry and incremented per page
 * The pointer parameters stay `void *` because callers hand it cells of
 * several different declared types.
 */
void MST_$WIRE_AREA(const void *start_va_ptr, const void *end_va_ptr,
                    void *page_list, const void *max_pages_ptr,
                    void *page_count_ret);
void MST_$INVALIDATE(void);
void MST_$CHANGE_RIGHTS(void);
void MST_$SET_GUARD(void);

/* Query functions */
void MST_$GET_UID(uint32_t *va_ptr, uid_t *uid_out, uint32_t *adjusted_va,
                  status_$t *status_ret);
void MST_$GET_UID_ASID(uint16_t *asid_p, uint32_t *va_ptr, uid_t *uid_out,
                       uint32_t *adjusted_va, status_$t *status_ret);
/*
 * MST_$GET_VA_INFO (0x00E4404E): the fifth argument is forwarded unchanged
 * to mst_$va_to_pte as its prot_out WORD (`move.l (0x18,A6),-(SP)` at
 * 0x00E44094) - it stays `void *` here only so existing callers' mocks keep
 * building; active_flag is flags bit 15 (`smi`), modified_flag bit 14.
 */
void MST_$GET_VA_INFO(uint16_t *asid_p, uint32_t *va_ptr, uid_t *uid_out,
                      uint32_t *adjusted_va, void *prot_out, boolean *active_flag,
                      boolean *modified_flag, status_$t *status_ret);
/*
 * MST_$GET_PRIVATE_SIZE (0x00E44AAE) takes four by-reference arguments, not
 * none: ASKNODE_$INTERNET_INFO's request-0x4B arm pushes
 * &asid, &reply+0x08, &reply+0x0C and &status (0x00E65424-0x00E65434).
 */
void MST_$GET_PRIVATE_SIZE(uint16_t *asid_p, uint32_t *size_ret,
                           uint32_t *size2_ret, status_$t *status_ret);

/* Touch-ahead control */
void MST_$PRIV_SET_TOUCH_AHEAD_CNT(void);
void MST_$SET_TOUCH_AHEAD_CNT(void);

/*
 * MST_$FORK (0x00E739F8) - clone the parent's MST segment entries into the
 * child's address space.
 *
 * Parameter shape read off the callee's own frame:
 *   +0x08  asid    word       0x00E73A06  move.w  (0x8,A6),D3w
 *   +0x0A  pid     word       0x00E73B42  move.w  (0xa,A6),-(SP)
 *   +0x0C  flags   longword   0x00E73A0A  move.l  (0xc,A6),D0
 *   +0x10  status  longword   0x00E73B6A  movea.l (0x10,A6),A0
 *
 * and confirmed at the single call site in PROC2_$FORK, which pushes
 * *fork_flags with `move.l (A0),-(SP)` (0x00E72F52) and then cleans up
 * 12 bytes (`lea (0xc,SP),SP` at 0x00E72F62).
 */
void MST_$FORK(uint16_t asid, uint16_t pid, uint32_t flags, status_$t *status);

/* Internal helper functions */
void mst_$unwire_page(void);
void mst_$unwire_asid_pages(uint16_t start, uint16_t end);

#endif /* MST_H */
