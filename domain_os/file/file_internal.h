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
 * file_lock_entry_detail_t - one slot of the lock object table (LOT).
 * Size: 28 bytes (0x1C).  This is the ONLY model of the table; the second,
 * wrong one that used to sit in file/file.h was removed by bead source-0sgi.
 *
 * Layout proof - FILE_$PRIV_LOCK_$ALLOC_ENTRY (0x00E5EB98..0x00E5ED57):
 *   0x00E5EBCA  movea.l #0xe935cc,A2        ; A2 = table base
 *   0x00E5EBC2  lsl.l #0x2,D2 / neg.l / lsl.l #0x3 / add.l   ; D2 = index*0x1C
 *   0x00E5EBFE  lea (0x0,A2,D2*0x1),A1      ; A1 = 0xE935CC + index*0x1C
 * and then writes ONLY negative displacements off A1, -0x1C through -0x01:
 *   0x00E5EC14  move.l (0x18,A0),(-0x1c,A1)   -> +0x00 context
 *   0x00E5EC20  move.l (0x1c,A0),(-0x18,A1)   -> +0x04 node_low
 *   0x00E5EC26  move.l (0x20,A0),(-0x14,A1)   -> +0x08 node_high
 *   0x00E5EC06  move.l (A2)+,(-0x10,A1)       -> +0x0C uid_high
 *   0x00E5EC0A  move.l (A2)+,(-0xc,A1)        -> +0x10 uid_low
 *   0x00E5EBD2  move.w (-0x8,A2,D2*0x1),...   -> +0x14 next (free-list pop)
 *   0x00E5EC1A  move.w (0x16,A0),(-0x6,A1)    -> +0x16 sequence
 *   0x00E5EC42  clr.b (-0x4,A1)               -> +0x18 refcount
 *   0x00E5EC58  andi.b #0x7f,(-0x3,A1)        -> +0x19 flags1
 *   0x00E5EC74  andi.b #0x7f,(-0x1,A1)        -> +0x1B flags2
 * A1 is therefore the END of the entry named by `index`, so entry `index`
 * starts at 0xE935CC + (index-1)*0x1C: THE TABLE IS 1-BASED, and the array
 * base 0xE935CC is entry 1.  FILE_$LOCK_INIT confirms it independently - its
 * free-list loop (0x00E32788-0x00E327A8) pre-biases A0 by one entry
 * (`movea.l #0xe935cc,A0` then `lea (0x1c,A0),A0`) and starts its index
 * counter D1 at 1.  Address entries with FILE_$LOT_ENTRY() below, never by
 * adding index*0x1C to the base.
 *
 * (+0x1A, `rights`, is not written by ALLOC_ENTRY; FILE_$CHECK_PROT reads it
 * at (-0x2,A2) - 0x00E5D1B8.)
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

_Static_assert(__builtin_offsetof(file_lock_entry_detail_t, context)   == 0x00, "lot.context");
_Static_assert(__builtin_offsetof(file_lock_entry_detail_t, node_low)  == 0x04, "lot.node_low");
_Static_assert(__builtin_offsetof(file_lock_entry_detail_t, node_high) == 0x08, "lot.node_high");
_Static_assert(__builtin_offsetof(file_lock_entry_detail_t, uid_high)  == 0x0C, "lot.uid_high");
_Static_assert(__builtin_offsetof(file_lock_entry_detail_t, uid_low)   == 0x10, "lot.uid_low");
_Static_assert(__builtin_offsetof(file_lock_entry_detail_t, next)      == 0x14, "lot.next");
_Static_assert(__builtin_offsetof(file_lock_entry_detail_t, sequence)  == 0x16, "lot.sequence");
_Static_assert(__builtin_offsetof(file_lock_entry_detail_t, refcount)  == 0x18, "lot.refcount");
_Static_assert(__builtin_offsetof(file_lock_entry_detail_t, flags1)    == 0x19, "lot.flags1");
_Static_assert(__builtin_offsetof(file_lock_entry_detail_t, rights)    == 0x1A, "lot.rights");
_Static_assert(__builtin_offsetof(file_lock_entry_detail_t, flags2)    == 0x1B, "lot.flags2");
_Static_assert(sizeof(file_lock_entry_detail_t)              == 0x1C, "sizeof lot entry");

/*
 * The lock object table itself.  FILE_$LOCK_INIT links entries 1..1792
 * (`move.w #0x6ff,D0w` at 0x00E32784 = 0x700 dbf iterations); the extra
 * trailing slot is the free-list terminator - entry 1792's `next` is set to
 * 1793 (0x00E3279E) and slot 1793 is never written, so it keeps the zero that
 * ends the chain in FILE_$PRIV_LOCK_$ALLOC_ENTRY (`tst.w`/`beq` at
 * 0x00E5EBB4).  On the m68k image that zero is BSS beyond 0xE9B1CC.
 */
extern file_lock_entry_detail_t FILE_$LOCK_ENTRIES[FILE_LOCK_ENTRY_COUNT + 1];

/*
 * Word at 0xE9F9C4, 8 bytes below the per-process lock table base 0xE9F9CC.
 * FILE_$LOCK_INIT clears it (0x00E327AC) and that is the only reference to the
 * address in the image.
 * The SR10.2 SAU2 link map settles the question as far as it can be settled:
 * sau2.10.2.tar's sau2/domain_os.map is the map for THIS image (it places
 * FILE_$LOCK_INIT at E32744, our address), and it puts 0xE9F9C4 inside the
 * segment `D71  E935CC  FILE_$LOT_DATA  size = 1086C` - E935CC..EA3E38, which
 * ends exactly where the per-ASID count array does.  That segment exports NO
 * symbols at all, so the map cannot name the cell.  Nor can the code: the only
 * table bases the image ever loads in 16-bit displacement range of 0xE9F9C4
 * are #0xE935CC, #0xE97294 and #0xEA202C, and every displacement taken off
 * them in the lock routines is -0x2662 (= 0xE9F9CC).  The word is
 * write-only.  (source-9r49, closed as not-nameable.)
 */
extern uint16_t FILE_$LOT_E9F9C4;

/*
 * file_$obj_loc_t (the 32-byte object-location descriptor) is shared with
 * rem_file/ - REM_FILE_$LOCK and REM_FILE_$UNLOCK both take one - so it lives
 * in the public header file/file.h.
 */

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
 * FILE_$LOT_HASHTAB is FILE_$LOCK_CONTROL.lock_map - the 251 words at
 * FILE_$LOCK_CONTROL + 0xC8 (0xE821F0, the SAU2 map's own name for the
 * address).  It is a macro in file/file.h, not an object of its own.
 */

/*
 * file_$lot_hash_modulus - the single in-code word at 0x00E5EA28 (00 FB = 251)
 * that FILE_$DELETE_INT (0x00E5E8FE), FILE_$LOCAL_READ_LOCK (0x00E60528),
 * FILE_$PRIV_LOCK (0x00E5F18C) and FILE_$PRIV_UNLOCK (0x00E5FD5A) each pass to
 * UID_$HASH by reference with `pea (d,PC)`.  Defined in file/file_data.c.
 */
extern uint16_t file_$lot_hash_modulus;

/*
 * file_$nil_cell - the single in-code longword of zeroes at 0x00E5E61E that
 * FILE_$PURIFY, FILE_$FW_FILE and FILE_$PRIV_UNLOCK pass as AST_$PURIFY's
 * segment list and FILE_$LOCK, FILE_$LOCK_D and FILE_$CHANGE_LOCK_D pass as
 * FILE_$PRIV_LOCK's ACL context, all with `pea (d,PC)`.  See file/file_data.c
 * for the six displacements.  Nothing reads through it on those paths.
 */
extern uint32_t file_$nil_cell;

/*
 * file_$zero_bytes - the two in-code zero bytes at 0x00E5D380 that
 * FILE_$SET_ATTRIBUTE (0x00E5D348) and FILE_$PRIV_LOCK's CHECK_RIGHTS helper
 * (0x00E5EE1C) pass as ACL_$RIGHTS' `ignore_super` and FILE_$SET_DTM
 * (0x00E5E2AE) passes as FILE_$SET_DTM_F's flags.  All three read byte 0.
 */
extern uint8_t file_$zero_bytes[2];

/*
 * file_$truncate_nil_context - the in-code zero longword at 0x00E73FAC that
 * FILE_$SET_LEN (0x00E73F98) and FILE_$TRUNCATE (0x00E73FD2) both pass as
 * FILE_$TRUNCATE_D's domain-context argument.
 */
extern uint32_t file_$truncate_nil_context;

/*
 * External lock control variables (in file_lock_control_t)
 */

/*
 * FILE_$LOT_FREE (+0x2CE), FILE_$LOT_HIGH (+0x2CC) and FILE_$LOT_SEQN
 * (+0x2C4) are macros over FILE_$LOCK_CONTROL fields; see file/file.h.
 */

/* Lock mode compatibility tables */
extern uint16_t FILE_$LOCK_MODE_TABLE[];      /* At offset 0x58 (24 entries) */
extern uint16_t FILE_$LOCK_COMPAT_TABLE[];    /* At offset 0x28 (12 entries) */
extern uint16_t FILE_$LOCK_MAP_TABLE[];       /* At offset 0x40 (12 entries) */
extern uint16_t FILE_$LOCK_REQ_TABLE[];       /* At offset 0x88 (12 entries) */
extern uint16_t FILE_$LOCK_CVT_TABLE[];       /* At offset 0xA0 (12 entries) */
/* FILE_$LOCK_ILLEGAL_MASK is FILE_$LOCK_CONTROL.lock_illegal_mask (+0x2C8). */

/*
 * Count of lock entries whose remote negotiation is still outstanding.
 * FILE_$PRIV_LOCK bumps it at 0x00E5F954 and drops it again at 0x00E5FA8C,
 * 0x00E5FAB4 and 0x00E5FAE2.  Word at FILE_$LOCK_CONTROL + 0x2CA (0xE823F2),
 * spelled as a macro in file/file.h.
 */

/*
 * Lock conflict matrix, 8 entries, at FILE_$LOCK_CONTROL + 0x18 (0xE82140).
 * Indexed by the *mapped* mode (FILE_$LOCK_MODE_TABLE[side][mode]); bit M of
 * the entry is set when a held lock whose mapped mode is M may coexist with
 * the request (FILE_$PRIV_LOCK_$CHECK_CONFLICTS 0x00E5EF8E / 0x00E5F04A).
 */
extern uint16_t FILE_$LOCK_CONFLICT_TABLE[8];

/* Domain booleans: 0xFF is true, tested with tst.b/bmi.
 * FILE_$LOT_FULL is FILE_$LOCK_CONTROL.flag_2d0 (+0x2D0), read as a SIGNED
 * byte; see the macro in file/file.h. */

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

/* Lock-mode canonicalisation table, 12 entries at FILE_$LOCK_CONTROL+0x40
 * (0xE82168).  FILE_$LOCAL_LOCK_VERIFY indexes it with an ENTRY'S LOCK MODE
 * (`move.w D0w,D1w / add.w D1w,D1w / cmp.w (0x40,A5,D1w*0x1),D2w` at
 * 0x00E608C0-0x00E608C8), not with an ASID - it was called FILE_$ASID_MAP
 * before source-9dc2 established what the compared field is. */
extern uint16_t FILE_$LOCK_MODE_MAP[];

/* FILE_$DEFAULT_SIZE is FILE_$LOCK_CONTROL.default_size (+0x2C0). */

/* AUDIT_$ENABLED comes from audit/audit.h, NETLOG_$OK_TO_LOG from netlog/netlog.h */

/* PROC1_$AS_ID (current process ASID) comes from proc1/proc1.h */

/*
 * ============================================================================
 * Internal Locking Functions
 * ============================================================================
 */

/* FILE_$PRIV_LOCK: declared in file/file.h -- audit/, dir/, name/, pacct/ and
 * rem_file/ call it (bead source-3uo). */

/* FILE_$PRIV_UNLOCK: declared in file/file.h -- same callers as
 * FILE_$PRIV_LOCK (bead source-3uo). */

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

/* file_lock_info_internal_t: declared in file/file.h -- REM_FILE_$SERVER
 * builds one (bead source-3uo). */

/* FILE_$READ_LOCK_ENTRYI: declared in file/file.h -- rem_file/ and svc/ call
 * it (bead source-3uo). */

/* FILE_$READ_LOCK_ENTRYUI (0x00E6046E) is declared in file/file.h - it is
 * called from name/ (name_$old_add_link 0x00E5687E). */

/* FILE_$LOCAL_READ_LOCK: declared in file/file.h -- rem_file/ calls it
 * (bead source-3uo). */

/* lock_verify_request_t: declared in file/file.h -- rem_file/ builds one
 * (bead source-3uo). */

/* FILE_$LOCAL_LOCK_VERIFY: declared in file/file.h -- rem_file/ calls it
 * (bead source-3uo). */

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

/* FILE_$SET_PROT_INT: declared in file/file.h -- rem_file/ calls it
 * (bead source-3uo). */

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
