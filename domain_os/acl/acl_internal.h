/*
 * ACL Internal Header - Access Control List Management
 *
 * Internal data structures and functions for the ACL subsystem.
 * This file should only be included by ACL implementation files.
 */

#ifndef ACL_INTERNAL_H
#define ACL_INTERNAL_H

#include "acl/acl.h"
#include "proc1/proc1.h"
#include "ml/ml.h"
#include "rgyc/rgyc.h"

/*
 * ============================================================================
 * Status Codes
 * ============================================================================
 */
#define status_$no_right_to_perform_operation   0x00230001
#define status_$project_list_is_full            0x00230011
#define status_$acl_proj_list_too_big           0x00230012
#define status_$image_buffer_too_small          0x0023000c
#define status_$cleanup_handler_set             0x00120035

/*
 * ============================================================================
 * Constants
 * ============================================================================
 */

/* Maximum number of project UIDs per process */
#define ACL_MAX_PROJECTS    8

/*
 * ============================================================================
 * ACL Data Structures
 * ============================================================================
 *
 * Per-process ACL data is stored in several parallel arrays indexed by PID.
 * Each process has:
 *   - Current SIDs (user, group, org, login): 36 bytes at 0x24 stride
 *   - Saved SIDs (for subsystem entry): 36 bytes at 0x24 stride
 *   - Pre-subsystem SIDs: 36 bytes at 0x24 stride
 *   - Project list: 12 bytes at 0x0C stride
 *   - Subsystem level counter: 2 bytes at 0x02 stride
 *   - Super mode counter: 2 bytes at 0x02 stride
 */

/*
 * SID block - Security ID information for a process (36 bytes)
 * Stored at stride 0x24 (36 bytes) per process
 */
typedef struct acl_sid_block_t {
    uid_t user_sid;      /* 0x00: User SID */
    uid_t group_sid;     /* 0x08: Group/Project SID */
    uid_t org_sid;       /* 0x10: Organization SID */
    uid_t login_sid;     /* 0x18: Login SID */
    uint32_t pad;        /* 0x20: Padding to 36 bytes */
} acl_sid_block_t;

/*
 * Project list entry (12 bytes per process)
 */
typedef struct acl_proj_list_t {
    uint32_t field_00;
    uint32_t field_04;
    uint32_t field_08;
} acl_proj_list_t;

/*
 * ============================================================================
 * ACL Global Variables
 * ============================================================================
 *
 * Original m68k addresses documented for reference.
 * A5-relative base: 0xE7CF54
 */

/*
 * ACL data base address: 0xE88834
 * Total size: 0xAD98 bytes (zeroed by ACL_$INIT)
 */

/*
 * Per-process SID arrays (indexed by PID, stride 0x24 = 36 bytes)
 * Each holds 9 uint32_t values (36 bytes) per process.
 */

/* Current SIDs: base 0xE90D10, offset from A5-relative 0xE97294 is -0x6584 */
extern acl_sid_block_t ACL_$CURRENT_SIDS[PROC1_MAX_PROCESSES];  /* 0xE90D10 */

/* Saved SIDs (pre-enter_super): base 0xE91610, offset -0x5C84 */
extern acl_sid_block_t ACL_$SAVED_SIDS[PROC1_MAX_PROCESSES];    /* 0xE91610 */

/* Original SIDs (pre-enter_subs): base 0xE90410, offset -0x6E84 */
extern acl_sid_block_t ACL_$ORIGINAL_SIDS[PROC1_MAX_PROCESSES]; /* 0xE90410 */

/*
 * Project lists metadata (indexed by PID, stride 0x0C = 12 bytes)
 * Used by SET/GET_RE_ALL_SIDS for opaque 12-byte project metadata
 */
extern acl_proj_list_t ACL_$PROJ_LISTS[PROC1_MAX_PROCESSES];    /* 0xE92228 */
extern acl_proj_list_t ACL_$SAVED_PROJ[PROC1_MAX_PROCESSES];    /* 0xE91F28 */

/*
 * Per-process project UID array (indexed by PID, 8 UIDs per process)
 * Used by ADD_PROJ/DELETE_PROJ/GET_PROJ_LIST for managing project UIDs
 * Stride 0x40 = 64 bytes per process (8 UIDs * 8 bytes)
 * Original address: 0xE924F4 (computed as 0xE97294 - 0x4DA0)
 */
extern uid_t ACL_$PROJ_UIDS[PROC1_MAX_PROCESSES][ACL_MAX_PROJECTS]; /* 0xE924F4 */

/*
 * Per-process subsystem level counter (indexed by PID, stride 2)
 * Incremented by ACL_$UP, decremented by ACL_$DOWN
 * Offset from 0xE97294: -0x3D5A = 0xE9353A
 */
extern int16_t ACL_$SUBSYS_LEVEL[PROC1_MAX_PROCESSES];          /* 0xE9353A */

/*
 * Per-process superuser mode counter (indexed by PID, stride 2)
 * Incremented by ENTER_SUPER, decremented by EXIT_SUPER
 * A5+0xB76 = 0xE7DACA
 */
extern int16_t ACL_$SUPER_COUNT[PROC1_MAX_PROCESSES];           /* 0xE7DACA */

/*
 * ASID bitmaps (8 bytes each, 64 bits for 64 ASIDs)
 */
extern uint8_t ACL_$ASID_FREE_BITMAP[8];    /* 0xE92534: 1=free */
extern uint8_t ACL_$ASID_SUSER_BITMAP[8];   /* 0xE935C4: 1=used suser */

/*
 * Locksmith state (A5+0xB70 = 0xE7DAC4)
 */
extern int16_t ACL_$LOCAL_LOCKSMITH;        /* 0xE7DAC4 */

/*
 * Locksmith override state
 */
extern int16_t ACL_$LOCKSMITH_OWNER_PID;    /* 0xE7DAC6 (A5+0xB72) */
extern int8_t ACL_$LOCKSMITH_OVERRIDE;      /* 0xE7DB4C (A5+0xBF8) */

/*
 * Subsystem entry magic value (stored on first entry)
 * A5+0xB6C = 0xE7DAC0
 */
extern int32_t ACL_$SUBS_MAGIC;             /* 0xE7DAC0 */

/*
 * Exclusion lock for ACL operations
 */
extern ml_$exclusion_t ACL_$EXCLUSION_LOCK; /* 0xE2C014 */

/*
 * ACL workspace buffer (A5-relative at offset 0)
 * Used by convert/image functions for temporary storage
 */
extern uint8_t ACL_$WORKSPACE[64];  /* 0xE7CF54 */

/*
 * Default ACL UIDs (referenced in acl.h, defined here for internal use)
 * These are loaded from RGYC during initialization.
 */
/* ACL_$DNDCAL - 0xE174DC */
/* ACL_$FNDWRX - 0xE174C4 */

/*
 * ACL type UIDs - used to identify ACL operations
 */
extern uid_t ACL_$FILEIN_ACL;       /* 0xE17454 */
extern uid_t ACL_$DIRIN_ACL;        /* 0xE1745C */
extern uid_t ACL_$DIR_MERGE_ACL;    /* 0xE17464 */
extern uid_t ACL_$FILE_MERGE_ACL;   /* 0xE1746C */
extern uid_t ACL_$FILE_SUBS_ACL;    /* 0xE17474 */

/*
 * ============================================================================
 * Internal Helper Functions
 * ============================================================================
 */

/*
 * acl_$check_suser_pid - Check if a process has superuser privileges
 *
 * Checks if the specified process is:
 *   - PID 1 (always superuser)
 *   - In super mode (SUPER_COUNT > 0)
 *   - Has login UID matching RGYC_$G_LOGIN_UID
 *   - Has user/group/login UID matching RGYC_$G_LOCKSMITH_UID
 *
 * If superuser, sets the corresponding bit in ASID_SUSER_BITMAP.
 *
 * Parameters:
 *   pid - Process ID to check
 *
 * Returns:
 *   Non-zero (0xFF) if superuser, 0 otherwise
 *
 * Original address: 0x00E463E4
 */
int8_t acl_$check_suser_pid(int16_t pid);

/*
 * ACL_$FREE_ASID - Free/reset ACL state for an ASID
 *
 * Resets all SID state for a process to system defaults:
 *   - User SID = RGYC_$P_SYS_USER_UID
 *   - Group SID = RGYC_$G_SYS_PROJ_UID
 *   - Org SID = RGYC_$O_SYS_ORG_UID
 *   - Login SID = UID_$NIL
 *
 * Marks ASID as free in bitmap and clears suser flag.
 *
 * Parameters:
 *   asid - Address space ID to free
 *   status_ret - Output status code
 *
 * Original address: 0x00E74C6A
 */
void ACL_$FREE_ASID(int16_t asid, status_$t *status_ret);

/*
 * ACL_$ALLOC_ASID - Allocate an ASID
 *
 * Finds a free ASID from the bitmap and marks it allocated.
 *
 * Parameters:
 *   asid_ret - Output ASID
 *   status_ret - Output status code
 *
 * Original address: 0x00E73BB8
 */
void ACL_$ALLOC_ASID(int16_t *asid_ret, status_$t *status_ret);

/*
 * ACL_$GET_SID - Get SID for an ASID
 *
 * Returns the current user SID for the specified ASID.
 *
 * Parameters:
 *   asid - Address space ID
 *   sid_ret - Output SID buffer
 *
 * Original address: 0x00E74C24
 */
void ACL_$GET_SID(int16_t asid, uid_t *sid_ret);

/*
 * acl_$is_process_type_2 - Check if process is type 2 (user process)
 *
 * Returns non-zero if the specified process is not type 2.
 * Type 2 appears to be a regular user process.
 *
 * Parameters:
 *   pid - Process ID to check
 *
 * Returns:
 *   Non-zero (-1) if process type != 2, 0 if type == 2
 *
 * Original address: 0x00E46498
 */
int8_t acl_$is_process_type_2(int16_t pid);

/*
 * acl_$image_internal - Internal image helper function
 *
 * Creates an internal image/representation of ACL data.
 *
 * Parameters:
 *   source_uid - Source UID
 *   buffer_len - Length of output buffer
 *   flag       - Operation flag
 *   output_buf - Output buffer
 *   len_out    - Output: actual length
 *   data_out   - Output: data buffer
 *   flag_out   - Output: flag byte
 *   status     - Output status code
 *
 * Original address: 0x00E47B78
 */
void acl_$image_internal(void *source_uid, int16_t buffer_len, int8_t flag,
                         void *output_buf, void *len_out, void *data_out,
                         void *flag_out, status_$t *status);

#endif /* ACL_INTERNAL_H */
