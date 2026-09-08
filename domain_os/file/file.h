/*
 * FILE - File Operations Module
 *
 * This module provides file operations for Domain/OS including:
 * - File locking (advisory and mandatory)
 * - File attributes (create, delete, set/get attributes)
 * - File protection and ACLs
 * - Remote file operations (via REM_FILE_$)
 *
 * Memory layout (m68k):
 *   - Lock control block: 0xE82128
 *   - Lock table: 0xE9F9CC (58 entries × 300 bytes)
 *   - Lock entries: 0xE935CC (1792 entries × 28 bytes)
 *   - UID lock eventcount: 0xE2C028
 */

#ifndef FILE_H
#define FILE_H

#include "base/base.h"
#include "ec/ec.h"

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
    uint16_t    reserved_00;        /* 0x00: filled in by AST_$GET_ATTRIBUTES */
    uint16_t    volume;             /* 0x02: volume index.  Two records are on
                                     *       the same volume iff these agree:
                                     *       FILE_$CHECK_SAME_VOLUME
                                     *       `move.w (-0x56,A6),D2w` /
                                     *       `cmp.w (-0x36,A6),D2w` (0x00E5E578)
                                     *       and name_$old_add_link
                                     *       `move.w (-0xbe,A6),D0w` /
                                     *       `cmp.w (-0x9e,A6),D0w` (0x00E568CC) */
    uint32_t    block_hint;         /* 0x04: passed on as the VTOC allocation
                                     *       hint by FILE_$PRIV_CREATE */
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
_Static_assert(offsetof(file_$obj_loc_t, volume)     == 0x02, "obj_loc.volume");
_Static_assert(offsetof(file_$obj_loc_t, block_hint) == 0x04, "obj_loc.block_hint");
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
 * ============================================================================
 * Constants
 * ============================================================================
 */

/* Number of entries in the lock table (hash buckets) */
#define FILE_LOCK_TABLE_ENTRIES     58

/*
 * Number of buckets in the lock hash table (FILE_$LOCK_CONTROL + 0xC8,
 * a.k.a. FILE_$LOT_HASHTAB at 0xE821F0).
 *
 * FILE_$LOCK_INIT clears 0xFB words there (0x00E327BE `move.w #0xfa,D0w` /
 * `clr.w (0xc8,A0)` / `dbf` = 251 iterations), and the same 251 is the
 * modulus every UID_$HASH call site passes by reference from the in-code
 * cell at 0x00E5EA28 (`file_$lot_hash_modulus`), so a hash remainder can be
 * any of 0 .. 250.
 */
#define FILE_LOT_HASH_BUCKETS       251

/* Number of lock entry slots linked by FILE_$LOCK_INIT's free-list loop
 * (`move.w #0x6ff,D0w` at 0x00E32784 -> 0x700 dbf iterations).  Entries are
 * numbered 1..FILE_LOCK_ENTRY_COUNT; see FILE_$LOT_ENTRY(). */
#define FILE_LOCK_ENTRY_COUNT       1792

/* Size of each lock table entry in bytes */
#define FILE_LOCK_TABLE_ENTRY_SIZE  300

/* Size of each lock entry in bytes */
#define FILE_LOCK_ENTRY_SIZE        28

/*
 * File attribute IDs for FILE_$SET_ATTRIBUTE
 */
#define FILE_ATTR_IMMUTABLE         1   /* Immutable flag */
#define FILE_ATTR_TROUBLE           2   /* Trouble flag */
#define FILE_ATTR_TYPE_UID          4   /* Type UID */
#define FILE_ATTR_DIR_PTR           5   /* Directory pointer */
#define FILE_ATTR_DELETE_ON_UNLOCK  7   /* Delete-on-unlock flag */
#define FILE_ATTR_REFCNT            8   /* Reference count */
#define FILE_ATTR_DTM_AST           9   /* DTM (AST compat mode) */
#define FILE_ATTR_DTU_AST           10  /* DTU (AST compat mode) */
#define FILE_ATTR_AUDITED           13  /* Audited flag (0xD) */
#define FILE_ATTR_MGR_ATTR          14  /* Manager attribute */
#define FILE_ATTR_DEVNO             22  /* Device number */
#define FILE_ATTR_DTM_OLD           23  /* DTM (old format) */
#define FILE_ATTR_DTU_FULL          24  /* DTU (full format) */
#define FILE_ATTR_MAND_LOCK         25  /* Mandatory lock flag (0x19) */
#define FILE_ATTR_DTM_CURRENT       26  /* DTM (use current time) */

/*
 * FILE_$SET_ATTRIBUTE's last two value parameters.
 *
 * They are two Pascal words - the required-rights mask at A6+0x12
 * (0x00E5D252, handed to ACL_$RIGHTS at 0x00E5D344 after zero extension) and
 * the ACL option flags at A6+0x14 (0x00E5D338, passed BY REFERENCE) - which
 * the compiler merges into a single `move.l` at most call sites.  The old
 * FILE_FLAGS_*_MASK spellings below are those merged longwords: the HIGH
 * half is the rights word, the LOW half the option word.
 */
#define FILE_ATTR_RIGHTS_NONE       0x0000  /* no ACL check (tst.w D2w) */
#define FILE_ATTR_RIGHTS_WRITE      0x0002
#define FILE_ATTR_RIGHTS_CTRL       0x0008
#define FILE_ATTR_OPTS_NONE         0x0000
#define FILE_ATTR_OPTS_ALL          0xFFFF

/* Deprecated merged spellings, kept so existing references still resolve. */
#define FILE_FLAGS_IMMUTABLE_MASK   0x0002FFFF  /* rights 0x0002, opts 0xFFFF */
#define FILE_FLAGS_TROUBLE_MASK     0x0000FFFF  /* rights 0x0000, opts 0xFFFF */
#define FILE_FLAGS_AUDITED_MASK     0x0000FFFF  /* rights 0x0000, opts 0xFFFF */
#define FILE_FLAGS_MAND_LOCK_MASK   0x00080000  /* rights 0x0008, opts 0x0000 */

/*
 * Attribute buffer sizes
 */
#define FILE_ATTR_INFO_SIZE         0x7A  /* 122 bytes - compact format */
#define FILE_ATTR_FULL_SIZE         0x90  /* 144 bytes - full format */

/*
 * File status codes (module 0x0F)
 */
#define file_$object_not_found                     0x000F0001  /* Object not found */
#define file_$object_is_remote                     0x000F0002  /* Object is remote */
#define file_$bad_reply_received_from_remote_node  0x000F0003  /* Bad reply from remote */
#define file_$comms_problem_with_remote_node        0x000F0004  /* Communication problem with remote node */
#define file_$object_not_locked_by_this_process    0x000F0005  /* Not locked by this process */
#define file_$object_in_use                        0x000F0006  /* Object in use */
#define file_$illegal_lock_request                 0x000F0007  /* Illegal lock request
                                                                 (FILE_$CHANGE_LOCK_D 0x00E5EAC2,
                                                                  FILE_$PRIV_LOCK 0x00E5F154/0x00E5F466) */
#define file_$local_lock_table_full                0x000F0009  /* Lock table full */
/* 0x000F000B is "operation cannot be done from here" in the SR10.4 status
 * database.  file_$cannot_create_on_remote_with_uid is an older, narrower
 * name for the same code, kept for its existing users. */
#define file_$cannot_create_on_remote_with_uid     0x000F000B
#define file_$obj_not_locked_by_this_process       0x000F000C  /* Not locked by this process (alt) */
#define file_$objects_on_different_volumes         0x000F0013  /* Objects on different volumes */
#define file_$invalid_arg                          0x000F0014  /* Invalid argument */
#define file_$incompatible_request                 0x000F0015  /* Incompatible request */
/* "operation cannot be done from here".  Was 0x000F0018, which is not a code
 * the status database defines at all; FILE_$FORCE_UNLOCK stores it with
 * `move.l #0xf000b,(A2)` at 0x00E60DFE and name_$old_add_link with
 * `move.l #0xf000b,(-0xd0,A6)` at 0x00E568BC. */
#define file_$op_cannot_perform_here               0x000F000B
/* The naming-server flavours FILE_$PRIV_LOCK raises - 0x000E0030 for objects
 * whose type byte says "directory" (0x00E5F7D4) and 0x000E000D for a
 * non-empty directory locked for delete (0x00E5F79A) - are declared once in
 * name/name.h, which file/file_internal.h includes. */
/* "object not found" (SR10.4 stcodes f0001).  Single name for this code;
 * dir/ used to spell it status_$wrong_type, os/ status_$special_passthrough
 * and ast/ status_$ast_object_not_found. */
#define status_$file_object_not_found              0x000F0001
/* "volume has been mounted read-only" (SR10.4 stcodes f0016).  Single name for
 * this code; it was also spelled file_$vol_mounted_read_only, file_$invalid_type
 * (file/), status_$out_of_space (vtoc/) and status_$ast_object_special_attribute
 * (ast/).  Raised by FILE_$PRIV_LOCK at 0x00E5F4D2 and 0x00E5F7E2 and by
 * FILE_$PRIV_CREATE (0x00E5C0xx) when the volume has bit 1 of its flags set. */
#define status_$file_volume_has_been_mounted_read_only 0x000F0016
#define status_$insufficient_rights                0x000F0011  /* Insufficient rights */
#define status_$no_rights                          0x000F0010  /* No rights at all */

/*
 * ============================================================================
 * Data Structures
 * ============================================================================
 */

/*
 * The lock-object-table entry (file_lock_entry_detail_t, 0x1C bytes) and the
 * FILE_$LOCK_ENTRIES table itself are private to the FILE subsystem; they live
 * in file/file_internal.h.  Bead source-0sgi removed a second, wrong model of
 * the same table that used to be declared here.
 */

/*
 * File lock table entry structure
 * Size: 300 bytes (0x12C)
 *
 * Hash bucket entry for file lock lookups by UID.
 *
 * FILE_$LOCK_INIT clears all 300 bytes of every row (0x00E3276C clears the
 * word at 0xE9F9CA + 2 = 0xE9F9CC first, and the xref 0x00E3276C -> 0xE9F9CC
 * pins it), so the `header` word is NOT preserved.  Address rows through
 * FILE_$PROC_LOT_SLOT() in file/file_internal.h, which numbers the 150 slots
 * 1-based the way the machine code does.
 */
typedef struct file_lock_table_entry_t {
    uint16_t    header;         /* 0x00: Entry header (preserved during init) */
    uint8_t     data[298];      /* 0x02: Lock chain data (cleared during init) */
} file_lock_table_entry_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(file_lock_table_entry_t, header) == 0x00, "file_lock_table_entry_t.header");
_Static_assert(__builtin_offsetof(file_lock_table_entry_t, data) == 0x02, "file_lock_table_entry_t.data");
_Static_assert(sizeof(file_lock_table_entry_t) == 0x12C, "file_lock_table_entry_t size");

/*
 * File lock control block structure
 * Located at 0xE82128 on m68k
 * Size: at least 721 bytes (0x2D1)
 */
typedef struct file_lock_control_t {
    uint8_t     reserved1[0xb8];    /* 0x00: Reserved/unknown */
    uid_t       base_uid;           /* 0xB8: Base UID (derived from UID_$NIL + NODE_$ME) */
    uid_t       generated_uid;      /* 0xC0: UID generated at init */
    uint16_t    lock_map[FILE_LOT_HASH_BUCKETS];      /* 0xC8: Lock mapping table (cleared at init).
                                     * FILE_$LOCK_INIT clears 0xFB words from
                                     * +0xC8 (00e327be `move.w #0xfa,D0w` /
                                     * `clr.w (0xc8,A0)` / `dbf`), so the array
                                     * ends at 0x2BD. */
    uint8_t     reserved_2be[14];   /* 0x2BE: Untouched by FILE_$LOCK_INIT */
    uint16_t    flag_2cc;           /* 0x2CC: Flag (set to 1 at init) */
    uint16_t    lot_free;           /* 0x2CE: FILE_$LOT_FREE - head of free list */
    uint8_t     flag_2d0;           /* 0x2D0: Flag (cleared at init) */
} file_lock_control_t;

/* Remaining documented offsets (bead source-pewa). */
_Static_assert(__builtin_offsetof(file_lock_control_t, reserved_2be) == 0x2BE, "file_lock_control_t.reserved_2be");

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(file_lock_control_t, reserved1) == 0x00, "file_lock_control_t.reserved1");
_Static_assert(__builtin_offsetof(file_lock_control_t, base_uid) == 0xB8, "file_lock_control_t.base_uid");
_Static_assert(__builtin_offsetof(file_lock_control_t, generated_uid) == 0xC0, "file_lock_control_t.generated_uid");
_Static_assert(__builtin_offsetof(file_lock_control_t, lock_map) == 0xC8, "file_lock_control_t.lock_map");
_Static_assert(__builtin_offsetof(file_lock_control_t, flag_2cc) == 0x2CC, "file_lock_control_t.flag_2cc");
_Static_assert(__builtin_offsetof(file_lock_control_t, lot_free) == 0x2CE, "file_lock_control_t.lot_free");
_Static_assert(__builtin_offsetof(file_lock_control_t, flag_2d0) == 0x2D0, "file_lock_control_t.flag_2d0");

/*
 * ============================================================================
 * External Data References
 * ============================================================================
 */

/* Lock control block */
extern file_lock_control_t FILE_$LOCK_CONTROL;

/* Lock table (58 entries) */
extern file_lock_table_entry_t FILE_$LOCK_TABLE[];

/* UID lock eventcount */
extern ec_$eventcount_t FILE_$UID_LOCK_EC;

/* Free list head (alias into control block) */
extern uint16_t FILE_$LOT_FREE;

/*
 * ============================================================================
 * Initialization Functions
 * ============================================================================
 */

/*
 * FILE_$LOCK_INIT - Initialize file locking subsystem
 *
 * Initializes:
 *   - Lock table (58 entries × 300 bytes)
 *   - Lock entries free list (1792 entries × 28 bytes)
 *   - Lock control block
 *   - UID lock eventcount
 *   - Calls REM_FILE_$UNLOCK_ALL to release any stale remote locks
 *
 * Original address: 0x00E32744
 */
void FILE_$LOCK_INIT(void);

/*
 * ============================================================================
 * File Creation/Deletion Functions
 * ============================================================================
 */

/*
 * FILE_$PRIV_CREATE - Create a file (internal/privileged)
 *
 * Core file creation function used by all public FILE_$CREATE variants.
 *
 * Parameters:
 *   file_type    - Type of file to create (0=default, 1=dir, 2=link, etc.)
 *   type_uid     - UID for typed objects (or UID_$NIL)
 *   dir_uid      - Directory to create file in
 *   file_uid_ret - Receives the new file's UID
 *   initial_size - Initial file size (or 0 for default)
 *   flags        - Creation flags
 *   owner_info   - Owner/ACL info (or NULL)
 *   status_ret   - Receives operation status
 *
 * Original address: 0x00E5D382
 */
uint32_t FILE_$PRIV_CREATE(int16_t file_type, const uid_t *type_uid, uid_t *dir_uid,
                           uid_t *file_uid_ret, uint32_t initial_size,
                           uint16_t flags, uid_t *owner_info, status_$t *status_ret);

/*
 * FILE_$CREATE - Create a new file
 *
 * Creates a default file in the specified directory.
 *
 * Parameters:
 *   dir_uid      - Directory to create file in
 *   file_uid_ret - Receives the new file's UID
 *   status_ret   - Receives operation status
 *
 * Original address: 0x00E5D778
 */
void FILE_$CREATE(uid_t *dir_uid, uid_t *file_uid_ret, status_$t *status_ret);

/*
 * FILE_$CREATE_IT - Create a typed file
 *
 * Creates a file with a specified type.
 *
 * Parameters:
 *   type_ptr     - Pointer to file type value (0, 4, or 5 valid)
 *   type_uid     - UID for the type
 *   dir_uid      - Directory to create file in
 *   size_ptr     - Pointer to initial size
 *   file_uid_ret - Receives the new file's UID
 *   status_ret   - Receives operation status
 *
 * Returns:
 *   Byte indicating success
 *
 * Original address: 0x00E5D79E
 */
uint8_t FILE_$CREATE_IT(int16_t *type_ptr, uid_t *type_uid, uid_t *dir_uid,
                        uint32_t *size_ptr, uid_t *file_uid_ret, status_$t *status_ret);

/*
 * FILE_$DELETE_INT - Delete a file (internal)
 *
 * Internal delete handler used by all public delete functions.
 *
 * Parameters:
 *   file_uid   - UID of file to delete
 *   flags      - Delete flags (bit 0=do delete, bit 1=force, bit 2=set delete-on-unlock)
 *   result     - Receives result byte
 *   status_ret - Receives operation status
 *
 * Returns:
 *   -1 if file was locked, 0 otherwise
 *
 * Original address: 0x00E5E8E0
 */
int8_t FILE_$DELETE_INT(uid_t *file_uid, uint16_t flags, uint8_t *result, status_$t *status_ret);

/*
 * FILE_$DELETE - Delete a file
 *
 * Original address: 0x00E5DB50
 */
void FILE_$DELETE(uid_t *file_uid, status_$t *status_ret);

/*
 * FILE_$DELETE_OBJ - Delete a file object
 *
 * Parameters:
 *   file_uid   - UID of file to delete
 *   force      - If negative, use "force" mode with delete-on-unlock
 *   param_3    - Additional parameter
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5DB12
 */
void FILE_$DELETE_OBJ(uid_t *file_uid, int8_t force, void *param_3, status_$t *status_ret);

/*
 * FILE_$DELETE_FORCE - Force delete a file
 *
 * Original address: 0x00E5DB7A
 */
void FILE_$DELETE_FORCE(uid_t *file_uid, status_$t *status_ret);

/*
 * FILE_$DELETE_WHEN_UNLOCKED - Delete a file when unlocked
 *
 * Original address: 0x00E5FC3A
 */
void FILE_$DELETE_WHEN_UNLOCKED(uid_t *file_uid, status_$t *status_ret);

/*
 * FILE_$DELETE_FORCE_WHEN_UNLOCKED - Force delete when unlocked
 *
 * Original address: 0x00E5FC64
 */
void FILE_$DELETE_FORCE_WHEN_UNLOCKED(uid_t *file_uid, status_$t *status_ret);

/*
 * FILE_$REMOVE_WHEN_UNLOCKED - Remove a file when unlocked
 *
 * Parameters:
 *   file_uid   - UID of file to remove
 *   result     - Receives result byte
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5FC8E
 */
void FILE_$REMOVE_WHEN_UNLOCKED(uid_t *file_uid, uint8_t *result, status_$t *status_ret);

/*
 * ============================================================================
 * File Attribute Functions
 * ============================================================================
 */

/*
 * FILE_$SET_ATTRIBUTE - Set a file attribute
 *
 * Core function for setting file attributes. Handles both local and remote
 * files, checking ACL permissions as needed.
 *
 * Parameters:
 *   file_uid   A6+0x08  UID of the file to modify
 *   attr_id    A6+0x0C  attribute id (see FILE_ATTR_* constants)
 *   value      A6+0x0E  pointer to the attribute value
 *   rights     A6+0x12  required-rights mask; 0 skips the ACL check
 *   options    A6+0x14  ACL option flags (passed by reference to ACL_$RIGHTS)
 *   status_ret A6+0x16  receives the operation status
 *
 * Attribute IDs:
 *   4  = Type UID
 *   5  = Directory pointer
 *   7  = Delete-on-unlock flag
 *   8  = Reference count
 *   9  = DTM (AST compat)
 *   10 = DTU (AST compat)
 *   14 = Manager attribute
 *   22 = Device number
 *   23 = DTM (old format)
 *   24 = DTU (full format)
 *   26 = DTM (use current time)
 *
 * Original address: 0x00E5D242
 */
void FILE_$SET_ATTRIBUTE(uid_t *file_uid, int16_t attr_id, void *value,
                         uint16_t rights, int16_t options,
                         status_$t *status_ret);

/*
 * FILE_$GET_ATTR_INFO - Get file attribute info (compact format)
 *
 * Returns file attributes in a compact 122-byte format (0x7A).
 * Checks lock status if requested via param_2 flags.
 *
 * Parameters:
 *   file_uid   - UID of file
 *   param_2    - Pointer to flags byte (bit 0=check lock, bit 1=check delete, bit 2=skip delete check)
 *   size_ptr   - Pointer to buffer size (must be 0x7A = 122)
 *   loc_rec    - 0x20-byte object-location record.  FILE_$GET_ATTR_INFO
 *                hands its UID field at +0x08 to FILE_$DELETE_INT
 *                (`pea (0x8,A1)` at 0x00E5D838) and, on success, copies the
 *                whole record back out of the local descriptor
 *                AST_$GET_ATTRIBUTES filled (0x00E5D88E, eight longwords)
 *   attr_out   - Output attribute buffer (0x7A bytes; see
 *                file/get_attr_info.c for the recovered layout)
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5D7F4
 */
void FILE_$GET_ATTR_INFO(uid_t *file_uid, void *param_2, int16_t *size_ptr,
                         file_$obj_loc_t *loc_rec, void *attr_out,
                         status_$t *status_ret);

/*
 * FILE_$GET_ATTRIBUTES - Get file attributes (full format)
 *
 * Returns file attributes in full 144-byte format (0x90).
 *
 * Parameters:
 *   file_uid   - UID of file
 *   param_2    - Pointer to flags
 *   size_ptr   - Pointer to buffer size (must be 0x90 = 144)
 *   loc_rec    - 0x20-byte object-location record; the routine hands its
 *                UID field at +0x08 to FILE_$DELETE_INT (`pea (0x8,A3)` at
 *                0x00E5D9C2) and copies the whole record back out of the
 *                local descriptor AST_$GET_ATTRIBUTES filled (0x00E5DA30,
 *                eight longwords)
 *   attr_out   - Output attribute buffer (144 bytes)
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5D984
 */
void FILE_$GET_ATTRIBUTES(uid_t *file_uid, void *param_2, int16_t *size_ptr,
                          file_$obj_loc_t *loc_rec, void *attr_out,
                          status_$t *status_ret);

/*
 * FILE_$ATTRIBUTES - Get file attributes (old format)
 *
 * Returns file attributes converted to old format via VTOCE_$NEW_TO_OLD.
 * Uses attribute flags 0x21.
 *
 * Parameters:
 *   file_uid   - UID of file
 *   attr_out   - Output buffer for old-format attributes
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5DA4C
 */
void FILE_$ATTRIBUTES(uid_t *file_uid, void *attr_out, status_$t *status_ret);

/*
 * FILE_$ACT_ATTRIBUTES - Get active file attributes (old format)
 *
 * Like FILE_$ATTRIBUTES but uses flags 0x01 (locked access).
 *
 * Parameters:
 *   file_uid   - UID of file
 *   attr_out   - Output buffer for old-format attributes
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5DAB0
 */
void FILE_$ACT_ATTRIBUTES(uid_t *file_uid, void *attr_out, status_$t *status_ret);

/*
 * FILE_$SET_TYPE - Set file type UID
 *
 * Parameters:
 *   file_uid - UID of file to modify
 *   type_uid - New type UID
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5E176
 */
void FILE_$SET_TYPE(uid_t *file_uid, uid_t *type_uid, status_$t *status_ret);

/*
 * FILE_$SET_DIRPTR - Set file directory pointer
 *
 * Parameters:
 *   file_uid - UID of file to modify
 *   dir_uid  - UID of parent directory
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5E1B2
 */
void FILE_$SET_DIRPTR(uid_t *file_uid, uid_t *dir_uid, status_$t *status_ret);

/*
 * FILE_$SET_DTM_F - Set Data Time Modified (full version)
 *
 * Sets the DTM attribute. If flag param is negative, uses current time.
 * Falls back to AST_$SET_ATTRIBUTE if incompatible request.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   flags      - Pointer to flags byte (negative = use current time)
 *   time_value - Pointer to time value (uint32_t + uint16_t)
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5E1EE
 */
void FILE_$SET_DTM_F(uid_t *file_uid, int8_t *flags, void *time_value,
                     status_$t *status_ret);

/*
 * FILE_$SET_DTM - Set Data Time Modified (simple version)
 *
 * Wrapper for FILE_$SET_DTM_F with explicit time value.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   time_value - Pointer to time value (uint32_t)
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5E28E
 */
void FILE_$SET_DTM(uid_t *file_uid, uint32_t *time_value, status_$t *status_ret);

/*
 * FILE_$SET_DTU_F - Set Data Time Used (full version)
 *
 * Sets the DTU (access time) attribute.
 * Falls back to AST_$SET_ATTRIBUTE if incompatible request.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   time_value - Pointer to time value (uint32_t + uint16_t)
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5E2C2
 */
void FILE_$SET_DTU_F(uid_t *file_uid, void *time_value, status_$t *status_ret);

/*
 * FILE_$SET_DTU - Set Data Time Used (simple version)
 *
 * Wrapper for FILE_$SET_DTU_F.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   time_value - Pointer to time value (uint32_t)
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5E332
 */
void FILE_$SET_DTU(uid_t *file_uid, uint32_t *time_value, status_$t *status_ret);

/*
 * FILE_$SET_DEVNO - Set device number
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   devno      - Pointer to device number (uint16_t)
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5E362
 */
void FILE_$SET_DEVNO(uid_t *file_uid, uint16_t *devno, status_$t *status_ret);

/*
 * FILE_$SET_MGR_ATTR - Set manager attribute
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   mgr_attr   - Pointer to manager attribute (8 bytes)
 *   version    - Pointer to version (must be 0)
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5E39A
 */
void FILE_$SET_MGR_ATTR(uid_t *file_uid, void *mgr_attr, int16_t *version,
                        status_$t *status_ret);

/*
 * FILE_$SET_REFCNT - Set reference count
 *
 * If reference count is 0xFFFFFFFF, sets to 0.
 * If >= 0xFFF5, sets to -11.
 * If refcnt becomes 0 and status is OK, deletes the file.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   refcnt     - Pointer to new reference count
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5E3F4
 */
void FILE_$SET_REFCNT(uid_t *file_uid, uint32_t *refcnt, status_$t *status_ret);

/*
 * ============================================================================
 * File Flag Functions
 * ============================================================================
 */

/*
 * FILE_$MK_PERMANENT - Mark file as permanent
 *
 * Stub function that always returns success. The permanent flag is
 * likely handled elsewhere in the system.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   status_ret - Receives operation status (always status_$ok)
 *
 * Original address: 0x00E5DBA4
 */
void FILE_$MK_PERMANENT(uid_t *file_uid, status_$t *status_ret);

/*
 * FILE_$MK_TEMPORARY - Mark file as temporary
 *
 * Stub function that always returns success. The temporary flag is
 * likely handled elsewhere in the system.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   status_ret - Receives operation status (always status_$ok)
 *
 * Original address: 0x00E5DBB2
 */
void FILE_$MK_TEMPORARY(uid_t *file_uid, status_$t *status_ret);

/*
 * FILE_$MK_IMMUTABLE - Mark file as immutable
 *
 * Sets the immutable flag on a file, preventing modifications.
 * Uses FILE_$SET_ATTRIBUTE with attr_id=1.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5DBC0
 */
void FILE_$MK_IMMUTABLE(uid_t *file_uid, status_$t *status_ret);

/*
 * FILE_$SET_AUDITED - Set file audit flags
 *
 * Sets the audit flags for a file. Requires audit administrator privileges.
 * The two boolean parameters control different aspects of auditing:
 *   - param_2 < 0 sets bit 0 of the audit flags
 *   - param_3 < 0 sets bit 1 of the audit flags
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   param_2    - Pointer to first audit flag (negative = enable)
 *   param_3    - Pointer to second audit flag (negative = enable)
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5DBE8
 */
void FILE_$SET_AUDITED(uid_t *file_uid, int8_t *param_2, int8_t *param_3,
                       status_$t *status_ret);

/*
 * FILE_$SET_TROUBLE - Set file trouble flag
 *
 * Sets the trouble flag on a file, indicating some issue with the file.
 * Uses FILE_$SET_ATTRIBUTE with attr_id=2.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   unused     - Unused parameter (preserved for API compatibility)
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5DC42
 */
void FILE_$SET_TROUBLE(uid_t *file_uid, void *unused, status_$t *status_ret);

/*
 * FILE_$SET_MAND_LOCK - Set mandatory lock flag
 *
 * Sets or clears the mandatory lock flag on a file.
 * Uses FILE_$SET_ATTRIBUTE with attr_id=25 (0x19).
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   flag       - Pointer to lock flag value
 *   status_ret - Receives operation status
 *
 * Original address: 0x00E5DC6A
 */
void FILE_$SET_MAND_LOCK(uid_t *file_uid, uint8_t *flag, status_$t *status_ret);

/*
 * ============================================================================
 * File Locking Functions
 * ============================================================================
 */

/*
 * Lock option flags for FILE_$PRIV_LOCK's `flags` parameter.
 *
 * This is the Pascal word at A6+0x14; the compiler frequently merges it with
 * the `key` word at A6+0x16 into a single `move.l` (FILE_$LOCK_D pushes
 * 0x00040000 = flags 0x0004, key 0x0000 at 0x00E5EA5C).  Every test in the
 * body is a byte-sized btst against A6+0x15, i.e. the LOW byte of this word,
 * so the bit numbers below are bit numbers within the 16-bit flags word.
 */
#define FILE_LOCK_FLAG_LOCAL_ONLY   0x0001   /* bit 0: skip AST_$COND_FLUSH (0x00E5FB32) */
#define FILE_LOCK_FLAG_REMOTE       0x0002   /* bit 1: request arrived from a remote node
                                              *        (0x00E5F15E); also selects
                                              *        ACL_$RIGHTS_CHECK over ACL_$RIGHTS */
#define FILE_LOCK_FLAG_CHECK_RIGHTS 0x0004   /* bit 2: enforce the mode's rights mask
                                              *        (0x00E5EE58) */
#define FILE_LOCK_FLAG_NO_RIGHTS    0x0008   /* bit 3: skip the rights check entirely
                                              *        (0x00E5F4DC, 0x00E5EDB8) */
#define FILE_LOCK_FLAG_ENTRY_BIT0   0x0020   /* bit 5: copied into lock entry flags2 bit 0
                                              *        (0x00E5ECAE) */
#define FILE_LOCK_FLAG_CHANGE       0x0040   /* bit 6: change an existing lock
                                              *        (0x00E5F1CC and six more sites) */
#define FILE_LOCK_FLAG_FOR_DELETE   0x0080   /* bit 7: lock is being taken to delete the
                                              *        object (0x00E5F790) */
#define FILE_LOCK_FLAG_ACL_CHECK    0x0100   /* bit 8: passed on to ACL_$RIGHTS_CHECK
                                              *        (0x00E5EDEA) */

/* The old longword spellings; FILE_$LOCK_D et al. push flags and key as one
 * longword, so <word flags> == <longword> >> 16. */
#define FILE_LOCK_FLAGS_OF(lw)      ((uint16_t)((lw) >> 16))
#define FILE_LOCK_KEY_OF(lw)        ((uint16_t)((lw) & 0xFFFF))

/*
 * FILE_$LOCK_D - Lock a file with domain context
 *
 * Locks a file with explicit domain (distributed) context.
 *
 * Parameters:
 *   file_uid     - UID of file to lock
 *   lock_index   - Pointer to lock index (from previous lock or 0)
 *   lock_mode    - Pointer to lock mode
 *   rights       - Pointer to rights byte
 *   param_5      - Additional parameter
 *   status_ret   - Output status code
 *
 * Original address: 0x00E5EA2A
 */
void FILE_$LOCK_D(uid_t *file_uid, uint16_t *lock_index, uint16_t *lock_mode,
                  uint8_t *rights, uint32_t *slot_io, status_$t *status_ret);

/*
 * FILE_$CHANGE_LOCK_D - Change an existing lock with domain context
 *
 * Changes the mode of an existing lock.
 *
 * Parameters:
 *   file_uid     - UID of file with existing lock
 *   lock_index   - Pointer to lock index
 *   lock_mode    - Pointer to new lock mode
 *   param_4      - Additional parameter
 *   status_ret   - Output status code
 *
 * Original address: 0x00E5EA9E
 */
void FILE_$CHANGE_LOCK_D(uid_t *file_uid, uint16_t *lock_index, uint16_t *lock_mode,
                         uint32_t *slot_io, status_$t *status_ret);

/*
 * FILE_$LOCK - Lock a file
 *
 * Standard file locking function.
 *
 * Parameters:
 *   file_uid     - UID of file to lock
 *   lock_index   - Pointer to lock index (uint16_t)
 *   lock_mode    - Pointer to lock mode (uint16_t)
 *   rights       - Pointer to rights byte (uint8_t)
 *   lock_info    - Output buffer for lock info (8 bytes)
 *   status_ret   - Output status code
 *
 * Original address: 0x00E5EB20
 */
void FILE_$LOCK(uid_t *file_uid, const uint16_t *lock_index,
                const uint16_t *lock_mode, const uint8_t *rights,
                void *lock_info, status_$t *status_ret);

/*
 * FILE_$UNLOCK_D - Unlock a file with domain context
 *
 * Unlocks a file with explicit domain (distributed) context.
 *
 * Parameters:
 *   file_uid     - UID of file to unlock
 *   lock_index   - Pointer to lock index
 *   lock_mode    - Pointer to lock mode
 *   status_ret   - Output status code
 *
 * Original address: 0x00E5FCC2
 */
void FILE_$UNLOCK_D(uid_t *file_uid, uint32_t *lock_index, uint16_t *lock_mode,
                    status_$t *status_ret);

/*
 * FILE_$UNLOCK - Unlock a file
 *
 * Standard file unlocking function.
 *
 * Parameters:
 *   file_uid     - UID of file to unlock
 *   lock_mode    - Pointer to lock mode
 *   status_ret   - Output status code
 *
 * Original address: 0x00E5FCFC
 */
void FILE_$UNLOCK(uid_t *file_uid, uint16_t *lock_mode, status_$t *status_ret);

/*
 * FILE_$UNLOCK_VOL - Unlock all locks on a volume
 *
 * Releases all locks held on a specific volume.
 *
 * Parameters:
 *   vol_uid      - UID of volume
 *   status_ret   - Output status code
 *
 * Original address: 0x00E60D36
 */
void FILE_$UNLOCK_VOL(uid_t *vol_uid, status_$t *status_ret);

/*
 * FILE_$FORCE_UNLOCK - Force unlock a file
 *
 * Forces release of a lock, even if not held by current process.
 * Only works for locks on the local node.
 *
 * Parameters:
 *   file_uid     - UID of file to unlock
 *   status_ret   - Output status code
 *
 * Original address: 0x00E60DB0
 */
void FILE_$FORCE_UNLOCK(uid_t *file_uid, status_$t *status_ret);

/*
 * FILE_$UNLOCK_PROC - Unlock all locks for a process
 *
 * Releases all locks held by a specific process.
 *
 * Parameters:
 *   proc_uid     - Process UID (or UID_$NIL for current process)
 *   file_uid     - File UID to unlock (or UID_$NIL for all)
 *   lock_mode    - Pointer to lock mode filter
 *   param_4      - Additional parameter
 *   status_ret   - Output status code
 *
 * Original address: 0x00E60E3E
 */
void FILE_$UNLOCK_PROC(uid_t *proc_uid, uid_t *file_uid, uint16_t *lock_mode,
                       uint32_t param_4, status_$t *status_ret);

/*
 * ============================================================================
 * Lock Query Functions
 * ============================================================================
 */

/*
 * FILE_$LOCATE - Get file location from UID
 *
 * Retrieves the location (network node) information for a file object.
 *
 * Parameters:
 *   file_uid     - Pointer to file UID to locate
 *   location_out - Output: receives location info (uint32_t node address)
 *   status_ret   - Output: status code
 *
 * Original address: 0x00E60620
 */
void FILE_$LOCATE(uid_t *file_uid, uint32_t *location_out, status_$t *status_ret);

/*
 * FILE_$LOCATEI - Get file location with diskless fallback
 *
 * Extended version of FILE_$LOCATE that handles diskless client UIDs.
 * If normal location lookup fails and UID appears to be a diskless UID,
 * computes location from the UID structure.
 *
 * Parameters:
 *   file_uid     - Pointer to file UID to locate
 *   location_out - Output: receives location UID (high + low)
 *   status_ret   - Output: status code
 *
 * Original address: 0x00E6067C
 */
void FILE_$LOCATEI(uid_t *file_uid, uid_t *location_out, status_$t *status_ret);

/*
 * FILE_$READ_LOCK_ENTRY - Read lock entry information
 *
 * Reads information about a lock on a file. Iterates through locks.
 *
 * Parameters:
 *   file_uid   - UID of file to query
 *   index      - Pointer to iteration index (starts at 1, updated on return)
 *   info_out   - Output buffer for lock info (26 bytes minimum)
 *   status_ret - Output: status code
 *
 * Original address: 0x00E608EC
 */
void FILE_$READ_LOCK_ENTRY(uid_t *file_uid, uint16_t *index,
                            void *info_out, status_$t *status_ret);

/*
 * FILE_$READ_LOCK_ENTRYU - Read lock entry by UID
 *
 * Reads lock entry information for a file by its UID.
 * Handles both local and remote files.
 *
 * Parameters:
 *   file_uid   - UID of file to query
 *   info_out   - Output buffer for lock info (26 bytes minimum)
 *   status_ret - Output: status code
 *
 * Original address: 0x00E6042E
 */
void FILE_$READ_LOCK_ENTRYU(uid_t *file_uid, void *info_out, status_$t *status_ret);

/*
 * FILE_$IMPORT_LK - Import a lock from another process
 *
 * Validates a lock index and returns the validated index if the lock
 * exists and matches the specified file UID.
 *
 * Parameters:
 *   file_uid   - UID of file the lock should be on
 *   index_in   - Pointer to lock index to validate
 *   index_out  - Output: validated lock index
 *   status_ret - Output: status code
 *
 * Original address: 0x00E603AC
 */
void FILE_$IMPORT_LK(uid_t *file_uid, uint32_t *index_in, uint32_t *index_out,
                      status_$t *status_ret);

/*
 * ============================================================================
 * File Protection/ACL Functions
 * ============================================================================
 */

/*
 * FILE_$CHECK_PROT - Check file protection/access rights
 *
 * Checks if the current process has the requested access rights to a file.
 * First checks a per-process lock cache, then falls back to ACL_$RIGHTS.
 *
 * Parameters:
 *   file_uid     A6+0x08  UID of file to check
 *   access_mask  A6+0x0C  Required access rights mask (word)
 *   slot_num     A6+0x0E  Lock table slot number (0-149, longword)
 *   ignore_super A6+0x12  ACL_$RIGHTS' ignore_super boolean.  0x00E5D226
 *                         `pea (0x12,A6)` passes the ADDRESS of this slot,
 *                         and ACL_$RIGHTS reads the byte there.
 *   option_flags A6+0x14  ACL_$RIGHTS' option-flags word.  0x00E5D216
 *                         `pea (0x14,A6)` passes the address of this slot.
 *   rights_out   A6+0x16  Output: actual rights available
 *   status_ret   A6+0x1A  Output: status code
 *
 * A6+0x12 and A6+0x14 were previously modelled as one 4-byte `void *unused`;
 * they are two distinct 2-byte parameters, each passed on to ACL_$RIGHTS by
 * reference.  Both known callers push them together with a single
 * `clr.l -(SP)` (0x00E73FFC, 0x00E43DAC), i.e. FALSE and 0.
 *
 * Returns:
 *   1 if rights check completed (check status for success/failure)
 *   Result from ACL_$RIGHTS otherwise
 *
 * Original address: 0x00E5D172
 */
int16_t FILE_$CHECK_PROT(uid_t *file_uid, uint16_t access_mask, uint32_t slot_num,
                         boolean ignore_super, int16_t option_flags,
                         uint16_t *rights_out, status_$t *status_ret);

/*
 * FILE_$SET_PROT - Set file protection
 *
 * Sets file protection based on protection type. Maps protection types
 * to attribute IDs and handles both local and remote files.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   prot_type  - Pointer to protection type (0-6)
 *   acl_data   - ACL data buffer (44 bytes)
 *   acl_uid    - ACL UID (8 bytes) - flags encoded in low word
 *   status_ret - Output status code
 *
 * Protection types:
 *   0-5: Standard protection modes (mapped to attr IDs 0x10-0x15)
 *   6: Set protection by ACL UID
 *
 * The acl_uid parameter encodes special flags in the low word:
 *   Bits 4-11 (masked with 0xFF0 >> 4): If bit 4 is set, use default protection
 *
 * Original address: 0x00E5DF3A
 */
/* acl_data is the 44-byte ACL data block (0x00E5DFA8 copies 11 longwords);
 * acl_uid is an 8-byte UID whose low word carries the flag bits
 * (`and.w (0x4,A2),D0w` at 0x00E5DF58). */
void FILE_$SET_PROT(uid_t *file_uid, uint16_t *prot_type, void *acl_data,
                    uid_t *acl_uid, status_$t *status_ret);

/*
 * FILE_$SET_ACL - Set file ACL
 *
 * Sets the access control list for a file using a "funky" ACL format.
 * Converts the ACL format and calls FILE_$SET_PROT.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   acl_uid    - ACL UID in funky format (type encoded in low word)
 *   status_ret - Output status code
 *
 * Original address: 0x00E5E06A
 */
void FILE_$SET_ACL(uid_t *file_uid, uid_t *acl_uid, status_$t *status_ret);

/*
 * FILE_$OLD_AP - Set file access protection (old/legacy interface)
 *
 * Legacy function for setting file access protection.
 * Used for backward compatibility with older protection schemes.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   prot_type  - Pointer to protection type
 *   acl_data   - ACL data buffer (44 bytes)
 *   acl_uid    - ACL UID (8 bytes)
 *   status_ret - Output status code
 *
 * Original address: 0x00E5E100
 */
/* Same argument shape as FILE_$SET_PROT: 0x00E5E132 copies 11 longwords of
 * acl_data and 0x00E5E116 reads 8 bytes of acl_uid. */
void FILE_$OLD_AP(uid_t *file_uid, int16_t *prot_type, void *acl_data,
                  uid_t *acl_uid, status_$t *status_ret);

/*
 * ============================================================================
 * File Write Functions
 * ============================================================================
 */

/*
 * FILE_$FW_FILE - Force Write File
 *
 * Forces all dirty pages of a file to be written back to disk.
 * This ensures file data durability by flushing the kernel buffer cache.
 *
 * Operation:
 *   1. Checks if file is locked (via FILE_$DELETE_INT query)
 *   2. Calls AST_$PURIFY with appropriate flags:
 *      - If locked: flags = 0x0002 (local only)
 *      - If not locked: flags = 0x8002 (include remote sync)
 *
 * Parameters:
 *   file_uid   - UID of file to flush
 *   status_ret - Output status code
 *
 * Original address: 0x00E5E622
 */
void FILE_$FW_FILE(uid_t *file_uid, status_$t *status_ret);

/*
 * FILE_$FW_PARTIAL - Force Write Partial File
 *
 * Forces dirty pages within a byte range to be written back to disk.
 * Iterates through pages covered by the byte range and purifies each.
 *
 * Page size is 32KB (0x8000 bytes). The function calculates the starting
 * page from the offset and walks through subsequent pages until the
 * byte count is exhausted.
 *
 * Parameters:
 *   file_uid     - UID of file to flush
 *   start_offset - Pointer to starting byte offset
 *   byte_count   - Pointer to number of bytes to flush
 *   status_ret   - Output status code
 *
 * Original address: 0x00E5E680
 */
/* byte_count is read as a longword (`move.l (A1),D2` at 0x00E5E6BC). */
void FILE_$FW_PARTIAL(uid_t *file_uid, uint32_t *start_offset,
                      uint32_t *byte_count, status_$t *status_ret);

/*
 * FILE_$FW_PAGES - Force Write Specific Pages
 *
 * Forces specific pages to be written back to disk. Takes a list of
 * page entries where each entry contains:
 *   bits 5-31: page number
 *   bits 0-4:  sub-page index
 *
 * Pages are processed in batches of up to 32, with each batch sorted
 * for sequential I/O optimization before writing.
 *
 * Parameters:
 *   file_uid   - UID of file to flush
 *   page_list  - Array of page entry values
 *   page_count - Pointer to number of pages in list
 *   status_ret - Output status code
 *
 * Original address: 0x00E5E71E
 */
void FILE_$FW_PAGES(uid_t *file_uid, uint32_t *page_list, uint16_t *page_count,
                    status_$t *status_ret);

/*
 * ============================================================================
 * File Miscellaneous Functions
 * ============================================================================
 */

/*
 * FILE_$NEIGHBORS - Check if two files are on the same volume
 *
 * Checks if two files are "neighbors" (located on the same volume).
 * Wrapper around FILE_$CHECK_SAME_VOLUME.
 *
 * Parameters:
 *   file_uid1  - First file UID
 *   file_uid2  - Second file UID
 *   status_ret - Output status code
 *
 * Original address: 0x00E5E5AE
 */
int8_t FILE_$NEIGHBORS(uid_t *file_uid1, uid_t *file_uid2, status_$t *status_ret);

/*
 * FILE_$PURIFY - Purify (flush) a file's dirty pages
 *
 * Flushes dirty pages of a file to disk by calling AST_$PURIFY.
 * This performs a basic purification with default flags.
 *
 * Parameters:
 *   file_uid   - UID of file to purify
 *   status_ret - Output status code
 *
 * Original address: 0x00E5E5F2
 */
void FILE_$PURIFY(uid_t *file_uid, status_$t *status_ret);

/*
 * ============================================================================
 * File Truncation/Length Functions
 * ============================================================================
 */

/*
 * FILE_$SET_LEN - Set file length
 *
 * Sets the length of a file. Equivalent to truncation if shrinking.
 *
 * Parameters:
 *   file_uid   - UID of file to modify
 *   new_length - Pointer to new length value
 *   status_ret - Output status code
 *
 * Original address: 0x00E73F90
 */
void FILE_$SET_LEN(uid_t *file_uid, uint32_t *new_length, status_$t *status_ret);

/*
 * FILE_$SET_LEN_D - Set file length with domain context
 *
 * Sets the length of a file with explicit domain context for
 * distributed locking.
 *
 * Parameters:
 *   file_uid    - UID of file to modify
 *   new_length  - Pointer to new length value
 *   domain_ctx  - Pointer to domain context (lock index)
 *   status_ret  - Output status code
 *
 * Original address: 0x00E73FB0
 */
void FILE_$SET_LEN_D(uid_t *file_uid, uint32_t *new_length,
                     uint32_t *domain_ctx, status_$t *status_ret);

/*
 * FILE_$TRUNCATE - Truncate a file
 *
 * Truncates a file to the specified size.
 *
 * Parameters:
 *   file_uid   - UID of file to truncate
 *   new_size   - Pointer to new size value
 *   status_ret - Output status code
 *
 * Original address: 0x00E73FCA
 */
void FILE_$TRUNCATE(uid_t *file_uid, uint32_t *new_size, status_$t *status_ret);

/*
 * FILE_$TRUNCATE_D - Truncate a file with domain context
 *
 * Truncates a file to the specified size with explicit domain context.
 * This is the core implementation used by FILE_$TRUNCATE and FILE_$SET_LEN.
 *
 * Parameters:
 *   file_uid    - UID of file to truncate
 *   new_size    - Pointer to new size value
 *   domain_ctx  - Pointer to domain context (lock index)
 *   status_ret  - Output status code
 *
 * Original address: 0x00E73FE4
 */
void FILE_$TRUNCATE_D(uid_t *file_uid, uint32_t *new_size,
                      uint32_t *domain_ctx, status_$t *status_ret);

/*
 * ============================================================================
 * File Space Management Functions
 * ============================================================================
 */

/*
 * FILE_$RESERVE - Reserve disk space for a file
 *
 * Pre-allocates disk space for a file to ensure contiguous space
 * and prevent fragmentation.
 *
 * Parameters:
 *   file_uid   - UID of file to reserve space for
 *   start_byte - Pointer to starting byte offset
 *   byte_count - Pointer to number of bytes to reserve
 *   status_ret - Output status code
 *
 * Original address: 0x00E74310
 */
void FILE_$RESERVE(uid_t *file_uid, uint32_t *start_byte,
                   uint32_t *byte_count, status_$t *status_ret);

/*
 * FILE_$INVALIDATE - Invalidate cached pages of a file
 *
 * Forces cached pages for a file to be discarded, ensuring that
 * subsequent reads will fetch fresh data from disk.
 *
 * Parameters:
 *   file_uid    - UID of file to invalidate
 *   start_page  - Pointer to starting page number
 *   page_count  - Pointer to number of pages to invalidate
 *   flags       - Pointer to flags byte
 *   status_ret  - Output status code
 *
 * Original address: 0x00E75158
 */
void FILE_$INVALIDATE(uid_t *file_uid, uint32_t *start_page,
                      uint32_t *page_count, uint8_t *flags,
                      status_$t *status_ret);

/*
 * FILE_$GET_SEG_MAP - Get segment map for a file
 *
 * Retrieves a bitmap showing which segments of a file are currently
 * allocated or mapped.
 *
 * Parameters:
 *   file_uid    - UID of file to query
 *   start_off   - Pointer to starting offset for segment query
 *   flags_in    - Pointer to flags (negative = set flag bit 0)
 *   bitmap_out  - Output bitmap (32 bits)
 *   status_ret  - Output status code
 *
 * Original address: 0x00E74044
 */
void FILE_$GET_SEG_MAP(uid_t *file_uid, uint32_t *start_off,
                       int8_t *flags_in, uint32_t *bitmap_out,
                       status_$t *status_ret);

/*
 * ============================================================================
 * Lock Export Functions
 * ============================================================================
 */

/*
 * FILE_$EXPORT_LK - Export a lock to another process
 *
 * Exports a file lock held by the current process to another process,
 * allowing the target process to share the lock.
 *
 * Parameters:
 *   file_uid    - UID of file that must match the locked file
 *   lock_index  - Pointer to lock index to export
 *   target_proc - UID of target process to export lock to
 *   index_out   - Output: lock index assigned in target's table
 *   status_ret  - Output status code
 *
 * Original address: 0x00E74110
 */
void FILE_$EXPORT_LK(uid_t *file_uid, uint32_t *lock_index,
                     uid_t *target_proc, int32_t *index_out,
                     status_$t *status_ret);

/*
 * FILE_$UNLOCK_ALL - Unlock all locks for current process
 *
 * Releases all file locks held by the current process.
 * Typically called during process cleanup.
 *
 * Original address: 0x00E75138
 */
void FILE_$UNLOCK_ALL(void);

/*
 * FILE_$FORK_LOCK - Duplicate parent's file lock table to child during fork
 *
 * Copies all lock table entries from the current (parent) process to
 * the child process, incrementing each lock entry's reference count.
 * Called from PROC2_$FORK for non-vfork, non-init processes.
 *
 * Parameters:
 *   new_asid   - Pointer to child process's ASID
 *   status_ret - Output status code (always set to status_$ok)
 *
 * Original address: 0x00E74244
 */
void FILE_$FORK_LOCK(uint16_t *new_asid, status_$t *status_ret);

/*
 * ============================================================================
 * Pathname Case Mapping Functions
 * ============================================================================
 */

/*
 * MAP_CASE - Convert Unix-style pathname to Domain/OS case-mapped representation
 *
 * Converts a Unix pathname to Domain/OS case convention:
 *   - lowercase -> uppercase
 *   - uppercase -> ':' + char (escaped)
 *   - special chars (space, backslash, colon, control, high-bit) -> escaped forms
 *   - '/' passes through and resets component tracking
 *
 * Parameters:
 *   name        - Input pathname buffer
 *   name_len    - Pointer to input length
 *   output      - Output buffer for case-mapped result
 *   max_out_len - Pointer to maximum output buffer size
 *   out_len     - Pointer to output length (set on return)
 *   truncated   - Pointer to truncation flag (0xFF=truncated, 0x00=complete)
 *
 * Original address: 0x00e53ef8
 */
void MAP_CASE(char *name, int16_t *name_len, char *output,
              int16_t *max_out_len, int16_t *out_len, uint8_t *truncated);

/*
 * UNMAP_CASE - Convert Domain/OS case-mapped pathname to Unix-style
 *
 * Reverses the case mapping performed by MAP_CASE:
 *   - bare uppercase -> lowercase
 *   - ':' + uppercase -> keep uppercase
 *   - '\' -> '../'
 *   - ':' + digit -> special char
 *   - ':_' -> space, ':|' -> backslash, ':#XX' -> hex decode
 *
 * Parameters:
 *   name        - Input pathname buffer (Domain/OS case-mapped)
 *   name_len    - Pointer to input length
 *   output      - Output buffer for Unix-style result
 *   max_out_len - Pointer to maximum output buffer size
 *   out_len     - Pointer to output length (set on return)
 *   truncated   - Pointer to truncation flag (0xFF=truncated, 0x00=complete)
 *
 * Original address: 0x00e540d4
 */
void UNMAP_CASE(char *name, int16_t *name_len, char *output,
                int16_t *max_out_len, int16_t *out_len, uint8_t *truncated);

/*
 * FILE_$PRIV_UNLOCK_ALL - Unlock all locks held by a process
 *
 * Parameters:
 *   asid_ptr - pointer to the ASID whose locks are released; OS_$SHUTDOWN
 *              passes the address of a constant word 0 (0xE6D628), meaning
 *              all processes.
 *
 * Original address: 0x00E60BD0 (file/priv_unlock_all.c)
 */
void FILE_$PRIV_UNLOCK_ALL(uint16_t *asid_ptr);

/*
 * FILE_$READ_LOCK_ENTRYUI - Read lock entry by UID (unchecked)
 *
 * Reads lock entry info for a file without access checks.  Public because
 * name_$old_add_link (0x00E5687E) calls it when a remote hard-link add comes
 * back with file_$comm_failure.
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
 * ============================================================================
 * Lock internals reachable from outside file/
 *
 * These declarations moved here from file/file_internal.h (bead source-3uo):
 * audit/, dir/, name/, pacct/, rem_file/ and svc/ all call into the private
 * lock path, so the borrowing headers must not reach into a foreign internal
 * header for them.  Nothing about the routines or the records changed.
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
 * Main internal function for all unlock operations.  The callee frame at
 * 0x00E5FD32 is `link.w A6,-0xfc` with ten arguments occupying 32 bytes:
 *
 *   file_uid   A6+0x08  long  UID of the object to unlock
 *   lock_slot  A6+0x0C  long  per-process lock slot, 0 = search the row.
 *                             Pushed whole (`ext.l D0; move.l D0,-(SP)` at
 *                             0x00E60F06) but only its low word A6+0x0E is
 *                             read (0x00E5FDA2).
 *   lock_mode  A6+0x10  word  lock mode to match, 0 = every mode (and then
 *                             repeat until nothing is left)
 *   asid       A6+0x12  word  owning process' ASID
 *   by_key     A6+0x14  word  Pascal boolean: search the hash chain by
 *                             (mode, key, rem_key, rem_node) instead of the
 *                             process' lock row.  Pushed with `st -(SP)`
 *                             (0x00E607B2), which stores the byte at the
 *                             even half of the word slot.
 *   key        A6+0x16  word  lock key to match, 0 = any (0x00E5FF1A)
 *   rem_key    A6+0x18  long  matched against entry->context  (0x00E5FF38)
 *   rem_node   A6+0x1C  long  matched against entry->node_low (0x00E5FF2E)
 *   dtv_out    A6+0x20  long  out: data-time-valid, 0 when not produced
 *   status_ret A6+0x24  long  out: status code
 *
 * Returns:
 *   The REM_FILE_$UNLOCK / AST_$TRUNCATE result byte, or 0 when the reported
 *   status is non-zero (`seq D0b; and.b (-0xd8,A6),D0b` at 0x00E6039C).
 *
 * Original address: 0x00E5FD32
 */
boolean FILE_$PRIV_UNLOCK(uid_t *file_uid, int32_t lock_slot,
                          uint16_t lock_mode, uint16_t asid,
                          boolean by_key, uint16_t key,
                          uint32_t rem_key, uint32_t rem_node,
                          uint32_t *dtv_out, status_$t *status_ret);

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
typedef struct __attribute__((packed)) {
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

/* Remaining documented offsets (bead source-pewa). */
_Static_assert(__builtin_offsetof(file_lock_info_internal_t, file_uid) == 0x00, "file_lock_info_internal_t.file_uid");

/*
 * The m68k ABI aligns longs to two bytes, so holder_node really does sit at
 * +0x16 in the image (FILE_$FORCE_UNLOCK reads it as `cmp.l (-0x12,A6),D0` at
 * 0x00E60DEA with the record based at A6-0x28).  `packed` reproduces that on
 * hosts whose natural alignment would push it to +0x18.
 */
_Static_assert(offsetof(file_lock_info_internal_t, context)     == 0x08, "lock_info.context");
_Static_assert(offsetof(file_lock_info_internal_t, owner_node)  == 0x0C, "lock_info.owner_node");
_Static_assert(offsetof(file_lock_info_internal_t, side)        == 0x10, "lock_info.side");
_Static_assert(offsetof(file_lock_info_internal_t, mode)        == 0x12, "lock_info.mode");
_Static_assert(offsetof(file_lock_info_internal_t, sequence)    == 0x14, "lock_info.sequence");
_Static_assert(offsetof(file_lock_info_internal_t, holder_node) == 0x16, "lock_info.holder_node");
_Static_assert(offsetof(file_lock_info_internal_t, holder_port) == 0x1A, "lock_info.holder_port");
_Static_assert(offsetof(file_lock_info_internal_t, remote_info) == 0x1E, "lock_info.remote_info");
_Static_assert(sizeof(file_lock_info_internal_t)                == 0x22, "sizeof lock_info");

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
/*
 * This record is the head of file_lock_info_internal_t: FILE_$VERIFY_LOCK_HOLDER
 * passes its lock_info straight to FILE_$LOCAL_LOCK_VERIFY.  The two words the
 * verifier reads sit at +0x10 and +0x12 (00e608a0 `cmp.w (0x10,A2),D1w` and
 * 00e608b2 `cmp.w (0x12,A2),D0w`), so 0x08..0x0F must be spelled out.
 */
typedef struct {
    uid_t file_uid;      /* 0x00: File UID (8 bytes) */
    uint32_t context;    /* 0x08: file_lock_info_internal_t.context (unused here) */
    uint32_t owner_node; /* 0x0C: file_lock_info_internal_t.owner_node (unused here) */
    uint16_t side;       /* 0x10: Lock side (0=reader, 1=writer) from flags2 bit 7 */
    /* 0x12: the lock MODE being asked about, not a process ASID (source-9dc2).
     * FILE_$LOCAL_LOCK_VERIFY compares it twice, both times against a mode:
     * directly against the entry's own (flags2 & 0x78) >> 3 at 0x00E608AA-
     * 0x00E608B2, and against FILE_$LOCK_MODE_MAP[entry_mode] at
     * 0x00E608C0-0x00E608C8. */
    uint16_t mode;       /* 0x12: Lock mode to check */
} lock_verify_request_t;

/* Layout recovered from the disassembly -- see the field comments above. */
_Static_assert(__builtin_offsetof(lock_verify_request_t, file_uid) == 0x00, "lock_verify_request_t.file_uid");
_Static_assert(__builtin_offsetof(lock_verify_request_t, context) == 0x08, "lock_verify_request_t.context");
_Static_assert(__builtin_offsetof(lock_verify_request_t, owner_node) == 0x0C, "lock_verify_request_t.owner_node");
_Static_assert(__builtin_offsetof(lock_verify_request_t, side) == 0x10, "lock_verify_request_t.side");
_Static_assert(__builtin_offsetof(lock_verify_request_t, mode) == 0x12, "lock_verify_request_t.mode");
_Static_assert(sizeof(lock_verify_request_t) == 0x14, "lock_verify_request_t size");

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
 *   subsys_flag - Subsystem data flag, a Domain BOOLEAN byte (negative =
 *                 true = allow the locksmith override).  The routine reads it
 *                 with `move.b (0x14,A6),D3b` (0x00E5DD1E) - the high, even
 *                 half of the word slot, which is where a byte push lands -
 *                 and both call sites push a byte: `clr.w -(SP)` from
 *                 FILE_$SET_PROT (0x00E5E030, both halves zero) and
 *                 `move.b (-0x42c,A2),-(SP)` from the REM_FILE_ server
 *                 (0x00E634BA).  (source-w7lk)
 *   status_ret  - Output status code
 *
 * Original address: 0x00E5DD08
 */
void FILE_$SET_PROT_INT(uid_t *file_uid, void *acl_data, uint16_t attr_type,
                        uint16_t prot_type, boolean subsys_flag,
                        status_$t *status_ret);

#endif /* FILE_H */
