/*
 * DIR - Directory Operations Module (Internal Header)
 *
 * Internal structures, constants, and function prototypes for the
 * directory subsystem. This header should only be included by
 * implementation files within the dir/ module.
 */

#ifndef DIR_INTERNAL_H
#define DIR_INTERNAL_H

#include "dir/dir.h"
#include "acl/acl.h"
#include "ast/ast.h"
#include "file/file_internal.h" // we call FILE_$PRIV_UNLOCK
#include "name/name.h"
#include "mst/mst.h"
#include "network/network.h"
#include "proc1/proc1.h"
#include "misc/crash_system.h"

/*
 * ============================================================================
 * Internal Data Structures
 * ============================================================================
 */

/*
 * Dir_$OpResponse - Common response structure for DIR_$DO_OP
 *
 * This structure holds the response from directory operations.
 * The exact fields used depend on the operation type.
 */
typedef struct Dir_$OpResponse {
    uint8_t  f12;           /* 0x00: Response flags byte 1 */
    uint8_t  f13;           /* 0x01: Response flags byte 2 (continuation flag) */
    uint8_t  f14;           /* 0x02: Response flags byte 3 */
    uint8_t  f15;           /* 0x03: Response flags byte 4 (loop flag) */
    status_$t status;       /* 0x04: Operation status */
    uint8_t  f18[8];        /* 0x08: Operation-specific data */
    uint16_t _20_2_;        /* 0x10: Length field for some operations */
    uint32_t _22_4_;        /* 0x12: UID high for some operations */
    uint32_t f1a;           /* 0x16: UID low for some operations */
    uint32_t _24_4_;        /* 0x1A: Additional data */
} Dir_$OpResponse;

/*
 * Dir_$OpRequest - Base request structure for directory operations
 *
 * All directory operations share a common header format.
 * Operation-specific data follows this header.
 */
typedef struct Dir_$OpRequest {
    uint8_t  op;            /* 0x00: Operation code (DIR_OP_*) */
    uint8_t  padding[3];    /* 0x01-0x03: Padding */
    uid_t    uid;           /* 0x04-0x0B: Directory UID */
    uint16_t reserved;      /* 0x0C-0x0D: Reserved/type field */
    /* Operation-specific data follows */
} Dir_$OpRequest;

/*
 * Dir_$OpAddHardLinkuRequest - Request structure for ADD_HARD_LINKU
 */
typedef struct Dir_$OpAddHardLinkuRequest {
    uint8_t  op;                /* Operation code: DIR_OP_ADD_HARD_LINKU */
    uint8_t  padding[3];
    uid_t    uid1;              /* Directory UID */
    uint16_t padding2;
    uint8_t  field8_0x98[134];  /* Variable length name data */
    uid_t    uid2;              /* Target file UID */
    uint16_t path_len;          /* Name length */
} Dir_$OpAddHardLinkuRequest;

/*
 * ============================================================================
 * Internal Global Data References
 * ============================================================================
 */

/*
 * Directory operation parameter tables
 *
 * These tables contain operation-specific parameters indexed by operation type.
 * Located at 0xE7FC42 on M68K.
 */
extern uint16_t DAT_00e7fc42;   /* Base address of parameter tables */
extern uint16_t DAT_00e7fc4a;   /* ADD_HARD_LINKU params */
extern uint16_t DAT_00e7fc4e;
extern uint16_t DAT_00e7fc52;   /* DROP_HARD_LINKU params */
extern uint16_t DAT_00e7fc56;
extern uint16_t DAT_00e7fc62;   /* CNAMEU params */
extern uint16_t DAT_00e7fc66;
extern uint16_t DAT_00e7fc6a;   /* ADD_BAKU params */
extern uint16_t DAT_00e7fc6e;
extern uint16_t DAT_00e7fc72;   /* DELETE_FILEU params */
extern uint16_t DAT_00e7fc76;
extern uint16_t DAT_00e7fc7a;   /* CREATE_DIRU params */
extern uint16_t DAT_00e7fc7e;
extern uint16_t DAT_00e7fc82;   /* DROP_DIRU params */
extern uint16_t DAT_00e7fc86;
extern uint16_t DAT_00e7fc8a;   /* ADD_LINKU params */
extern uint16_t DAT_00e7fc8e;
extern uint16_t DAT_00e7fc92;   /* READ_LINKU params */
extern uint16_t DAT_00e7fc96;
extern uint16_t DAT_00e7fc9a;   /* DROP_LINKU params */
extern uint16_t DAT_00e7fc9e;
extern uint16_t DAT_00e7fcba;   /* FIX_DIR params */
extern uint16_t DAT_00e7fcbe;
extern uint16_t DAT_00e7fcca;   /* SET_DEFAULT_ACL params */
extern uint16_t DAT_00e7fcce;
extern uint16_t DAT_00e7fcd2;   /* GET_DEFAULT_ACL params */
extern uint16_t DAT_00e7fcd6;
extern uint16_t DAT_00e7fcda;   /* VALIDATE_ROOT_ENTRY params */
extern uint16_t DAT_00e7fcde;
extern uint16_t DAT_00e7fce2;   /* SET_PROTECTION params */
extern uint16_t DAT_00e7fce6;
extern uint16_t DAT_00e7fcea;   /* SET_DEF_PROTECTION params */
extern uint16_t DAT_00e7fcee;
extern uint16_t DAT_00e7fcf2;   /* GET_DEF_PROTECTION params */
extern uint16_t DAT_00e7fcf6;
extern uint16_t DAT_00e7fcfa;   /* RESOLVE params */
extern uint16_t DAT_00e7fcfe;
extern uint16_t DAT_00e7fd02;   /* ADD_MOUNT params - my_host_id word */
extern uint16_t DAT_00e7fd06;   /* ADD_MOUNT request size */
extern uint16_t DAT_00e7fd0a;   /* DROP_MOUNT params - my_host_id word */
extern uint16_t DAT_00e7fd0e;   /* DROP_MOUNT request size */

/*
 * ============================================================================
 * OLD_* Fallback Functions (for compatibility with older servers)
 * ============================================================================
 */

/* These are the legacy implementations called when the new protocol fails */

void DIR_$OLD_ADDU(uid_t *dir_uid, char *name, int16_t *name_len,
                   uid_t *file_uid, status_$t *status_ret);

void DIR_$OLD_ROOT_ADDU(uid_t *dir_uid, char *name, int16_t *name_len,
                        uid_t *file_uid, uint32_t *flags, status_$t *status_ret);

void DIR_$OLD_ADD_HARD_LINKU(uid_t *dir_uid, char *name, uint16_t *name_len,
                             uid_t *target_uid, status_$t *status_ret);

void DIR_$OLD_ADD_LINKU(uid_t *dir_uid, char *name, int16_t *name_len,
                        void *target, uint16_t *target_len, status_$t *status_ret);

void DIR_$OLD_ADD_BAKU(uid_t *dir_uid, char *name, uint16_t *name_len,
                       uid_t *backup_uid, status_$t *status_ret);

void DIR_$OLD_DELETE_FILEU(uid_t *dir_uid, char *name, uint16_t *name_len,
                           status_$t *param4, void *param5, status_$t *status_ret);

void DIR_$OLD_DROPU(uid_t *dir_uid, char *name, uint16_t *name_len,
                    uid_t *file_uid, status_$t *status_ret);

void DIR_$OLD_DROP_HARD_LINKU(uid_t *dir_uid, char *name, uint16_t *name_len,
                              uint16_t *flags, status_$t *status_ret);

void DIR_$OLD_DROP_LINKU(uid_t *dir_uid, char *name, uint16_t *name_len,
                         uid_t *target_uid, status_$t *status_ret);

void DIR_$OLD_CNAMEU(uid_t *dir_uid, char *old_name, uint16_t *old_name_len,
                     char *new_name, uint16_t *new_name_len, status_$t *status_ret);

void DIR_$OLD_CREATE_DIRU(uid_t *parent_uid, char *name, uint16_t *name_len,
                          uid_t *new_dir_uid, status_$t *status_ret);

void DIR_$OLD_DROP_DIRU(uid_t *parent_uid, char *name, uint16_t *name_high,
                        uint16_t *name_low, status_$t *status_ret);

void DIR_$OLD_FIX_DIR(uid_t *dir_uid, status_$t *status_ret);

void DIR_$OLD_GET_DEFAULT_ACL(uid_t *dir_uid, uid_t *acl_type, uid_t *acl_ret,
                              status_$t *status_ret);

void DIR_$OLD_SET_DEFAULT_ACL(uid_t *dir_uid, uid_t *acl_type, uid_t *acl_uid,
                              status_$t *status_ret);

void DIR_$OLD_VALIDATE_ROOT_ENTRY(char *name, uint16_t *name_len,
                                  status_$t *status_ret);

void DIR_$OLD_FIND_UID(uid_t *dir_uid, uid_t *target_uid, char *name_buf,
                       int16_t *name_len_ret, status_$t *status_ret);

uint32_t DIR_$OLD_FIND_NET(uid_t *dir_uid, uint32_t *index);

void DIR_$OLD_GET_ENTRYU(uid_t *dir_uid, char *name, uint16_t *name_len,
                         void *entry_ret, status_$t *status_ret);

void DIR_$OLD_READ_LINKU(int16_t dir_uid_low, int16_t name_low, uint16_t *name_len,
                         int16_t target_low, int16_t *target_len,
                         uid_t *target_uid, status_$t *status_ret);

/*
 * ============================================================================
 * Internal Helper Functions
 * ============================================================================
 */

/*
 * DIR_$GET_ENTRYU_FUN_00e4d460 - Internal entry retrieval helper
 *
 * Called by DIR_$GET_ENTRYU to perform the actual lookup.
 *
 * Original address: 0x00E4D460
 */
void DIR_$GET_ENTRYU_FUN_00e4d460(uid_t *local_uid, char *name,
                                   uint16_t name_len, void *entry_ret,
                                   status_$t *status_ret);

/*
 * DIR_$DIR_READU_FUN_00e4e1a8 - Internal directory read helper
 *
 * Originally a nested Pascal subprocedure of DIR_$DIR_READU.
 * Flattened to take explicit parameters from the parent function.
 *
 * Original address: 0x00E4E1A8
 */
void DIR_$DIR_READU_FUN_00e4e1a8(uid_t *dir_uid, int32_t *continuation,
                                  uint16_t *max_entries, int32_t *count_ret,
                                  void *flags, int32_t *eof_ret,
                                  status_$t *status_ret);

/*
 * FUN_00e4e1fe - Internal directory read helper
 *
 * Called by DIR_$DIR_READU for normal directory reads.
 *
 * Original address: 0x00E4E1FE
 */
void FUN_00e4e1fe(status_$t *status_ret);

/*
 * DIR_$ADD_ENTRY_INTERNAL - Internal add entry helper
 *
 * Shared implementation for DIR_$ADDU and DIR_$ROOT_ADDU.
 *
 * Original address: 0x00E500B8
 */
void DIR_$ADD_ENTRY_INTERNAL(uid_t *dir_uid, char *name, int16_t name_len,
                  uid_t *file_uid, uint32_t flags, status_$t *status_ret);

/*
 * FUN_00e4e786 - Internal find UID helper
 *
 * Shared implementation for DIR_$FIND_UID and DIR_$FIND_NET.
 *
 * Original address: 0x00E4E786
 */
void FUN_00e4e786(uid_t *dir_uid, uid_t *target_uid, int8_t flag,
                  int16_t name_buf_len, char *name_buf,
                  int16_t *name_len_ret, uint32_t *net_ret,
                  status_$t *status_ret);

/*
 * ============================================================================
 * Additional Internal Helper Functions
 * ============================================================================
 */

/* FUN_00e5674c - Shared add entry helper for OLD_ADDU and OLD_ADD_HARD_LINKU
 * Original address: 0x00E5674C
 */
void FUN_00e5674c(uid_t *dir_uid, char *name, uint16_t name_len,
                  uid_t *file_uid, uint8_t hard_link_flag,
                  status_$t *status_ret);

/* FUN_00e56b08 - Shared delete/drop helper
 * Original address: 0x00E56B08
 */
void FUN_00e56b08(uid_t *dir_uid, char *name, uint16_t name_len,
                  uint8_t flag1, uint8_t flag2, uint8_t flag3,
                  uint8_t *result_buf, status_$t *status_ret);

/* FUN_00e57f74 - Root directory entry lookup
 * Original address: 0x00E57F74
 */
void FUN_00e57f74(uid_t *dir_uid, char *name, uint16_t name_len,
                  void *entry_ret, status_$t *status_ret);

/* FUN_00e57ce0 - Non-root directory entry lookup
 * Original address: 0x00E57CE0
 */
void FUN_00e57ce0(uid_t *dir_uid, char *name, uint16_t name_len,
                  void *entry_ret, status_$t *status_ret);

/* FUN_00e56682 - Root add entry helper
 * Original address: 0x00E56682
 */
void FUN_00e56682(uid_t *dir_uid, uint16_t type, char *name,
                  uint16_t name_len, uid_t *file_uid,
                  uint32_t flags, status_$t *status_ret);

/* FUN_00e54414 - Validate and parse leaf name
 * Returns negative (true) on success, non-negative on failure
 * Original address: 0x00E54414
 */
int8_t FUN_00e54414(char *name, uint16_t name_len,
                    uint8_t *parsed_name, uint16_t *parsed_len);

/* FUN_00e54854 - Enter super mode / acquire directory lock
 * Original address: 0x00E54854
 */
void FUN_00e54854(uid_t *dir_uid, uint32_t *handle_ret,
                  uint32_t flags, status_$t *status_ret);

/* FUN_00e54734 - Release directory lock / exit super mode
 * Original address: 0x00E54734
 */
void FUN_00e54734(status_$t *status_ret);

/* FUN_00e5569c - Perform directory entry operation (drop link, etc.)
 * Original address: 0x00E5569C
 */
void FUN_00e5569c(uid_t *dir_uid, uint32_t handle, uint8_t *name,
                  uint16_t name_len, uint16_t op_type,
                  void *result, status_$t *status_ret);

/* FUN_00e54b9e - Find entry in directory by name
 * Original address: 0x00E54B9E
 */
int8_t FUN_00e54b9e(uint32_t handle, uint8_t *name, uint16_t name_len,
                    int32_t *entry_ret, uint16_t *param5,
                    uint16_t *param6);

/* FUN_00e54b58 - Compute hash for directory entry
 * Original address: 0x00E54B58
 */
uint16_t FUN_00e54b58(uint8_t *name, uint16_t name_len, uint16_t param3);

/* FUN_00e555dc - Update directory entry after rename
 * Original address: 0x00E555DC
 */
void FUN_00e555dc(uint32_t handle, uint16_t param2, uint16_t param3,
                  uint16_t hash);

/* FUN_00e55220 - Add entry to directory (non-root path)
 * Original address: 0x00E55220
 */
void FUN_00e55220(uid_t *dir_uid, uint32_t handle, uint8_t *name,
                  uint16_t name_len, uint16_t type, void *uid_data,
                  uint16_t flags, uint8_t *result, status_$t *status_ret);

/* FUN_00e55406 - Add entry to directory (root path, with replace)
 * Original address: 0x00E55406
 */
void FUN_00e55406(uid_t *dir_uid, uint32_t handle, uint8_t *name,
                  uint16_t name_len, uint16_t type, void *uid_data,
                  uint32_t extra, uint8_t replace_flag,
                  uint8_t *result, status_$t *status_ret);

/* FUN_00e5545c - Add link entry helper
 * Original address: 0x00E5545C
 */
void FUN_00e5545c(uid_t *dir_uid, uint32_t handle, uint8_t *name,
                  uint16_t name_len, void *target, uint16_t target_len,
                  uint8_t flags, uint8_t *result, status_$t *status_ret);

/* FUN_00e55764 - Read link data from entry
 * Original address: 0x00E55764
 */
void FUN_00e55764(uint32_t handle, int32_t entry, uint8_t *buf,
                  uint16_t *buf_len, status_$t *status_ret);

/* FUN_00e54546 - Create directory object
 * Original address: 0x00E54546
 */
void FUN_00e54546(uid_t *parent_uid, uint32_t handle, uint16_t type,
                  uid_t *new_dir_uid, status_$t *status_ret);

/* FUN_00e544b0 - Initialize directory buffer
 * Original address: 0x00E544B0
 */
void FUN_00e544b0(void *buffer);

/* FUN_00e56a04 - Fix root entry
 * Original address: 0x00E56A04
 */
void FUN_00e56a04(uid_t *dir_uid, char *name, uint16_t name_len,
                  void *entry_data);

/* FUN_00e4dffe - Canned root directory read
 * Original address: 0x00E4DFFE
 */
void FUN_00e4dffe(void);

/* FUN_00e4bc26 - Check if status code is retryable
 * Original address: 0x00E4BC26
 */
int8_t FUN_00e4bc26(int16_t status);

/* FUN_00e4bc76 - Update hint after redirect
 * Original address: 0x00E4BC76
 */
void FUN_00e4bc76(void *uid, void *hint1, int16_t hint2, void *redirect, uint16_t param5);

/* FUN_00e4bec2 - Audit CNAMEU operation
 * Original address: 0x00E4BEC2
 */
void FUN_00e4bec2(uint16_t audit_type, status_$t status, uid_t *uid,
                  uint16_t name_len, uint16_t new_name_len,
                  void *name, void *new_name);

/* FUN_00e4bd48 - Audit link operation
 * Original address: 0x00E4BD48
 */
void FUN_00e4bd48(uint16_t audit_type, status_$t status, uid_t *uid,
                  uint16_t name_len, void *name, uint16_t target_len,
                  uint32_t target_data);

/* FUN_00e4be16 - Audit add/drop entry operation
 * Original address: 0x00E4BE16
 */
void FUN_00e4be16(uint16_t audit_type, status_$t status, uid_t *uid,
                  uid_t *file_uid, uint16_t name_len, void *name);

/* FUN_00e4bf92 - Audit resolve operation
 * Original address: 0x00E4BF92
 */
void FUN_00e4bf92(uint32_t pname_data, uint16_t path_len,
                  void *result, status_$t status);

/* FUN_00e4bce0 - Audit mount/drop mount operation
 * Original address: 0x00E4BCE0
 */
void FUN_00e4bce0(uint16_t audit_type, status_$t status, uid_t *uid,
                  void *mount_uid, uint32_t extra);

/* FUN_00e4af28 - Audit protection operation
 * Original address: 0x00E4AF28
 */
void FUN_00e4af28(status_$t status, uid_t *uid, void *prot_data,
                  uid_t *acl_type, void *acl_data, uint16_t param6);

/* FUN_00e53728 - Cleanup directory handle entry
 * Original address: 0x00E53728
 */
void FUN_00e53728(void *handle_entry, uint8_t flag, status_$t *status_ret);

/* FUN_00e4b340 - Get request header by version
 * Original address: 0x00E4B340
 */
void *FUN_00e4b340(void *handle_entry, int16_t version);

/* FUN_00e4b838 - Release request buffer
 * Original address: 0x00E4B838
 */
void FUN_00e4b838(void *handle_entry);

/* FUN_00e4b9d6 - Release directory handle slot
 * Original address: 0x00E4B9D6
 */
void FUN_00e4b9d6(void **handle_ptr);

/* FUN_00e4c9e4 - Callback for add entry with root flag
 * Original address: 0x00E4C9E4
 */
void FUN_00e4c9e4(void);

/* FUN_00e4d5b4 - Read link helper used by DO_OP case 0x3e
 * Original address: 0x00E4D5B4
 */
void FUN_00e4d5b4(uid_t *uid, void *name, uint16_t name_len,
                  uint16_t buf_len, uint32_t extra, void *link_type_ret,
                  uid_t *uid_ret, status_$t *status_ret);

/* DIR_$OLD_DIR_READU - Legacy directory read
 * Original address: 0x00E57C80
 */
/* DIR_$OLD_DIR_READU - Legacy directory read
 * Checks for canned root, crashes if so, then calls FUN_00e579c0.
 * Dereferences param_3 and param_4 before passing to helper.
 * Original address: 0x00E57C80
 */
void DIR_$OLD_DIR_READU(uid_t *uid, void *param_2, void *param_3,
                        void *param_4, void *param_5, void *param_6,
                        status_$t *status_ret);

/* DIR_$OLD_READ_INFOBLK - Read directory info block
 * Reads info block data from a directory, up to max_len bytes.
 * Original address: 0x00E560A6
 */
void DIR_$OLD_READ_INFOBLK(uid_t *dir_uid, void *info_data,
                            int16_t *max_len, int16_t *actual_len,
                            status_$t *status_ret);

/* DIR_$OLD_WRITE_INFOBLK - Write directory info block
 * Writes info block data to a directory.
 * Original address: 0x00E5613C
 */
void DIR_$OLD_WRITE_INFOBLK(uid_t *dir_uid, void *info_data,
                             int16_t *len, status_$t *status_ret);

/* DIR_$OLD_INIT - Legacy directory init
 * Original address: 0x00E314F4
 */
void DIR_$OLD_INIT(void);

/* DIR_$OLD_CLEANUP - Legacy directory cleanup
 * Original address: 0x00E54B2A
 */
void DIR_$OLD_CLEANUP(void);

/* FUN_00e579c0 - Internal directory read implementation
 * Called by DIR_$OLD_DIR_READU after root UID check.
 * Original address: 0x00E579C0
 */
void FUN_00e579c0(uid_t *uid, void *param_2, uint32_t param_3,
                  uint32_t param_4, void *param_5, void *param_6,
                  status_$t *status_ret);

/*
 * ============================================================================
 * Additional Internal Data
 * ============================================================================
 */

/* Directory operation parameter tables for SET_ACL */
extern uint16_t DAT_00e7fcc2;   /* SET_ACL type field */
extern uint16_t DAT_00e7fcc6;   /* SET_ACL request size */

/* Directory handle slot data base address: 0xE7DC00 */
extern uint32_t DAT_00e7fc3c;   /* Active slots bitmap */
extern uint32_t DAT_00e7fc34;   /* Additional bitmap */
extern uint32_t DAT_00e7f470;   /* Counter/flag */
extern uint32_t DAT_00e7fbf4;   /* Counter/flag */
extern uint32_t DAT_00e7f4b0;   /* Counter/flag */
extern void    *DAT_00e7fc30;   /* Free list head (handle entries) */
extern void    *DAT_00e7fc38;   /* Free list head (request buffers) */
extern uint8_t  DAT_00e7f280;   /* Start of handle entry pool */
extern uint8_t  DAT_00e7f4bc;   /* Start of request buffer pool */
extern uint16_t DAT_00e7fc40;   /* Link buffer mutex owner */

/* Event counters and mutexes */
extern ec_$eventcount_t DIR_$WAIT_ECS[];    /* Array of 32 event counters (base) */
extern ml_$exclusion_t  DIR_$MUTEX;         /* Directory exclusion mutex */
extern ml_$exclusion_t  DIR_$LINK_BUF_MUTEX;/* Link buffer mutex */
extern ec_$eventcount_t DIR_$WT_FOR_HDNL_EC;/* Wait-for-handle event counter */

/* Hint subsystem data */
extern uint8_t DAT_00e7fb9c;    /* Hint parameter table base */
extern uint8_t DAT_00e7fba0;    /* Hint size table base */

/* DAT_00e4b33c - UID_$NIL reference used as lock callback */
extern uint8_t DAT_00e4b33c;

/* Naming error string for crash */
extern char *PTR_Naming_bad_request_header_ver_err_00e7dbfc;
extern char  Naming_bad_request_header_ver_err;
/* Alias for crash in OLD_DIR_READU */
extern char Bad_request_header_version_err;

/*
 * OLD directory subsystem data area
 *
 * Base address: 0xE7FD24 (runtime, A5-relative in OLD functions)
 * The OLD functions use a flat data area with various arrays at
 * known offsets. The handle slot array starts at offset 0x2B8
 * with 8-byte entries indexed by process current index.
 *
 * Layout (relative to base 0xE7FD24):
 *   0x2B8 + i*8 : slot handle pointer for slot i (uint32_t)
 */
#define DIR_OLD_NUM_SLOTS 58  /* dbf 0x39 = 58 iterations */
#define DIR_OLD_HANDLE_OFFSET 0x2B8

extern uint8_t DAT_00e7fd24;  /* OLD directory data base */
extern uint32_t DAT_00e7ffdc; /* First handle slot (0xe7fd24 + 0x2b8) */

/*
 * Status codes used by OLD functions
 * Only define if not already defined in included headers
 */
#ifndef status_$wrong_type
#define status_$wrong_type                          0x000F0001
#endif
#ifndef status_$naming_illegal_directory_operation
#define status_$naming_illegal_directory_operation  0x000E0011
#endif
#ifndef status_$naming_bad_type
#define status_$naming_bad_type                     0x000E0012
#endif
#ifndef status_$naming_not_root_dir
#define status_$naming_not_root_dir                 0x000E001F
#endif
#ifndef status_$naming_ran_out_of_address_space
#define status_$naming_ran_out_of_address_space     0x000E0016
#endif
#ifndef status_$naming_entry_repaired
#define status_$naming_entry_repaired               0x000E0023
#endif
#ifndef status_$naming_entry_stale
#define status_$naming_entry_stale                  0x000E0022
#endif
#ifndef status_$naming_invalid_link_operation
#define status_$naming_invalid_link_operation        0x000E000A
#endif
#ifndef status_$naming_directory_not_empty
#define status_$naming_directory_not_empty           0x000E000C
#endif
#ifndef status_$naming_name_is_not_a_file
#define status_$naming_name_is_not_a_file            0x000E000E
#endif
#ifndef status_$naming_object_is_not_an_acl_object
#define status_$naming_object_is_not_an_acl_object   0x000E002F
#endif
#ifndef status_$no_right_to_perform_operation
#define status_$no_right_to_perform_operation        0x00230001
#endif
#ifndef status_$insufficient_rights_to_perform_operation
#define status_$insufficient_rights_to_perform_operation 0x00230002
#endif

/* DIR_OP_SET_ACL and DIR_OP_GET_ENTRYU operation codes */
#ifndef DIR_OP_SET_ACL
#define DIR_OP_SET_ACL       0x4A
#endif
#ifndef DIR_OP_GET_ENTRYU_OP
#define DIR_OP_GET_ENTRYU_OP 0x44
#endif

/* ACL subsystem externs (not already in acl.h) */
extern uid_t ACL_$DIRIN_ACL;
extern int16_t ACL_TYPE_DIR;
extern int16_t ACL_TYPE_FILE;

/* ACL_$DEFAULT_ACL - Get default ACL for a type */
void ACL_$DEFAULT_ACL(uid_t *acl_ret, int16_t *type);

/* ACL_$DNDCAL and ACL_$FNDWRX - default ACL UIDs */
extern uid_t ACL_$DNDCAL;
extern uid_t ACL_$FNDWRX;

/* ACL_$DEF_ACLDATA - Fill default ACL data */
void ACL_$DEF_ACLDATA(void *acl_data_out, void *uid_out);

/*
 * Externs for functions/data already declared in included headers
 * (file.h, file_internal.h, ml.h, ast.h, proc1.h, etc.)
 * are NOT re-declared here to avoid conflicts.
 *
 * These are accessible via the #include chain:
 *   FILE_$SET_ACL, FILE_$SET_PROT, FILE_$GET_ATTRIBUTES,
 *   FILE_$SET_REFCNT, FILE_$FW_FILE, FILE_$FW_PARTIAL,
 *   FILE_$DELETE_OBJ, FILE_$TRUNCATE, FILE_$PRIV_CREATE,
 *   FILE_$PRIV_LOCK, FILE_$PRIV_UNLOCK,
 *   ML_$EXCLUSION_INIT, ML_$EXCLUSION_STOP,
 *   MST_$UNMAP, MST_$UNMAP_PRIVI, MST_$MAPS,
 *   AST_$GET_LOCATION, AST_$GET_COMMON_ATTRIBUTES, AST_$SET_ATTRIBUTE,
 *   AUDIT_$ENABLED, PROC1_$AS_ID, PROC1_$CURRENT, PROC1_$TYPE,
 *   NODE_$ME, HINT_$GET_HINTS, HINT_$ADDI,
 *   REM_FILE_$RN_DO_OP, MAP_CASE, UNMAP_CASE, CRASH_SYSTEM
 */

/* NAME_CONVERT_ACL_STATUS - convert ACL status to naming status */
void NAME_CONVERT_ACL_STATUS(status_$t *status_ret);

/*
 * ACL_$RIGHTS, REM_FILE_$DROP_HARD_LINKU, etc. are already
 * declared in acl/acl.h, rem_file/rem_file.h (included via
 * file/file_internal.h). FUN_00e4e786 is declared earlier
 * in this file. No need to re-declare.
 */

/* REM_FILE_$SET_DEF_ACL - Remote set default ACL
 * (Not declared in rem_file.h)
 */
void REM_FILE_$SET_DEF_ACL(void *location, uid_t *dir_uid,
                           uid_t *acl_type, uid_t *acl_uid,
                           status_$t *status_ret);

/* REM_NAME_$GET_ENTRY - Remote name get entry
 * (Not declared in rem_file.h)
 */
void REM_NAME_$GET_ENTRY(uid_t *dir_uid, char *name, uint16_t *name_len,
                         void *entry_ret, status_$t *status_ret);

/* Various internal functions called by DO_OP switch cases */
void FUN_00e5044a(uid_t *uid, void *name, uint16_t name_len, uid_t *file_uid,
                  uint16_t flags, status_$t *status_ret);
void FUN_00e4fef2(uid_t *uid, uint16_t type, void *name, uint16_t name_len,
                  uint16_t entry_type, uint32_t extra, void *uid_data,
                  uint16_t target_len, uint32_t target_data,
                  void *result, status_$t *status_ret);
void FUN_00e5125e(uid_t *uid, void *name, uint16_t name_len, uint8_t flag1,
                  uint16_t flag2, uint16_t flag3, void *buf,
                  uid_t *result_uid, status_$t *status_ret);
void FUN_00e518bc(uid_t *uid, uint32_t type_info, int16_t name_ptr,
                  uint32_t name_info, int16_t new_name_ptr,
                  uint32_t status_info);
void FUN_00e50832(uid_t *uid, uint16_t type, void *name_ptr, uint16_t name_len,
                  void *uid_data, uid_t *result_uid, status_$t *status_ret);
void FUN_00e52576(uid_t *uid, void *name, uint16_t name_len,
                  void *result_uid, status_$t *status_ret);
void FUN_00e52744(uid_t *uid, void *name, uint16_t name_len,
                  status_$t *status_ret);
void FUN_00e511da(uid_t *uid, uint16_t type, void *name, uint16_t name_len,
                  uint16_t entry_type, void *result_uid, status_$t *status_ret);
void FUN_00e4d954(uid_t *uid, int16_t count, void *entries, uint16_t flags,
                  void *params, uint32_t cont, uint32_t max,
                  uint32_t size, void *count_ret, void *eof_ret,
                  void *result, status_$t *status_ret);
void FUN_00e4cffa(uid_t *uid, void *name, uint16_t name_len,
                  void *type_ret, void *uid_ret, void *extra_ret,
                  status_$t *status_ret);
void FUN_00e4e41a(uid_t *uid, void *params, uint8_t flag,
                  void *name_ret, void *len_ret, void *uid_ret,
                  status_$t *status_ret);
void FUN_00e53a18(uid_t *uid, status_$t *status_ret);
/* FUN_00e52bc2 is DIR_$SET_ACL - declared in dir.h */
void FUN_00e52fa6(uid_t *uid, void *type, void *acl, status_$t *status_ret);
void FUN_00e53128(uid_t *uid, uid_t *type, uid_t *acl_ret, status_$t *status_ret);
void FUN_00e501d2(void *name, uint16_t name_len, status_$t *status_ret);
void FUN_00e5216a(void *uid, void *prot, int16_t type);
void FUN_00e52044(uid_t *uid, void *type, void *prot, void *acl, status_$t *status_ret);
void FUN_00e51cf6(uid_t *uid, void *type, void *prot_ret, void *acl_ret, status_$t *status_ret);
void FUN_00e4d0e2(uint32_t path_data, uint16_t path_len, void *result,
                  void *uid_ret, void *extra, void *flags1, void *flags2,
                  void *cont, void *size, void *eof, void *count,
                  uint32_t max, void *link_count, status_$t *status_ret);
void FUN_00e5325e(uid_t *uid, void *mount_uid, uint32_t lv_num, status_$t *status_ret);
void FUN_00e533e6(void *mount_uid, uint32_t lv_num, status_$t *status_ret);

/* DAT_00e56096 - Info block parameter table entries */
extern uint8_t DAT_00e56096;
extern uint8_t DAT_00e56098;
extern uint8_t DAT_00e56094;
extern uint8_t DAT_00e5609e;
extern uint8_t DAT_00e560a2;
extern uint8_t DAT_00e5609a;
extern uint8_t DAT_00e54730;
extern uint8_t DAT_00e5716c;
extern uint8_t DAT_00e56946;
extern uint8_t DAT_00e564de;
extern uint8_t DAT_00e564e2;
extern uint8_t DAT_00e5716a;
extern uint8_t DAT_00e54b28;

/* ACL_$NIL extern */
extern uid_t ACL_$NIL;

#endif /* DIR_INTERNAL_H */
