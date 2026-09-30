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
#include "proc1/proc1_config.h"   /* PROC1_MAX_PROCESSES */

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
 * SVC TRAP slot 0x3C.  Applies the set-ID SIDs of `uid`'s protection to the
 * calling process (acl_$setids with set = the caller's "magic" matched) and
 * commits them to its current and saved SIDs; audited.
 *
 * Returns: TRUE (0xFF) when the SIDs were committed, FALSE otherwise.
 *
 * Original address: 0x00E46DA0; acl/enter_subs.c
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
    uint8_t reserved_1d[3];    /* 0x1D..0x1F */
    uint32_t owner_ext[3];     /* 0x20: owner / group / org extension longs,
                                * the aote_t owner1_ext..owner3_ext image;
                                * acl_$setids hands them out with the SIDs
                                * (0x00E46C68, 0x00E46CB2, 0x00E46D00) */
} acl_$prot_data_t;

#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(acl_$prot_data_t, group)        == 0x08, "prot.group");
_Static_assert(__builtin_offsetof(acl_$prot_data_t, org)          == 0x10, "prot.org");
_Static_assert(__builtin_offsetof(acl_$prot_data_t, owner_rights) == 0x18, "prot.owner_rights");
_Static_assert(__builtin_offsetof(acl_$prot_data_t, world_rights) == 0x1B, "prot.world_rights");
_Static_assert(__builtin_offsetof(acl_$prot_data_t, subsys_rights)== 0x1C, "prot.subsys_rights");
_Static_assert(__builtin_offsetof(acl_$prot_data_t, owner_ext)    == 0x20, "prot.owner_ext");
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

/*
 * acl_$cache_slot_t - one slot of the in-memory ACL image cache at 0xE88834.
 *
 * ACL_$INIT zeroes 0xE88834..0xE935CC (0x00E310AA-0x00E310C0) and builds a
 * 31-entry circular free list with `divs.w #0x1f` (0x00E31140-0x00E3117A), so
 * the cache holds 31 slots of 0x400 bytes: 0xE88834 + 31*0x400 = 0xE90434,
 * exactly where ACL_$DATA.original_sids[1] begins.
 *
 * acl_$eval_rights indexes it with `lsl.l #0x8` + `lsl.l #0x2` (= *0x400) at
 * 0x00E4680C and 0x00E46870.  ACL_$SET_ACL_CHECK uses the same stride.
 *
 * Packed: type_uid lands on an odd multiple of two, which the m68k ABI allows
 * but a 4-byte-aligning host would pad.
 */
#define ACL_CACHE_SLOTS         31
#define ACL_CACHE_SLOT_SIZE     0x400
#define ACL_CACHE_NO_SLOT       ((int16_t)-1)

typedef struct __attribute__((packed)) acl_$cache_slot_t {
    int16_t  version;           /* 0x00: image format version.  acl_$load_acl_image
                                 *       tests it with `cmpi.w #0x3,(A2)`
                                 *       (0x00E45BF6) and `cmpi.w #0x5,(A2)` +
                                 *       `bge` (0x00E45C52); acl_$convert_image
                                 *       stamps 5 into the image it builds
                                 *       (`move.w #0x5,(A2)`, 0x00E44E86).  The
                                 *       compare is signed, hence int16_t. */
    uid_t    type_uid;          /* 0x02: ACL_$FILE_ACL / ACL_$DIR_ACL / ...
                                 *       (0x00E4745C, 0x00E4763A) */
    uint32_t reserved_0a;       /* 0x0A */
    uint16_t entry_count;       /* 0x0E: `move.w (0xe,A1),D2w`, 0x00E461B6 */
    uint16_t reserved_10;       /* 0x10 */
    uid_t    required_uid;      /* 0x12: 0x00E474E6, 0x00E475C6 */
    uid_t    subsys_uid;        /* 0x1A: subsystem manager; compared against
                                 *       sids->login_sid at 0x00E46828 */
    uint32_t reserved_22;       /* 0x22: `clr.l (0x22,A1)` 0x00E45C1C */
    uint16_t reserved_26;       /* 0x26: `clr.w (0x26,A1)` 0x00E45C20 */
    int8_t   world_entry_present;
                                /* 0x28: Pascal boolean.  Its ONE reader is the
                                 *       version-3/4 directory fixup in
                                 *       acl_$load_acl_image: `tst.b (0x28,A2)`
                                 *       + `bmi` at 0x00E45CA0 skips appending
                                 *       the all-nil (person = group = org =
                                 *       UID_$NIL) entry with rights
                                 *       ACL_V4_RIGHTS_DEFAULT when the flag is
                                 *       negative.  That all-nil entry is what
                                 *       acl_$convert_image turns into
                                 *       prot->world_rights (0x00E44F44-
                                 *       0x00E44F56), so a true flag means "this
                                 *       image already carries its world entry".
                                 *       The version-3 promotion clears it unless
                                 *       the image is a directory ACL
                                 *       (0x00E45C38); every version-5 image the
                                 *       kernel builds clears it outright
                                 *       (acl_$convert_image 0x00E44EC6,
                                 *       acl_$image_internal 0x00E47DD8). */
    int8_t   unused_29;         /* 0x29: no reader anywhere in the image; the
                                 *       three writers only ever clear it
                                 *       alongside world_entry_present
                                 *       (0x00E45C3C, 0x00E44ECA, 0x00E47DDC). */
    uint8_t  reserved_2a[0x0A]; /* 0x2A..0x33: five words the version-3 fixup
                                 *       zeroes (0x00E45C40-0x00E45C4E) */
    uint8_t  entries[0x3CC];    /* 0x34: 0x20-byte ACL entries
                                 *       (`lea (0x34,A0),A0` + `lea (0x20,A0),A0`
                                 *        at 0x00E461AC / 0x00E46232) */
} acl_$cache_slot_t;

/* Packed, so the layout holds on every build: unconditional. */
_Static_assert(__builtin_offsetof(acl_$cache_slot_t, type_uid)    == 0x02, "cache.type_uid");
_Static_assert(__builtin_offsetof(acl_$cache_slot_t, world_entry_present) == 0x28,
               "cache.world_entry_present");
_Static_assert(__builtin_offsetof(acl_$cache_slot_t, unused_29)  == 0x29, "cache.unused_29");
_Static_assert(__builtin_offsetof(acl_$cache_slot_t, entry_count) == 0x0E, "cache.entry_count");
_Static_assert(__builtin_offsetof(acl_$cache_slot_t, required_uid)== 0x12, "cache.required_uid");
_Static_assert(__builtin_offsetof(acl_$cache_slot_t, subsys_uid)  == 0x1A, "cache.subsys_uid");
_Static_assert(__builtin_offsetof(acl_$cache_slot_t, entries)     == 0x34, "cache.entries");
_Static_assert(sizeof(acl_$cache_slot_t) == ACL_CACHE_SLOT_SIZE, "sizeof acl_$cache_slot_t");

/*
 * ----------------------------------------------------------------------------
 * The ACL image cache directory (ACL module A5 data, A5 = 0xE7CF54)
 * ----------------------------------------------------------------------------
 *
 * ACL_$DATA.acl_cache holds the 31 raw 0x400-byte ACL images.  The
 * bookkeeping that decides which image lives in which slot is a set of
 * parallel A5-relative arrays in ACL_$UNWIRED_DATA that acl_$find_acl_slot
 * (0x00E45E8E) walks:
 *
 *   A5+0x800  cache_dir[31]            16 bytes each  (0xE7D754)
 *   A5+0x9F0  cache_lru_links[32]       4 bytes each  (0xE7D944)
 *   A5+0xA70  cache_hash_links[32]      4 bytes each  (0xE7D9C4)
 *   A5+0xAF0  cache_hash_buckets[61]    2 bytes each  (0xE7DA44)
 *   A5+0xB74  cache_free_head                         (0xE7DAC8)
 *   A5+0xB76  cache_lru_head                          (0xE7DACA)
 *
 * The first three are contiguous, which fixes their element counts:
 * 0x800 + 31*0x10 = 0x9F0, 0x9F0 + 32*4 = 0xA70, 0xA70 + 32*4 = 0xAF0.  The
 * bucket array is the 61 words the hash modulus can produce: the image
 * initialises exactly 0xAF0..0xB69 to -1, and ACL_$ENTER_SUBS keeps its
 * magic longword at A5+0xB6C (`move.l D0,(0xb6c,A5)' 0x00E46E08), so a
 * 64-word array (the earlier reading) would overlap it.
 *
 * cache_lru_head is also super_count[0] (see ACL_$UNWIRED_DATA).
 */

/* UID_$HASH modulus for the ACL cache: the word at 0x00E45E8C, reached by the
 * `pea (-0x20,PC)` at 0x00E45EAA.  Raw bytes 00 3D. */
#define ACL_CACHE_HASH_MOD          61
/* One bucket per value the modulus can produce (A5+0xAF0..0xB69). */
#define ACL_CACHE_HASH_BUCKETS      ACL_CACHE_HASH_MOD
/* Both link arrays are 32 elements wide even though only slots 0..30 exist. */
#define ACL_CACHE_LINK_SLOTS        32

/*
 * One directory entry: which ACL UID a slot currently holds, plus the two
 * rights bytes acl_$find_acl_slot replays into the caller's protection record
 * when the cached image is a "default ACL" (flag byte negative).
 */
typedef struct acl_$cache_dir_t {
    uid_t    acl_uid;           /* 0x00: `cmpm.l` pair at 0x00E45F06 */
    uint16_t hash_bucket;       /* 0x08: the UID_$HASH bucket this slot is
                                 *       chained in.  acl_$load_acl_image stores
                                 *       it (`move.w D0w,(0x808,A2)`, 0x00E45E5E)
                                 *       and acl_$alloc_cache_slot reads it back
                                 *       to unlink an evicted slot from its
                                 *       bucket (`move.w (0x808,A0),D2w`,
                                 *       0x00E45954). */
    int8_t   cached_flag;       /* 0x0A: `move.b (0x80a,A2),(A1)` 0x00E45F10 */
    uint8_t  reserved_0b;       /* 0x0B */
    uint16_t world_rights;      /* 0x0C: WORD - acl_$load_acl_image widens
                                 *       prot->world_rights into it
                                 *       (`move.w D5w,(0x80c,A2)`, 0x00E45E40).
                                 *       acl_$find_acl_slot reads only its low
                                 *       byte at +0x0D (0x00E45F24). */
    uint16_t subsys_rights;     /* 0x0E: WORD, same treatment
                                 *       (`move.w D5w,(0x80e,A2)`, 0x00E45E4A;
                                 *        low byte read at +0x0F, 0x00E45F2A) */
} acl_$cache_dir_t;

/* uid_t and words only, 4-byte alignment at most: unconditional. */
_Static_assert(__builtin_offsetof(acl_$cache_dir_t, acl_uid)       == 0x00, "cache_dir.acl_uid");
_Static_assert(__builtin_offsetof(acl_$cache_dir_t, cached_flag)   == 0x0A, "cache_dir.cached_flag");
_Static_assert(__builtin_offsetof(acl_$cache_dir_t, hash_bucket)   == 0x08, "cache_dir.hash_bucket");
_Static_assert(__builtin_offsetof(acl_$cache_dir_t, world_rights)  == 0x0C, "cache_dir.world_rights");
_Static_assert(__builtin_offsetof(acl_$cache_dir_t, subsys_rights) == 0x0E, "cache_dir.subsys_rights");
_Static_assert(sizeof(acl_$cache_dir_t) == 0x10, "sizeof acl_$cache_dir_t");

/*
 * One node of a circular doubly-linked slot list.  acl_$cache_list_insert /
 * acl_$cache_list_remove read `next` at +0 and `prev` at +2 (0x00E44C54,
 * 0x00E44CB2) and index the array with `lsl.l #0x2`.
 */
typedef struct acl_$cache_link_t {
    int16_t next;               /* 0x00 */
    int16_t prev;               /* 0x02 */
} acl_$cache_link_t;

_Static_assert(sizeof(acl_$cache_link_t) == 4, "sizeof acl_$cache_link_t");

/*
 * ============================================================================
 * ACL_$UNWIRED_DATA - the ACL_ module data block (map "D E7CF54 ACL_
 * size = BFC")
 * ============================================================================
 *
 * Module data block ACL_$UNWIRED_DATA: Claude Opus 5.5 (source-l2yd).
 *
 * Every Pascal ACL_ routine loads A5 with `lea (0xe7cf54).l,A5'; ACL_$INIT
 * (boot segment) uses the literal base (`movea.l #0xe7cf54,A0' 0x00E31142)
 * and REM_FILE reaches super_count through the map symbol ACL_$SUPER_COUNT
 * (`movea.l #0xe7dacc,A0' / `tst.w (-0x2,A0,D2w*0x1)', 0x00E619BC).  A
 * MODULE_DATA block linked in the SAU2 map's order after MST_UNWIRED and
 * before HINT_; the address is the ordering key, not the link address.
 *
 *   A5 off  image      field
 *   0x000   0xE7CF54   workspace[0x400]     `pea (A5)' (ACL_$CONVERT_TO_9ACL)
 *   0x400   0xE7D354   image_buf            `pea (0x400,A5)' 0x00E45D64
 *   0x800   0xE7D754   cache_dir[31]        (0x800,A2) with A2 = A5 + slot*0x10
 *   0x9F0   0xE7D944   cache_lru_links[32]  `pea (0x9f0,A5)' 0x00E45F4C
 *   0xA70   0xE7D9C4   cache_hash_links[32] (0xa70,A1) with A1 = A5 + slot*4
 *   0xAF0   0xE7DA44   cache_hash_buckets[61] (0xaf0,A0) with A0 = A5 + h*2
 *   0xB6A   0xE7DABE   (1 word, never addressed)
 *   0xB6C   0xE7DAC0   subs_magic           ACL_$ENTER_SUBS 0x00E46E08
 *   0xB70   0xE7DAC4   local_locksmith      `(0xb70,A5)'
 *   0xB72   0xE7DAC6   locksmith_owner_pid  `(0xb72,A5)'
 *   0xB74   0xE7DAC8   cache_free_head
 *   0xB76   0xE7DACA   cache_lru_head       `pea (0xb76,A5)' 0x00E45F50
 *   0xB76   0xE7DACA   super_count[0..64]   map ACL_$SUPER_COUNT is [1]
 *   0xBF8   0xE7DB4C   locksmith_override   `st (0xbf8,A5)'
 *   0xBF9   0xE7DB4D   (3 bytes, never addressed)
 *
 * super_count[pid]: A5 + 0xB76 + pid*2, ACL_$ENTER_SUPER `addq.w
 * #0x1,(0xb76,A0)' with A0 = A5 + PROC1_$CURRENT*2 (0x00E46FA8).  Pascal
 * [1..64] whose bias slot is cache_lru_head, so the two are union arms and
 * every user indexes with the pid; super_count[64] ends at
 * locksmith_override.
 *
 * Pointer-free.  acl_$cache_slot_t is packed and the other records are
 * uid_t / word records, so the layout is the same on a host and every
 * assert is unconditional.  Image contents (`gsk read 0xE7CF54 3068'): zero
 * except cache_hash_buckets[0..60] = -1 (0xE7DA44..0xE7DABD) and
 * cache_lru_head = -1 (0xE7DACA) - the empty-cache state.
 */
#define ACL_$UNWIRED_DATA_SIZE 0xBFC        /* map: ACL_ size = BFC */

/* The 0x400-byte workspace ACL_$CONVERT_TO_9ACL / _FROM_9ACL hand to
 * acl_$image_internal and ACL_$PRIM_CREATE with capacity 0x400; it ends
 * exactly at image_buf (A5+0x400). */
#define ACL_WORKSPACE_SIZE 0x400

typedef struct acl_$unwired_data_t {
    uint8_t           workspace[ACL_WORKSPACE_SIZE];         /* +0x000 */
    acl_$cache_slot_t image_buf;                            /* +0x400 */
    acl_$cache_dir_t  cache_dir[ACL_CACHE_SLOTS];           /* +0x800 */
    acl_$cache_link_t cache_lru_links[ACL_CACHE_LINK_SLOTS];  /* +0x9F0 */
    acl_$cache_link_t cache_hash_links[ACL_CACHE_LINK_SLOTS]; /* +0xA70 */
    int16_t           cache_hash_buckets[ACL_CACHE_HASH_BUCKETS]; /* +0xAF0 */
    int16_t           _0b6a;                                /* +0xB6A never addressed */
    int32_t           subs_magic;                           /* +0xB6C ACL_$ENTER_SUBS
                                                             *        (acl/enter_subs.c):
                                                             *        the manager's
                                                             *        return address */
    int16_t           local_locksmith;                      /* +0xB70 */
    int16_t           locksmith_owner_pid;                  /* +0xB72 */
    int16_t           cache_free_head;                      /* +0xB74 */
    union {                                                 /* +0xB76 */
        int16_t       cache_lru_head;
        /* [0..64], indexed with the pid; [0] is cache_lru_head */
        int16_t       super_count[PROC1_MAX_PROCESSES];
    };
    int8_t            locksmith_override;                   /* +0xBF8 Domain boolean */
    uint8_t           _0bf9[3];                             /* +0xBF9 never addressed */
} acl_$unwired_data_t;

_Static_assert(__builtin_offsetof(acl_$unwired_data_t, workspace) == 0x000, "workspace (A5)");
_Static_assert(__builtin_offsetof(acl_$unwired_data_t, image_buf) == 0x400, "image_buf (0x400,A5)");
_Static_assert(__builtin_offsetof(acl_$unwired_data_t, cache_dir) == 0x800, "cache_dir (0x800,A2)");
_Static_assert(__builtin_offsetof(acl_$unwired_data_t, cache_lru_links) == 0x9F0, "cache_lru_links (0x9f0,A5)");
_Static_assert(__builtin_offsetof(acl_$unwired_data_t, cache_hash_links) == 0xA70, "cache_hash_links (0xa70,A1)");
_Static_assert(__builtin_offsetof(acl_$unwired_data_t, cache_hash_buckets) == 0xAF0, "cache_hash_buckets (0xaf0,A0)");
_Static_assert(__builtin_offsetof(acl_$unwired_data_t, _0b6a) == 0xB6A, "61 buckets end at 0xB6A");
_Static_assert(__builtin_offsetof(acl_$unwired_data_t, subs_magic) == 0xB6C, "subs_magic (0xb6c,A5)");
_Static_assert(__builtin_offsetof(acl_$unwired_data_t, local_locksmith) == 0xB70, "local_locksmith (0xb70,A5)");
_Static_assert(__builtin_offsetof(acl_$unwired_data_t, locksmith_owner_pid) == 0xB72, "locksmith_owner_pid (0xb72,A5)");
_Static_assert(__builtin_offsetof(acl_$unwired_data_t, cache_free_head) == 0xB74, "cache_free_head (0xb74,A5)");
_Static_assert(__builtin_offsetof(acl_$unwired_data_t, cache_lru_head) == 0xB76, "cache_lru_head (0xb76,A5)");
_Static_assert(__builtin_offsetof(acl_$unwired_data_t, super_count) == 0xB76, "super_count[0] (0xb76,A0)");
_Static_assert(__builtin_offsetof(acl_$unwired_data_t, super_count[1]) == 0xB78,
               "map ACL_$SUPER_COUNT (0xE7DACC) = super_count[1]; stride 2");
_Static_assert(__builtin_offsetof(acl_$unwired_data_t, super_count[PROC1_MAX_PROCESSES]) == 0xBF8,
               "super_count[64] ends at locksmith_override");
_Static_assert(__builtin_offsetof(acl_$unwired_data_t, locksmith_override) == 0xBF8, "locksmith_override (0xbf8,A5)");
_Static_assert(sizeof(acl_$unwired_data_t) == ACL_$UNWIRED_DATA_SIZE, "ACL_: map size 0xBFC");

MODULE_DATA_DECLARE(acl_$unwired_data_t, ACL_$UNWIRED_DATA, 0x00E7CF54);



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
