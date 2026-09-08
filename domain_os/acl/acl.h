/*
 * ACL - Access Control List Management
 *
 * This module provides access control list operations including:
 * - Permission checking (rights verification)
 * - Subsystem privilege management (UP/DOWN, ENTER/EXIT_SUPER)
 * - Process SID (Security ID) management
 * - Superuser status checking
 * - Locksmith mode support
 *
 * The ACL subsystem maintains per-process security context including:
 * - User, Group, Organization, and Login SIDs
 * - Subsystem nesting level
 * - Superuser mode counter
 */

#ifndef ACL_H
#define ACL_H

#include "base/base.h"

/*
 * Status codes, module 0x23 ("OS / ACL manager" in the SR10.4 status-code
 * database).  These are the single definitions; dir/, file/, name/ and pacct/
 * all raise them and include this header rather than redefining them
 * (bead source-3uo).
 */
#define status_$no_right_to_perform_operation            0x00230001
#define status_$insufficient_rights_to_perform_operation 0x00230002
#define status_$acl_exit_super_unbalanced                0x00230003  /* "exit_super called more often than enter_super" */
#define status_$acl_wrong_type                           0x00230004  /* "wrong type - operation illegal on system objects" */
#define status_$acl_no_right_to_set_subsystem_data       0x00230010  /* "no right to set subsystem data or subsystem manager" */
#define status_$acl_unimplemented_call                   0x0023001C  /* "attempt to issue unimplemented ACL call" */

/*
 * ============================================================================
 * Initialization
 * ============================================================================
 */

/*
 * ACL_$INIT - Initialize the ACL subsystem
 *
 * Must be called once during system startup before any other ACL functions.
 *
 * Original address: 0x00E3109C
 */
void ACL_$INIT(void);

/*
 * ============================================================================
 * Superuser Status
 * ============================================================================
 */

/*
 * ACL_$IS_SUSER - Check if current process has superuser privileges
 *
 * A process has superuser status if any of:
 *   - It is PID 1 (init process)
 *   - It is in super mode (ACL_$ENTER_SUPER called)
 *   - Its login UID matches the login UID
 *   - Any of its SIDs match the locksmith UID
 *
 * Returns:
 *   Non-zero (-1) if superuser, 0 otherwise
 *
 * Original address: 0x00E47E8C
 */
int8_t ACL_$IS_SUSER(void);

/*
 * ACL_$GET_LOCAL_LOCKSMITH - Check if local locksmith mode is enabled
 *
 * Returns the locksmith status for the local node.
 *
 * Returns:
 *   0 if locksmith mode is enabled
 *   Non-zero otherwise
 *
 * Original address: 0x00E4923C
 */
int16_t ACL_$GET_LOCAL_LOCKSMITH(void);

/*
 * ============================================================================
 * Subsystem Privilege Management
 * ============================================================================
 */

/*
 * ACL_$UP - Increment subsystem level
 *
 * Increments the subsystem nesting level for the current process.
 * While in a subsystem context, additional privileges may be granted.
 *
 * Original address: 0x00E4703E
 */
void ACL_$UP(void);

/*
 * ACL_$DOWN - Decrement subsystem level
 *
 * Decrements the subsystem nesting level for the current process.
 * Will not decrement below zero.
 *
 * Original address: 0x00E47068
 */
void ACL_$DOWN(void);

/*
 * ACL_$IN_SUBSYS - Check if in subsystem context
 *
 * Returns a Domain BOOLEAN BYTE: "sgt D0b" at 0x00E470BA writes only D0's low
 * byte, so the rest of the register still holds PROC1_$CURRENT * 2 from
 * 0x00E470B0 and is not part of the answer.  Both callers read it as a byte -
 * REM_FILE_$RN_DO_OP "tst.b D0b / bpl" at 0x00E615AC and REM_FILE_$LOCK the
 * same at 0x00E61B28.  (source-lpk8)
 *
 * Returns:
 *   true (0xFF) if the subsystem level is greater than zero, false otherwise
 *
 * Original address: 0x00E47098
 */
boolean ACL_$IN_SUBSYS(void);

/*
 * ACL_$ENTER_SUPER - Enter superuser mode
 *
 * Increments the superuser mode counter. While in super mode,
 * the process bypasses certain permission checks.
 * Must be balanced with ACL_$EXIT_SUPER.
 *
 * Original address: 0x00E46F90
 */
void ACL_$ENTER_SUPER(void);

/*
 * ACL_$EXIT_SUPER - Exit superuser mode
 *
 * Decrements the superuser mode counter.
 * Crashes if called without matching ENTER_SUPER.
 *
 * Original address: 0x00E46FB4
 */
void ACL_$EXIT_SUPER(void);

/*
 * ACL_$CLEAR_SUPER - Clear superuser mode
 *
 * Clears the superuser mode counter to zero and releases
 * any held locksmith override.
 *
 * Original address: 0x00E46FF8
 */
void ACL_$CLEAR_SUPER(void);

/*
 * ACL_$ENTER_SUBS - Enter subsystem context
 *
 * Enters a subsystem context with the specified UID's privileges.
 *
 * Parameters:
 *   uid        - UID of subsystem to enter
 *   status_ret - Output status code
 *
 * Returns:
 *   Non-zero if successful, 0 otherwise
 *
 * Original address: 0x00E46DA0
 */
int8_t ACL_$ENTER_SUBS(uid_t *uid, status_$t *status_ret);

/*
 * ============================================================================
 * Rights Checking
 * ============================================================================
 */

/*
 * ACL_$RIGHTS - Check access rights for an object
 *
 * Verifies the caller has the specified access rights to an object.
 *
 * The body (0x00E46A00) copies the UID into its own frame and hands nine
 * arguments to acl_$eval_rights (0x00E464B8); every one of the four caller
 * arguments below is dereferenced there, so none of them may be NULL:
 *
 *   uid           A6+0x08  UID of the object.  Copied to a local
 *                          (`move.l (A0)+,(-0x8,A6)` at 0x00E46A12).
 *   ignore_super  A6+0x0C  Pointer to a Domain boolean.  0x00E46A50
 *                          `movea.l (0xc,A6),A3` / 0x00E46A54
 *                          `move.b (A3),-(SP)`: the BYTE at that address is
 *                          passed by value.  TRUE (0xFF) suppresses the
 *                          super-user bypass that acl_$eval_rights would
 *                          otherwise take at 0x00E464DC
 *                          (`tst.b D4b` / `bpl` on in_super, then
 *                          `tst.b D5b` / `bpl` on this flag).
 *                          Callers pass the address of a byte-sized literal
 *                          pooled in their own code region.
 *   required_mask A6+0x10  Pointer to the required rights mask, read as a
 *                          LONGWORD (`move.l (A2),-(SP)` at 0x00E46A4E).
 *   option_flags  A6+0x14  Pointer to the object-type / option word, read as
 *                          a WORD (`move.w (A1),-(SP)` at 0x00E46A48).
 *   status        A6+0x18  Output status code.
 *
 * Returns:
 *   The granted rights, as the full longword acl_$eval_rights leaves in D0
 *   (callers test it with `tst.l D0` at 0x00E73D4C and `cmpi.l #0x2,D0` at
 *   0x00E71504).  Zero means access denied.
 *
 * Original address: 0x00E46A00
 */
uint32_t ACL_$RIGHTS(uid_t *uid, boolean *ignore_super, uint32_t *required_mask,
                     int16_t *option_flags, status_$t *status);

/*
 * ACL_$RIGHTS_CHECK - Check access rights for an object (variant)
 *
 * Parameters:
 *   acl_ctx       - pointer to the ACL context record (0x00E46AFA)
 *   file_uid      - UID of object to check
 *   required_mask - Pointer to required access rights mask (or NULL)
 *   option_flags  - Pointer to option flags (or NULL)
 *   check_flag    - Pointer to check flag
 *   status        - Output status code
 *
 * Returns:
 *   Non-zero if access granted, 0 if denied
 *
 * Original address: 0x00E46AEC
 */
/*
 * The first argument is a POINTER to an ACL context record, not a UID by
 * value: 0x00E46AFA does `movea.l (0x8,A6),A2` and then passes `A2` and
 * `A2+0x24` on to 0x00E464B8.
 */
int16_t ACL_$RIGHTS_CHECK(void *acl_ctx, uid_t *file_uid,
                          void *required_mask, void *option_flags,
                          int8_t *check_flag, status_$t *status);

/*
 * ACL_$CHECK_RIGHTS - Check rights with full options
 *
 * Original address: 0x00E46A8E
 */
int16_t ACL_$CHECK_RIGHTS(uid_t *uid, void *acl_data, void *options,
                          status_$t *status);

/*
 * ACL_$MIN_RIGHTS - Get minimum rights for an object
 *
 * Returns the minimum access rights that would be granted to any user
 * for the specified object (intersection of all ACL entries).
 *
 * Parameters:
 *   uid - UID of object to check
 *
 * Returns:
 *   Bitmask of rights (lower 4 bits valid)
 *
 * Original address: 0x00E468E2
 */
uint32_t ACL_$MIN_RIGHTS(uid_t *uid);

/*
 * acl_$prot_data_t - the 44-byte protection block that
 * AST_$GET_ACL_ATTRIBUTES leaves in ast_$acl_attr_t.acl_data (attribute
 * record +0x0C..+0x37).
 *
 * Offsets recovered from acl_$eval_rights, whose copy of the record sits at
 * A6-0x74 (= attrs +0x0C):
 *   0x00 owner          `lea (-0x74,A6),A1` / cmpm against sids[0] (0x00E46706)
 *   0x08 group          `lea (-0x6c,A6),A4`                        (0x00E4672A)
 *   0x10 org            `lea (-0x64,A6),A2`                        (0x00E4679E)
 *   0x18 owner_rights   `btst.b #0x4,(-0x5c,A6)`                   (0x00E466FA)
 *   0x19 group_rights   `btst.b #0x4,(-0x5b,A6)`                   (0x00E4671E)
 *   0x1A org_rights     `btst.b #0x4,(-0x5a,A6)`                   (0x00E46792)
 *   0x1B world_rights   `move.b (-0x59,A6),D2b`                    (0x00E467BC)
 *   0x1C subsys_rights  `move.b (0x1c,A2),D0b` in acl_$eval_acl_entries
 *                                                                  (0x00E46224)
 * acl_$find_acl_slot writes the last two from the cached default ACL data
 * (`move.b (0x80d,A2),(0x1b,A1)` / `(0x80f,A2),(0x1c,A1)`, 0x00E45F24).
 */
typedef struct acl_$prot_data_t {
    uid_t   owner;              /* 0x00 */
    uid_t   group;             /* 0x08 */
    uid_t   org;               /* 0x10 */
    uint8_t owner_rights;      /* 0x18 */
    uint8_t group_rights;      /* 0x19 */
    uint8_t org_rights;        /* 0x1A */
    uint8_t world_rights;      /* 0x1B */
    uint8_t subsys_rights;     /* 0x1C */
    uint8_t reserved_1d[15];   /* 0x1D..0x2B */
} acl_$prot_data_t;

#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(acl_$prot_data_t, group)        == 0x08, "prot.group");
_Static_assert(__builtin_offsetof(acl_$prot_data_t, org)          == 0x10, "prot.org");
_Static_assert(__builtin_offsetof(acl_$prot_data_t, owner_rights) == 0x18, "prot.owner_rights");
_Static_assert(__builtin_offsetof(acl_$prot_data_t, world_rights) == 0x1B, "prot.world_rights");
_Static_assert(__builtin_offsetof(acl_$prot_data_t, subsys_rights)== 0x1C, "prot.subsys_rights");
_Static_assert(sizeof(acl_$prot_data_t) == 44, "sizeof acl_$prot_data_t");
#endif

/*
 * ACL_$SET_ACL_CHECK - may this caller replace an object's ACL?
 *
 * Parameters (source-u4vy; emitted in acl/set_acl_check.c):
 *   obj_uid    - A6+0x08  the object whose ACL is being replaced; copied into
 *                the routine's own frame at 0x00E470DA
 *   new_prot   - A6+0x0C  the 44-byte protection record being installed.  It
 *                is the same acl_$prot_data_t REM_FILE_$SET_ACL calls its
 *                `acl_header` (11 longwords copied at 0x00E62B12), and the
 *                three per-SID rights bytes at +0x18/+0x19/+0x1A are tested
 *                with `btst.b #0x5` at 0x00E47720-0x00E47736.
 *   acl_uid    - A6+0x10  the ACL object being installed
 *   op_type    - A6+0x14  POINTER to an operation-type word.  5 skips the SID
 *                checks (0x00E4770E); 3 takes the SIDs from the object's own
 *                protection record rather than from new_prot (0x00E4776A,
 *                0x00E477AA, 0x00E47830).
 *   setid_ret  - A6+0x18  out: Domain boolean, cleared at 0x00E470E8 and set
 *                with `st` at 0x00E474E4 / 0x00E47708 / 0x00E4775C
 *   status_ret - A6+0x1C  out: status code
 *
 * Returns the Domain boolean in D4 (`move.b D4b,D0b` at 0x00E4786C): TRUE
 * when the change is permitted.
 *
 * Original address: 0x00E470C4
 */
boolean ACL_$SET_ACL_CHECK(uid_t *obj_uid, acl_$prot_data_t *new_prot,
                           uid_t *acl_uid, int16_t *op_type,
                           boolean *setid_ret, status_$t *status_ret);

/*
 * ACL_$CHECK_FAULT_RIGHTS - Check fault handling rights between processes
 *
 * Determines if process pid1 can handle faults for process pid2.
 *
 * Parameters:
 *   pid1 - Pointer to the fault handler's PROC1 pid word (read-only,
 *          0x00E48A42 `move.w (A2),-(SP)`)
 *   pid2 - Pointer to the faulting process' PROC1 pid word (read-only,
 *          0x00E48A72 `move.w (A3),D1w`)
 *
 * Returns:
 *   Non-zero if fault handling allowed, 0 otherwise
 *
 * Original address: 0x00E48A28
 */
int8_t ACL_$CHECK_FAULT_RIGHTS(const uint16_t *pid1, const uint16_t *pid2);

/*
 * ACL_$CHECK_DEBUG_RIGHTS - Check debug rights between processes
 *
 * Similar to CHECK_FAULT_RIGHTS but for debugging privileges.
 *
 * Parameters:
 *   pid1 - Pointer to debugger process ID
 *   pid2 - Pointer to debuggee process ID
 *
 * Returns:
 *   Non-zero if debug access allowed, 0 otherwise
 *
 * Original address: 0x00E48ADA
 */
int8_t ACL_$CHECK_DEBUG_RIGHTS(int16_t *pid1, int16_t *pid2);

/*
 * ACL_$USED_SUSER - Check if current process has used superuser privilege
 *
 * Returns non-zero if the current process has used superuser privilege
 * at any point (for auditing purposes).
 *
 * Returns:
 *   Non-zero if superuser was used, 0 otherwise
 *
 * Original address: 0x00E492CC
 */
int8_t ACL_$USED_SUSER(void);

/*
 * ============================================================================
 * SID Management
 * ============================================================================
 */

/*
 * ACL_$GET_EXSID - Get extended SID for current process
 *
 * Parameters:
 *   exsid  - Output buffer for extended SID
 *   status - Output status code
 *
 * Original address: 0x00E48972
 */
void ACL_$GET_EXSID(void *exsid, status_$t *status);

/*
 * ACL_$GET_PID_SID - Get SID for a specific process
 *
 * Parameters:
 *   pid        - Process ID
 *   sid_ret    - Output SID buffer
 *   status_ret - Output status code
 *
 * Original address: 0x00E489E2
 */
void ACL_$GET_PID_SID(int16_t pid, uid_t *sid_ret, status_$t *status_ret);

/*
 * ACL_$GET_RE_ALL_SIDS - Get all SIDs for current requester
 *
 * Write sizes verified at 0x00E487C2 - 0x00E4880A: nine longwords into each
 * of the first two buffers and three longwords into each of the next two.
 * It is a procedure - no result slot is pushed at any call site.
 *
 * Parameters:
 *   acl_data   - Output buffer, 36 bytes (0x00E487C2)
 *   re_sids    - Output buffer, 36 bytes (0x00E487D2)
 *   prot_info  - Output buffer, 12 bytes (0x00E487F8)
 *   subsys_ids - Output buffer, 12 bytes (0x00E48806)
 *   status     - Output status code (cleared at 0x00E48810)
 *
 * Original address: 0x00E48792
 */
void ACL_$GET_RE_ALL_SIDS(void *acl_data, void *re_sids,
                          void *prot_info, void *subsys_ids,
                          status_$t *status);

/*
 * ============================================================================
 * ASID Management
 * ============================================================================
 */

/*
 * ACL_$ALLOC_ASID - Allocates an address space ID.
 *
 * Doesn't _actually_ acllocate it.  It marks the passed address space as being used,
 * copying settings from the current process's ASID
 *
 * Parameters:
 *   asid       - ASID to mark as allocated
 *   status_ret - Output status code
 *
 * Original address: 0x00E73BB8
 */
void ACL_$ALLOC_ASID(int16_t asid_ret, status_$t *status_ret);

/*
 * ACL_$FREE_ASID - Free an address space ID
 *
 * Resets the ACL state for an ASID to system defaults.
 *
 * Parameters:
 *   asid       - ASID to free
 *   status_ret - Output status code
 *
 * Original address: 0x00E74C6A
 */
void ACL_$FREE_ASID(int16_t asid, status_$t *status_ret);

/*
 * ACL_$GET_SID - Get SID for an ASID
 *
 * Parameters:
 *   asid    - Address space ID
 *   sid_ret - Output SID buffer
 *
 * Original address: 0x00E74C24
 */
void ACL_$GET_SID(int16_t asid, uid_t *sid_ret);

/*
 * ============================================================================
 * ACL Data Conversion
 * ============================================================================
 */

/*
 * ACL_$DEFAULT_ACL - Get default ACL UID for an object type
 *
 * Parameters:
 *   acl_ret  - Output ACL UID
 *   acl_type - Pointer to object type word: 0/4/5 -> ACL_$FNDWRX,
 *              1/2 -> ACL_$DNDCAL, 3 -> UID_$NIL, otherwise unchanged
 *
 * Original address: 0x00E4787E
 */
void ACL_$DEFAULT_ACL(uid_t *acl_ret, int16_t *acl_type);

/*
 * ACL_$DEF_ACLDATA - Get default ACL data
 *
 * Fills a 44-byte acl_$prot_data_t with the system default protection -
 * owner RGYC_$P_SYS_USER_UID (0xE174EC), group RGYC_$G_NIL_UID (0xE17524),
 * org PPO_$NIL_ORG_UID (0xE17574), rights 0x10/0x10/0x10 and world 0x0F
 * (0x00E478EE-0x00E4792E) - and sets *uid_out to UID_$NIL
 * (0x00E4793E-0x00E4794A).
 *
 * Parameters:
 *   acl_data_out - Output ACL data buffer (44 bytes, an acl_$prot_data_t)
 *   uid_out      - Output UID buffer (8 bytes, set to UID_$NIL)
 *
 * Original address: 0x00E478DC
 */
void ACL_$DEF_ACLDATA(void *acl_data_out, void *uid_out);

/*
 * ACL_$CONVERT_FUNKY_ACL - Convert "funky" ACL format
 *
 * Bits 4..11 of acl_uid.low's high word select the encoding; 0x80, 0x40 and
 * 0x20 (after `lsr.w #4` + `andi.w #0xe0`, 0x00E49040-0x00E49060) are the three
 * the routine knows, anything else falls through untouched.
 *
 * Parameters:
 *   acl_uid        - ACL UID in funky format (8 bytes)
 *   acl_data_out   - Output ACL data buffer (an acl_$prot_data_t)
 *   prot_info_out  - In/out: the normalised object UID (8 bytes).  0x00E490E4
 *                    can set bit 24 of its low half (`bset.b #0x0,(0x4,A3)`).
 *   target_uid_out - Output ACL type UID (8 bytes): ACL_$FILE_ACL (0xE17444)
 *                    or ACL_$DIR_ACL (0xE1744C), stored at 0x00E490D2 /
 *                    0x00E49126
 *   status_ret     - Output status code
 *
 * Original address: 0x00E4900C
 */
void ACL_$CONVERT_FUNKY_ACL(void *acl_uid, void *acl_data_out,
                             void *prot_info_out, void *target_uid_out,
                             status_$t *status_ret);

/*
 * ============================================================================
 * Project List Management
 * ============================================================================
 */

/*
 * ACL_$ADD_PROJ - Add a project to the current process's project list
 *
 * Adds the specified project UID to the project list. Requires superuser.
 *
 * Parameters:
 *   proj_acl   - Project UID to add
 *   status_ret - Output status code
 *
 * Original address: 0x00E47EAC
 */
void ACL_$ADD_PROJ(uid_t *proj_acl, status_$t *status_ret);

/*
 * ACL_$DELETE_PROJ - Delete a project from the current process's project list
 *
 * Removes the specified project UID from the project list. Requires superuser.
 *
 * Parameters:
 *   proj_acl   - Project UID to remove
 *   status_ret - Output status code
 *
 * Original address: 0x00E47F54
 */
void ACL_$DELETE_PROJ(uid_t *proj_acl, status_$t *status_ret);

/*
 * ACL_$GET_PROJ_LIST - Get the project list for the current process
 *
 * Parameters:
 *   proj_acls  - Output buffer for project UIDs
 *   max_count  - Pointer to max count (clamped to 8)
 *   count_ret  - Output: actual count
 *   status_ret - Output status code
 *
 * Original address: 0x00E48034
 */
void ACL_$GET_PROJ_LIST(uid_t *proj_acls, int16_t *max_count, int16_t *count_ret,
                        status_$t *status_ret);

/*
 * ACL_$SET_PROJ_LIST - Set the project list for the current process
 *
 * Parameters:
 *   proj_acls  - Array of project UIDs to set
 *   count      - Pointer to count (max 8)
 *   status_ret - Output status code
 *
 * Original address: 0x00E480F4
 */
void ACL_$SET_PROJ_LIST(uid_t *proj_acls, int16_t *count, status_$t *status_ret);

/*
 * ============================================================================
 * SID Management (Extended)
 * ============================================================================
 */

/*
 * ACL_$GET_RE_SIDS - Get requestor SIDs for the current process
 *
 * Parameters:
 *   original_sids - Output buffer for original SIDs (36 bytes)
 *   current_sids  - Output buffer for current SIDs (36 bytes)
 *   status_ret    - Output status code
 *
 * Original address: 0x00E488B6
 */
void ACL_$GET_RE_SIDS(void *original_sids, void *current_sids, status_$t *status_ret);

/*
 * ACL_$GET_RES_SIDS - Get resource SIDs for the current process
 *
 * Parameters:
 *   original_sids - Output buffer for original SIDs (36 bytes)
 *   current_sids  - Output buffer for current SIDs (36 bytes)
 *   saved_sids    - Output buffer for saved SIDs (36 bytes)
 *   status_ret    - Output status code
 *
 * Original address: 0x00E4890C
 */
void ACL_$GET_RES_SIDS(void *original_sids, void *current_sids, void *saved_sids,
                       status_$t *status_ret);

/*
 * ACL_$GET_RES_ALL_SIDS - Get all resource SIDs and project lists
 *
 * Parameters:
 *   original_sids  - Output buffer for original SIDs (36 bytes)
 *   current_sids   - Output buffer for current SIDs (36 bytes)
 *   saved_sids     - Output buffer for saved SIDs (36 bytes)
 *   saved_proj     - Output buffer for saved project metadata (12 bytes)
 *   current_proj   - Output buffer for current project metadata (12 bytes)
 *   status_ret     - Output status code
 *
 * Original address: 0x00E4881C
 */
void ACL_$GET_RES_ALL_SIDS(void *original_sids, void *current_sids, void *saved_sids,
                           void *saved_proj, void *current_proj, status_$t *status_ret);

/*
 * ACL_$SET_RE_ALL_SIDS - Set all requestor SIDs for the current process
 *
 * Parameters:
 *   new_original_sids - New original SIDs (36 bytes)
 *   new_current_sids  - New current SIDs (36 bytes)
 *   new_saved_proj    - New saved project metadata (12 bytes)
 *   new_current_proj  - New current project metadata (12 bytes)
 *   status_ret        - Output status code
 *
 * Original address: 0x00E481AE
 */
void ACL_$SET_RE_ALL_SIDS(void *new_original_sids, void *new_current_sids,
                          void *new_saved_proj, void *new_current_proj,
                          status_$t *status_ret);

/*
 * ACL_$SET_RES_ALL_SIDS - Set all resource SIDs for the current process
 *
 * Parameters:
 *   new_original_sids - New original SIDs (36 bytes)
 *   new_current_sids  - New current SIDs (36 bytes)
 *   new_saved_sids    - New saved SIDs (36 bytes)
 *   new_saved_proj    - New saved project metadata (12 bytes)
 *   new_current_proj  - New current project metadata (12 bytes)
 *   status_ret        - Output status code
 *
 * Original address: 0x00E4855A
 */
void ACL_$SET_RES_ALL_SIDS(void *new_original_sids, void *new_current_sids,
                           void *new_saved_sids, void *new_saved_proj,
                           void *new_current_proj, status_$t *status_ret);

/*
 * ============================================================================
 * ACL Creation and Conversion
 * ============================================================================
 */

/*
 * ACL_$IMAGE - Create an ACL image
 *
 * Parameters:
 *   source_uid    - Source UID
 *   buffer_len    - Pointer to buffer length
 *   unknown_flag  - Pointer to flag byte
 *   param_4       - Unknown parameter
 *   param_5       - Unknown parameter
 *   param_6       - Unknown parameter
 *   status_ret    - Output status code
 *
 * Original address: 0x00E47DF6
 */
void ACL_$IMAGE(void *source_uid, int16_t *buffer_len, int8_t *unknown_flag,
                void *param_4, void *param_5, void *param_6, status_$t *status_ret);

/*
 * ACL_$PRIM_CREATE - Create a primitive ACL object
 *
 * Parameters:
 *   acl_data      - ACL data buffer
 *   data_len      - Pointer to length
 *   dir_uid       - Directory UID
 *   type          - ACL type code
 *   file_uid_ret  - Output: created file UID
 *   status_ret    - Output status code
 *
 * Original address: 0x00E47968
 */
void ACL_$PRIM_CREATE(void *acl_data, int16_t *data_len, uid_t *dir_uid,
                      void *type, uid_t *file_uid_ret, status_$t *status_ret);

/*
 * ACL_$CONVERT_TO_9ACL - Convert ACL to 9-entry format
 *
 * Parameters:
 *   type          - ACL type / protection buffer, passed by reference
 *                   (callers push a pointer: pea; passed through unchanged
 *                   as the 4th argument of ACL_$PRIM_CREATE)
 *   source_uid    - Source UID
 *   dir_uid       - Directory UID
 *   default_prot  - Default protection
 *   result_uid    - Output: converted ACL UID
 *   status_ret    - Output status code
 *
 * Original address: 0x00E48CE8
 */
void ACL_$CONVERT_TO_9ACL(void *type, uid_t *source_uid, uid_t *dir_uid,
                          void *default_prot, uid_t *result_uid, status_$t *status_ret);

/*
 * ACL_$CONVERT_FROM_9ACL - render an ACL object as the old 9-entry image
 *
 * It raises the caller's privilege, brackets acl_$image_internal
 * (0x00E47B78) with both ACL locks, and marks the returned UID.
 *
 * Parameters:
 *   source_acl   - Source ACL UID
 *   acl_type     - NEVER READ by the routine (A6+0x0C)
 *   prot_buf_out - Output: acl_$image_internal's `data_out`
 *   prot_uid_out - Output: source_acl with bit 24 of .low set (0x00E48FFC)
 *   status_ret   - Output status code
 *
 * Original address: 0x00E48F56, 182 bytes (emitted in acl/convert_from_9acl.c)
 */
void ACL_$CONVERT_FROM_9ACL(uid_t *source_acl, uid_t *acl_type,
                             void *prot_buf_out, uid_t *prot_uid_out,
                             status_$t *status_ret);

/*
 * ACL_$COPY - Copy ACL from source to destination
 *
 * Parameters:
 *   source_acl_uid - Source ACL UID
 *   dest_uid       - Destination UID
 *   source_type    - Source ACL type
 *   dest_type      - Destination ACL type
 *   status_ret     - Output status code
 *
 * Original address: 0x00E4930A
 */
void ACL_$COPY(uid_t *source_acl_uid, uid_t *dest_uid, uid_t *source_type,
               uid_t *dest_type, status_$t *status_ret);

/*
 * ACL_$CONVERT_TO_10ACL - Convert ACL to 10-entry format
 *
 * Converts an ACL from older format to 10-entry format.
 *
 * Parameters:
 *   source_acl   - Source ACL UID
 *   file_uid     - File UID for conversion context
 *   result_uid   - Output: converted ACL UID
 *   acl_data     - Output: ACL data buffer (44 bytes)
 *   status_ret   - Output status code
 *
 * Returns:
 *   Non-zero if conversion was performed
 */
int8_t ACL_$CONVERT_TO_10ACL(void *source_acl, void *file_uid, uid_t *result_uid,
                              void *acl_data, status_$t *status_ret);

/*
 * ACL_$GET_ACL_ATTRIBUTES - Get ACL attributes for a file
 *
 * Retrieves ACL attributes (format info, flags) for the specified file.
 *
 * Parameters:
 *   file_uid   - UID of file to query
 *   flags      - Query flags
 *   attrs_out  - Output: attribute buffer (12 bytes)
 *   status_ret - Output status code
 */
void ACL_$GET_ACL_ATTRIBUTES(void *file_uid, int16_t flags, void *attrs_out,
                              status_$t *status_ret);

/*
 * ACL_$OVERRIDE_LOCAL_LOCKSMITH - Override local locksmith mode
 *
 * Temporarily enables or disables locksmith privilege override for the
 * current process. Used by remote file server to perform privileged
 * operations on behalf of remote clients.
 *
 * Parameters:
 *   enable     - Non-zero to enable override, 0 to disable
 *   status_ret - Output status code
 */
void ACL_$OVERRIDE_LOCAL_LOCKSMITH(int16_t enable, status_$t *status_ret);

/*
 * ============================================================================
 * Subsystem and Locksmith
 * ============================================================================
 */

/*
 * ACL_$INHERIT_SUBSYS - Inherit subsystem state from parent
 *
 * Parameters:
 *   inherit_flag - Pointer to inheritance flag byte
 *   status_ret   - Output status code
 *
 * Original address: 0x00E49138
 */
void ACL_$INHERIT_SUBSYS(uint8_t *inherit_flag, status_$t *status_ret);

/*
 * ACL_$SET_LOCAL_LOCKSMITH - Set local locksmith mode
 *
 * Parameters:
 *   locksmith_value - Pointer to new locksmith value
 *   status_ret      - Output status code
 *
 * Original address: 0x00E49196
 */
void ACL_$SET_LOCAL_LOCKSMITH(int16_t *locksmith_value, status_$t *status_ret);

/*
 * ============================================================================
 * Global Data
 * ============================================================================
 */

/* Default ACL UIDs for different object types */
extern uid_t ACL_$DNDCAL;   /* 0xE174DC: Default ACL for dirs/links */
extern uid_t ACL_$FNDWRX;   /* 0xE174C4: Default ACL for files */
extern uid_t ACL_$DIR_ACL;  /* Well-known ACL UID for directories */

/* ACL type UIDs - used to identify ACL operations */
extern uid_t ACL_$FILE_ACL;    /* 0xE1744C */
extern uid_t ACL_$FILEIN_ACL;  /* 0xE17454 */
extern uid_t ACL_$DIRIN_ACL;   /* 0xE1745C: {0x00000603, 0} (used by dir/) */

/* Nil ACL UID (0xE17384): {0x00000100, 0}.  Defined in acl/acl_data.c. */
extern uid_t ACL_$NIL;

/* Per-process super-user nesting counts, indexed by PROC1_$CURRENT (0xE7DACA) */
extern int16_t ACL_$SUPER_COUNT[];


/*
 * ACL_$SERVER - ACL half of the remote-file server
 *
 * REM_FILE_$SERVER delegates opcodes 0x64..0x77 to this routine
 * (0x00E639C4), with the same request/response/reply-length contract as
 * DIR_$SERVER.
 *
 * Original address: 0x00E49594
 */
void ACL_$SERVER(void *request, void *response, uint16_t *reply_len);

#endif /* ACL_H */
