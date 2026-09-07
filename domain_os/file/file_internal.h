/*
 * FILE - File Operations Module (Internal Header)
 *
 * Internal types, constants, and helper functions used only within
 * the FILE subsystem.
 */

#ifndef FILE_INTERNAL_H
#define FILE_INTERNAL_H

#include "file/file.h"
#include "uid/uid.h"
#include "ast/ast.h"
#include "acl/acl.h"
#include "time/time.h"
#include "hint/hint.h"
#include "rem_file/rem_file.h"
#include "audit/audit.h"
#include "network/network.h"
#include "route/route.h"
#include "disk/disk.h"
#include "proc1/proc1.h"
#include "vtoc/vtoc.h"
#include "netlog/netlog.h"
#include "name/name.h"

/*
 * ============================================================================
 * Memory Addresses (m68k)
 * ============================================================================
 */

#ifdef M68K_TARGET
/* Lock control block base address */
#define FILE_LOCK_CONTROL_ADDR      0xE82128

/* Lock table base address (58 entries × 300 bytes) */
#define FILE_LOCK_TABLE_ADDR        0xE9F9CC

/* Lock entries base address (1792 entries × 28 bytes) */
#define FILE_LOCK_ENTRIES_ADDR      0xE935CC

/* UID lock eventcount address */
#define FILE_UID_LOCK_EC_ADDR       0xE2C028

/* Secondary table address (58 words) */
#define FILE_LOCK_TABLE2_ADDR       0xEA3DC4
#endif

/*
 * ============================================================================
 * Internal Data Structures
 * ============================================================================
 */

/* Secondary lock table (58 words, purpose TBD) */
extern uint16_t FILE_$LOCK_TABLE2[];

/*
 * Per-UID hash-bucket lock holder array
 *
 * A 17-byte array where each byte holds the low byte of the PID
 * of the process that owns the corresponding UID hash bucket lock.
 * A zero byte means the bucket is free.
 *
 * On m68k, this array is accessed relative to A5 (the process data
 * area pointer) at offset 0. For portability, we expose it as a
 * global array.
 *
 * Used by FILE_$UID_LOCK_ACQUIRE and FILE_$UID_LOCK_RELEASE to
 * implement fine-grained per-UID locking during file delete operations.
 *
 * Hash function: ((uid.high ^ uid.low) folded to 16 bits via XOR) % 17
 */
#define FILE_UID_LOCK_BUCKETS  17
extern uint8_t FILE_$UID_LOCK_HOLDERS[FILE_UID_LOCK_BUCKETS];

/*
 * ============================================================================
 * Internal Helper Functions
 * ============================================================================
 */

/*
 * OS_PROC_SHUTWIRED - Shutdown wired pages (called on access denial)
 *
 * Called when access rights check fails. Purpose is to release
 * any wired pages and resources.
 *
 * Parameters:
 *   status - Output status code
 *
 * Original address: 0x00E5D050
 */
extern void OS_PROC_SHUTWIRED(status_$t *status);

/*
 * FILE_$DELETE_INT - Internal delete handler
 *
 * Declared in file.h, used internally for refcount operations.
 */

/*
 * FILE_$UID_LOCK_ACQUIRE - Acquire per-UID hash-bucket lock
 *
 * Hashes the UID to one of 17 buckets and busy-waits (with EC_$WAITN)
 * until the bucket is free. Stores the low byte of the current PID
 * as the lock holder.
 *
 * The caller MUST hold ML lock 5 when calling this function.
 * While waiting, this function temporarily releases and re-acquires
 * ML lock 5.
 *
 * Parameters:
 *   uid - UID to lock (used for hash computation only)
 *
 * Original address: 0x00E5D0A8
 */
void FILE_$UID_LOCK_ACQUIRE(uid_t *uid);

/*
 * FILE_$UID_LOCK_RELEASE - Release per-UID hash-bucket lock
 *
 * Releases a per-UID hash-bucket lock previously acquired by
 * FILE_$UID_LOCK_ACQUIRE. Clears the lock holder byte and advances
 * the FILE_$UID_LOCK_EC eventcount to wake any waiters.
 *
 * The caller MUST hold ML lock 5 when calling this function.
 *
 * Parameters:
 *   uid - UID whose lock to release (must match the UID passed to acquire)
 *
 * Original address: 0x00E5D134
 */
void FILE_$UID_LOCK_RELEASE(uid_t *uid);

/*
 * ============================================================================
 * Internal Locking Data Structures and Globals
 * ============================================================================
 */

/*
 * File lock entry structure (detailed)
 * Size: 28 bytes (0x1C)
 *
 * This is the complete structure for each lock entry.
 * Entries are stored at DAT_00e935b0 (base offset for entry N = N * 0x1C)
 */
typedef struct file_lock_entry_detail_t {
    uint32_t    context;        /* 0x00: Lock context (param_7/param_5) */
    uint32_t    node_low;       /* 0x04: Node address low (or local node info) */
    uint32_t    node_high;      /* 0x08: Node address high */
    uint32_t    uid_high;       /* 0x0C: File UID high part */
    uint32_t    uid_low;        /* 0x10: File UID low part */
    uint16_t    next;           /* 0x14: Next entry in chain (hash or free) */
    uint16_t    sequence;       /* 0x16: Lock sequence number */
    uint8_t     refcount;       /* 0x18: Reference count */
    uint8_t     flags1;         /* 0x19: Flags - bit 7=remote, bits 0-5=rights mask */
    uint8_t     rights;         /* 0x1A: Access rights mask */
    uint8_t     flags2;         /* 0x1B: Flags - bit 7=side, bits 3-6=lock mode,
                                         bit 2=remote flag, bit 1=pending, bit 0=? */
} file_lock_entry_detail_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(file_lock_entry_detail_t, context)   == 0x00, "lot.context");
_Static_assert(offsetof(file_lock_entry_detail_t, node_low)  == 0x04, "lot.node_low");
_Static_assert(offsetof(file_lock_entry_detail_t, node_high) == 0x08, "lot.node_high");
_Static_assert(offsetof(file_lock_entry_detail_t, uid_high)  == 0x0C, "lot.uid_high");
_Static_assert(offsetof(file_lock_entry_detail_t, uid_low)   == 0x10, "lot.uid_low");
_Static_assert(offsetof(file_lock_entry_detail_t, next)      == 0x14, "lot.next");
_Static_assert(offsetof(file_lock_entry_detail_t, sequence)  == 0x16, "lot.sequence");
_Static_assert(offsetof(file_lock_entry_detail_t, refcount)  == 0x18, "lot.refcount");
_Static_assert(offsetof(file_lock_entry_detail_t, flags1)    == 0x19, "lot.flags1");
_Static_assert(offsetof(file_lock_entry_detail_t, rights)    == 0x1A, "lot.rights");
_Static_assert(offsetof(file_lock_entry_detail_t, flags2)    == 0x1B, "lot.flags2");
_Static_assert(sizeof(file_lock_entry_detail_t)              == 0x1C, "sizeof lot entry");
#endif

/*
 * ----------------------------------------------------------------------------
 * Object location descriptor (32 bytes)
 *
 * FILE_$PRIV_LOCK builds one of these at A6-0x48 and hands it to
 * AST_$GET_ATTRIBUTES (0x00E5F752), AST_$LOAD_AOTE (0x00E5FB76) and
 * REM_FILE_$LOCK (0x00E5EEE0).  AST_$GET_ATTRIBUTES reads the caller's UID
 * from +0x08 (`lea (0x8,A4),A0` at 0x00E047D2) and, on success, overwrites
 * the whole 32-byte record with the AOTE's copy at aote+0x9C
 * (0x00E0492C / 0x00E049B4).
 * ----------------------------------------------------------------------------
 */
typedef struct file_$obj_loc_t {
    uint32_t    reserved_00[2];     /* 0x00: filled in by AST_$GET_ATTRIBUTES */
    uid_t       uid;                /* 0x08: object UID (set by the caller) */
    uint32_t    loc_info;           /* 0x10: location word, copied to entry+0x08 */
    uint32_t    node;               /* 0x14: node id, copied to entry+0x04 */
    uint32_t    reserved_18;        /* 0x18 */
    int8_t      rights_bits;        /* 0x1C: OR'ed into lock entry flags1 (0x00E5EC6C) */
    int8_t      flags;              /* 0x1D: bit 7 = object is remote,
                                     *       bit 6 = scratch flag used by PRIV_LOCK */
    uint16_t    reserved_1e;        /* 0x1E */
} file_$obj_loc_t;

#if defined(ARCH_M68K)
_Static_assert(offsetof(file_$obj_loc_t, uid)         == 0x08, "obj_loc.uid");
_Static_assert(offsetof(file_$obj_loc_t, loc_info)    == 0x10, "obj_loc.loc_info");
_Static_assert(offsetof(file_$obj_loc_t, node)        == 0x14, "obj_loc.node");
_Static_assert(offsetof(file_$obj_loc_t, rights_bits) == 0x1C, "obj_loc.rights_bits");
_Static_assert(offsetof(file_$obj_loc_t, flags)       == 0x1D, "obj_loc.flags");
_Static_assert(sizeof(file_$obj_loc_t)                == 0x20, "sizeof obj_loc");
#endif

/* file_$obj_loc_t.flags bits */
#define FILE_OBJ_LOC_REMOTE     0x80    /* bit 7: object lives on another node */
#define FILE_OBJ_LOC_SCRATCH    0x40    /* bit 6: cleared/undefined scratch bit */

/*
 * One entry of the hint vector HINT_$GET_HINTS fills in.  FILE_$PRIV_LOCK
 * indexes it 1-based (base A6-0x30, element i at A6-0x30+i*8) and passes
 * &hints[1] (A6-0x28) to HINT_$GET_HINTS.
 */
typedef struct file_$lock_hint_t {
    uint32_t    loc_info;           /* 0x00 */
    uint32_t    node;               /* 0x04 */
} file_$lock_hint_t;

#if defined(ARCH_M68K)
_Static_assert(sizeof(file_$lock_hint_t) == 8, "sizeof lock hint");
#endif

/* HINT_$GET_HINTS never reports more than five entries (hint/get_hints.c),
 * and FILE_$PRIV_LOCK's frame only has room for hints[1..5]. */
#define FILE_LOCK_MAX_HINTS     5

/*
 * Fields FILE_$PRIV_LOCK reads out of the AST_$GET_ATTRIBUTES record
 * (0x00E5F768, 0x00E5F77C, 0x00E5F7A4).  The record is a raw 0x90-byte
 * image, so the offsets are spelled out rather than typed.
 */
#define FILE_ATTR_NOT_EMPTY(a)  (((const uint8_t *)(a))[0])   /* +0x00 */
#define FILE_ATTR_OBJ_TYPE(a)   (((const uint8_t *)(a))[1])   /* +0x01: 1,2 = directory */
#define FILE_ATTR_VOL_FLAGS(a)  (*(const uint16_t *)(const void *)((const uint8_t *)(a) + 2))

/*
 * Lock entry flags (flags2 byte at offset 0x1B)
 */
#define FILE_LOCK_F2_SIDE       0x80    /* Lock side (0=reader, 1=writer) */
#define FILE_LOCK_F2_MODE_MASK  0x78    /* Lock mode (bits 3-6) */
#define FILE_LOCK_F2_MODE_SHIFT 3
#define FILE_LOCK_F2_REMOTE     0x04    /* Remote lock flag */
#define FILE_LOCK_F2_PENDING    0x02    /* Lock pending */
#define FILE_LOCK_F2_FLAG0      0x01    /* Unknown flag */

/*
 * Lock entry flags (flags1 byte at offset 0x19)
 */
#define FILE_LOCK_F1_REMOTE     0x80    /* Remote lock indicator */
#define FILE_LOCK_F1_RIGHTS     0x3F    /* Rights mask bits */

/*
 * Per-process lock table
 * Located at 0xE9F9CA (= FILE_LOCK_TABLE_ADDR - 2)
 * Each process (by ASID) has 300 bytes:
 *   - Bytes 0-1: Lock count at offset 0x1D98 (relative to base + ASID*2)
 *   - Bytes 2-299: Lock index array (up to 149 entries, 2 bytes each)
 */
#define FILE_PROC_LOCK_TABLE_ADDR   0xE9F9CA
#define FILE_PROC_LOCK_ENTRY_SIZE   300     /* 0x12C */
#define FILE_PROC_LOCK_MAX_ENTRIES  150     /* 0x96 entries per process */

/*
 * Lock hash table (array of head pointers)
 * Located at FILE_$LOCK_CONTROL + 0xC8
 */
extern uint16_t FILE_$LOT_HASHTAB[];

/*
 * External lock control variables (in file_lock_control_t)
 */

/* FILE_$LOT_FREE - Head of free lock entry list (at offset 0x2CE) */
/* Already declared in file.h */

/* Highest allocated lock entry index (at offset 0x2CC) */
extern uint16_t FILE_$LOT_HIGH;

/* Lock sequence counter (at offset 0x2C4) */
extern uint32_t FILE_$LOT_SEQN;

/* Lock mode compatibility tables */
extern uint16_t FILE_$LOCK_MODE_TABLE[];      /* At offset 0x58 (24 entries) */
extern uint16_t FILE_$LOCK_COMPAT_TABLE[];    /* At offset 0x28 (12 entries) */
extern uint16_t FILE_$LOCK_MAP_TABLE[];       /* At offset 0x40 (12 entries) */
extern uint16_t FILE_$LOCK_REQ_TABLE[];       /* At offset 0x88 (12 entries) */
extern uint16_t FILE_$LOCK_CVT_TABLE[];       /* At offset 0xA0 (12 entries) */
extern uint16_t FILE_$LOCK_ILLEGAL_MASK;      /* At offset 0x2C8 - illegal modes */

/*
 * Count of lock entries whose remote negotiation is still outstanding.
 * FILE_$PRIV_LOCK bumps it at 0x00E5F954 and drops it again at 0x00E5FA8C,
 * 0x00E5FAB4 and 0x00E5FAE2.  Word at FILE_$LOCK_CONTROL + 0x2CA (0xE823F2).
 */
extern uint16_t FILE_$LOT_PENDING;

/*
 * Lock conflict matrix, 8 entries, at FILE_$LOCK_CONTROL + 0x18 (0xE82140).
 * Indexed by the *mapped* mode (FILE_$LOCK_MODE_TABLE[side][mode]); bit M of
 * the entry is set when a held lock whose mapped mode is M may coexist with
 * the request (FILE_$PRIV_LOCK_$CHECK_CONFLICTS 0x00E5EF8E / 0x00E5F04A).
 */
extern uint16_t FILE_$LOCK_CONFLICT_TABLE[8];

/* Domain booleans: 0xFF is true, tested with tst.b/bmi. */
extern int8_t   FILE_$LOT_FULL;               /* At offset 0x2D0 - table full flag */

/*
 * ----------------------------------------------------------------------------
 * Lock table addressing
 *
 * Both tables are 1-based in the image:
 *   lock entry N          at 0x00E935B0 + N*0x1C  (so the array base
 *                         0x00E935CC is entry 1)
 *   process ASID slot I   at 0x00E9F9CA + ASID*300 + I*2 (so the array base
 *                         0x00E9F9CC is slot 1)
 * On a host build we address the C globals instead so the code is testable.
 * ----------------------------------------------------------------------------
 */
#if defined(ARCH_M68K)
#define FILE_$LOT_BASE          ((file_lock_entry_detail_t *)0x00E935CCUL)
#define FILE_$PROC_LOT_BASE     ((uint8_t *)0x00E9F9CCUL)
#define FILE_$PROC_LOT_CNT_BASE ((uint16_t *)0x00EA3DC4UL)
#else
#define FILE_$LOT_BASE          ((file_lock_entry_detail_t *)FILE_$LOCK_ENTRIES)
#define FILE_$PROC_LOT_BASE     ((uint8_t *)FILE_$LOCK_TABLE)
#define FILE_$PROC_LOT_CNT_BASE (FILE_$LOCK_TABLE2)
#endif

#define FILE_$LOT_ENTRY(n)      (&FILE_$LOT_BASE[(int32_t)(n) - 1])
#define FILE_$PROC_LOT_SLOT(asid, idx)                                        \
    (*(uint16_t *)(FILE_$PROC_LOT_BASE                                        \
                   + (int32_t)(asid) * FILE_LOCK_TABLE_ENTRY_SIZE             \
                   + ((int32_t)(idx) - 1) * 2))
#define FILE_$PROC_LOT_COUNT(asid)  (FILE_$PROC_LOT_CNT_BASE[(int32_t)(asid)])

/* ML resource id guarding the lock tables (`move.w #0x5,-(SP)` before every
 * ML_$LOCK / ML_$UNLOCK in FILE_$PRIV_LOCK, FILE_$PRIV_UNLOCK and
 * FILE_$FORK_LOCK). */
#define FILE_LOT_ML_LOCK_ID     5

/* ASID group mapping table (12 entries) - same address as LOCK_MAP_TABLE on m68k */
extern uint16_t FILE_$ASID_MAP[];

/* Default initial file size */
extern uint32_t FILE_$DEFAULT_SIZE;

/* AUDIT_$ENABLED comes from audit/audit.h, NETLOG_$OK_TO_LOG from netlog/netlog.h */

/* PROC1_$AS_ID (current process ASID) comes from proc1/proc1.h */

/*
 * ============================================================================
 * Internal Locking Functions
 * ============================================================================
 */

/*
 * FILE_$PRIV_LOCK - Core locking function
 *
 * Main internal function for all lock operations. Called by FILE_$LOCK,
 * FILE_$LOCK_D, FILE_$CHANGE_LOCK_D, and remote lock handlers.
 *
 * Parameters:
 *   file_uid   A6+0x08  UID of the object to lock
 *   asid       A6+0x0C  process ASID (usually PROC1_$AS_ID)
 *   side       A6+0x0E  lock "side" (0 or 1); anything else is rejected
 *   lock_mode  A6+0x10  requested lock mode, 0..11
 *   local_only A6+0x12  Pascal boolean: refuse to go off-node
 *   flags      A6+0x14  FILE_LOCK_FLAG_* word
 *   key        A6+0x16  caller-supplied lock key (remote requests)
 *   rem_key    A6+0x18  remote requester's lock key (entry+0x00)
 *   rem_node   A6+0x1C  remote requester's node (entry+0x04)
 *   rem_extra  A6+0x20  remote requester's extra word (entry+0x08)
 *   acl_ctx    A6+0x24  pointer to the ACL context pointer for
 *                       ACL_$RIGHTS_CHECK (0x00E5EDDE)
 *   rem_wait   A6+0x28  wait word forwarded to REM_FILE_$LOCK
 *   slot_io    A6+0x2A  in/out: per-process lock slot number
 *   rights_out A6+0x2E  out: rights word granted
 *   status_ret A6+0x32  out: status code
 *
 * Original address: 0x00E5F0EE
 */
void FILE_$PRIV_LOCK(uid_t *file_uid, int16_t asid, uint16_t side,
                     uint16_t lock_mode, boolean local_only,
                     uint16_t flags, uint16_t key,
                     uint32_t rem_key, uint32_t rem_node, uint32_t rem_extra,
                     void **acl_ctx, uint16_t rem_wait,
                     uint32_t *slot_io, uint16_t *rights_out,
                     status_$t *status_ret);

/*
 * FILE_$PRIV_UNLOCK - Core unlock function
 *
 * Main internal function for all unlock operations.
 *
 * Parameters:
 *   file_uid     - UID of file to unlock
 *   lock_index   - Lock table index (0 to search)
 *   mode_asid    - Combined: high word=lock_mode, low word=ASID
 *   remote_flags - Remote operation flags
 *   param_5      - Context high
 *   param_6      - Context low (node address)
 *   dtv_out      - Output: data-time-valid info
 *   status_ret   - Output: status code
 *
 * Returns:
 *   Result byte (modified flag)
 *
 * Original address: 0x00E5FD32
 */
uint8_t FILE_$PRIV_UNLOCK(uid_t *file_uid, uint16_t lock_index,
                          uint32_t mode_asid, int32_t remote_flags,
                          int32_t param_5, int32_t param_6,
                          uint32_t *dtv_out, status_$t *status_ret);

/*
 * FILE_$PRIV_UNLOCK_ALL - Unlock all locks for a process
 *
 * Releases all locks held by one or all processes.
 *
 * Parameters:
 *   asid_ptr - Pointer to ASID (0 = all processes)
 *
 * Original address: 0x00E60BD0
 */
void FILE_$PRIV_UNLOCK_ALL(uint16_t *asid_ptr);

/*
 * Internal lock info structure (34 bytes)
 * Output format for FILE_$LOCAL_READ_LOCK and FILE_$READ_LOCK_ENTRYI
 *
 * Note: This structure has 34 bytes total (8 longs + 1 short when copied).
 * The holder_node field at offset 0x16 is unaligned for 32-bit access,
 * so this structure should be packed on non-m68k architectures.
 *
 * For local locks (remote_flag=0):
 *   holder_node/port = NODE_$ME/ROUTE_$PORT (we are the holder)
 *   owner_node = entry.node_low (who locked it)
 *   remote_info = entry.node_high
 *
 * For remote locks (remote_flag=1):
 *   holder_node/port = entry.node_low/high (remote holder)
 *   owner_node = NODE_$ME (we are the owner)
 *   remote_info = ROUTE_$PORT
 */
typedef struct {
    uid_t    file_uid;      /* 0x00: File UID (8 bytes) */
    uint32_t context;       /* 0x08: Lock context */
    uint32_t owner_node;    /* 0x0C: Owner's node address (who initiated the lock) */
    uint16_t side;          /* 0x10: Lock side (0=reader, 1=writer) */
    uint16_t mode;          /* 0x12: Lock mode */
    uint16_t sequence;      /* 0x14: Lock sequence number */
    uint32_t holder_node;   /* 0x16: Lock holder's node (who actually holds it) */
    uint32_t holder_port;   /* 0x1A: Lock holder's port */
    uint32_t remote_info;   /* 0x1E: Remote node/port info (4 bytes, total=34) */
} file_lock_info_internal_t;

/*
 * FILE_$READ_LOCK_ENTRYI - Read lock entry by iteration
 *
 * Iterates through lock entries, returning info about each.
 *
 * Parameters:
 *   file_uid   - File UID to search for
 *   index      - Pointer to iteration index (starts at 1)
 *   info_out   - Output buffer for lock info
 *   status_ret - Output status code
 *
 * Original address: 0x00E6093C
 */
void FILE_$READ_LOCK_ENTRYI(uid_t *file_uid, uint16_t *index,
                             file_lock_info_internal_t *info_out, status_$t *status_ret);

/*
 * FILE_$READ_LOCK_ENTRYUI - Read lock entry by UID (unchecked)
 *
 * Reads lock entry info for a file without access checks.
 *
 * Parameters:
 *   file_uid   - File UID to search for
 *   info_out   - Output buffer for lock info
 *   status_ret - Output status code
 *
 * Original address: 0x00E6046E
 */
void FILE_$READ_LOCK_ENTRYUI(uid_t *file_uid, void *info_out, status_$t *status_ret);

/*
 * FILE_$LOCAL_READ_LOCK - Read local lock entry data
 *
 * Searches the local lock table for a lock on the specified file
 * and returns the lock information.
 *
 * Parameters:
 *   file_uid   - UID of file to query
 *   info_out   - Output buffer for lock info (34 bytes)
 *   status_ret - Output status code
 *
 * Original address: 0x00E6050E
 */
void FILE_$LOCAL_READ_LOCK(uid_t *file_uid, file_lock_info_internal_t *info_out, status_$t *status_ret);

/*
 * Extended lock entry query structure
 * Passed from callers like FILE_$READ_LOCK_ENTRYUI
 * Contains both the file UID and process identification
 */
typedef struct {
    uid_t file_uid;      /* 0x00: File UID (8 bytes) */
    uint16_t side;       /* 0x10: Lock side (0=reader, 1=writer) from flags2 bit 7 */
    uint16_t asid;       /* 0x12: Process ASID to check */
} lock_verify_request_t;

/*
 * FILE_$LOCAL_LOCK_VERIFY - Verify local lock ownership
 *
 * Checks if the specified file is locked by the process identified
 * in the request structure.
 *
 * Parameters:
 *   request    - Lock verification request containing file UID and process info
 *   status_ret - Output status code
 *
 * Original address: 0x00E6081C
 */
void FILE_$LOCAL_LOCK_VERIFY(lock_verify_request_t *request, status_$t *status_ret);

/*
 * FILE_$VERIFY_LOCK_HOLDER - Verify lock holder is still valid
 *
 * Checks if the lock holder is still holding the lock. If the lock
 * has been released, cleans up the stale entry.
 *
 * Parameters:
 *   lock_info  - Lock information structure (from read operations)
 *   status_ret - Output status code
 *
 * Original address: 0x00E60732
 */
void FILE_$VERIFY_LOCK_HOLDER(file_lock_info_internal_t *lock_info, status_$t *status_ret);

/*
 * Helper: Audit lock/unlock operations
 * Called when AUDIT_$ENABLED is set
 *
 * Original address: 0x00E5E88A (renamed from FUN_*)
 */
void FILE_$AUDIT_LOCK(status_$t status, uid_t *file_uid, uint16_t lock_mode);

/*
 * ============================================================================
 * Internal Protection/ACL Functions
 * ============================================================================
 */

/*
 * FILE_$SET_PROT_INT - Set file protection (internal)
 *
 * Core internal function for setting file protection. Handles both local
 * and remote files, ACL checking, and locksmith privileges.
 *
 * Parameters:
 *   file_uid    - UID of file to modify
 *   acl_data    - ACL data buffer (44 bytes)
 *   attr_type   - Protection attribute type
 *   prot_type   - Protection type being set
 *   subsys_flag - Subsystem data flag (negative to allow override)
 *   status_ret  - Output status code
 *
 * Original address: 0x00E5DD08
 */
void FILE_$SET_PROT_INT(uid_t *file_uid, void *acl_data, uint16_t attr_type,
                        uint16_t prot_type, int16_t subsys_flag,
                        status_$t *status_ret);

/*
 * FILE_$CHECK_SAME_VOLUME - Check if two files are on the same volume
 *
 * Checks if two file UIDs refer to objects on the same volume.
 * Used during protection operations to verify source/target locations.
 *
 * Parameters:
 *   file_uid1     - First file UID
 *   file_uid2     - Second file UID
 *   copy_location - If negative, copy location info on remote hit
 *   location_out  - Output buffer for location info (32 bytes)
 *   status_ret    - Output status code
 *
 * Returns:
 *   0: Files are on different volumes or error occurred
 *   -1: Files are local and on same logical volume
 *
 * Original address: 0x00E5E476
 */
int8_t FILE_$CHECK_SAME_VOLUME(uid_t *file_uid1, uid_t *file_uid2,
                                int8_t copy_location, uint32_t *location_out,
                                status_$t *status_ret);

/*
 * FILE_$AUDIT_SET_PROT - Log audit event for protection changes
 *
 * Logs a protection/ACL change audit event if auditing is enabled.
 *
 * Parameters:
 *   file_uid   - UID of file being modified
 *   acl_data   - ACL data buffer (44 bytes)
 *   prot_info  - Protection info (8 bytes)
 *   prot_type  - Protection type being set
 *   status     - Status of the operation
 *
 * Original address: 0x00E5DC96
 */
void FILE_$AUDIT_SET_PROT(uid_t *file_uid, void *acl_data, void *prot_info,
                          uint16_t prot_type, status_$t status);

/*
 * ============================================================================
 * External ACL Functions
 * ============================================================================
 */

/*
 * ACL_$SET_ACL_CHECK, ACL_$GET_LOCAL_LOCKSMITH, ACL_$CONVERT_FUNKY_ACL and
 * ACL_$DEF_ACLDATA are declared in acl/acl.h (included above).
 */

/*
 * NOTE: AST functions (AST_$GET_DISM_SEQN, AST_$GET_COMMON_ATTRIBUTES, etc.)
 * are declared in ast/ast.h which is included above.
 */

/*
 * NOTE: REM_FILE_$* functions are declared in rem_file/rem_file.h
 * which is included above. No need to redeclare them here.
 */

#endif /* FILE_INTERNAL_H */
