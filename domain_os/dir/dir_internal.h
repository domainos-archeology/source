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
#include "proc1/proc1.h"

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
void DIR_$GET_ENTRYU_FUN_00e4d460(status_$t *status_ret);

/*
 * DIR_$DIR_READU_FUN_00e4e1a8 - Internal directory read helper
 *
 * Called by DIR_$DIR_READU for canned root reads.
 *
 * Original address: 0x00E4E1A8
 */
void DIR_$DIR_READU_FUN_00e4e1a8(status_$t *status_ret);

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

/* Global data */
extern uint32_t NODE_$ME;       /* This node's ID */

#endif /* DIR_INTERNAL_H */
