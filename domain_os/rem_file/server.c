/*
 * REM_FILE_$SERVER - Remote file operations server dispatcher
 *
 * This is the server-side handler for incoming remote file operation requests.
 * It receives requests from remote nodes, dispatches to the appropriate handler,
 * and sends back responses.
 *
 * Original address: 0x00E63586
 * Size: 4108 bytes
 *
 * The server handles approximately 30 different operation codes including:
 * - Lock/unlock operations (opcodes 0x0A, 0x0C, 0x84)
 * - File attribute operations (opcodes 0x04, 0x10, 0x1E, 0x20)
 * - Protection/ACL operations (opcodes 0x18, 0x80, 0x82)
 * - Directory operations (opcodes 0x1C, 0x22, 0x24, 0x28)
 * - Area management (opcodes 0x86, 0x88, 0x8A)
 * - Node/process management (opcodes 0x00, 0x12, 0x26, 0x7E)
 *
 * Protocol:
 * - Requests arrive via APP_$RECEIVE on protocol 2
 * - Request byte 0: message length
 * - Request byte 1 (local_439): operation code
 * - Request bytes 2+: operation-specific data
 * - Response byte 0: 0x80 (server response marker)
 * - Response byte 1: operation code + 1
 * - Response bytes 2+: operation-specific response data
 */

#include "rem_file/rem_file_internal.h"
#include "file/file_internal.h"
#include "name/name.h"
#include "dir/dir.h"
#include "area/area.h"
#include "ml/ml.h"
#include "app/app.h"
#include "netbuf/netbuf.h"
#include "pkt/pkt.h"

/*
 * Server opcodes (received in local_439)
 * These are distinct from client opcodes in rem_file_internal.h
 */
#define SERVER_OP_TEST              0x00    /* Test connectivity */
#define SERVER_OP_SET_ATTRIBUTE     0x04    /* Set file attribute */
#define SERVER_OP_TRUNCATE          0x08    /* Truncate or delete file */
#define SERVER_OP_LOCK              0x0A    /* Lock file (simple) */
#define SERVER_OP_UNLOCK            0x0C    /* Unlock file */
#define SERVER_OP_NEIGHBORS         0x10    /* Check same volume */
#define SERVER_OP_NODE_CRASH        0x12    /* Remote node crashed - cleanup */
#define SERVER_OP_PURIFY            0x14    /* Purify file pages */
#define SERVER_OP_LOCAL_READ_LOCK   0x16    /* Read local lock info */
#define SERVER_OP_SET_DEF_ACL       0x18    /* Set default ACL */
#define SERVER_OP_LOCAL_LOCK_VERIFY 0x1A    /* Verify lock ownership */
#define SERVER_OP_GET_ENTRY         0x1C    /* Get directory entry */
#define SERVER_OP_GET_SEG_MAP       0x1E    /* Get segment map */
#define SERVER_OP_INVALIDATE        0x20    /* Invalidate AST entry */
#define SERVER_OP_ADD_HARD_LINK     0x22    /* Add hard link */
#define SERVER_OP_GENERATE_UID      0x24    /* Generate unique UID */
#define SERVER_OP_CREATE_PRESR10    0x26    /* Pre-SR10 create */
#define SERVER_OP_DROP_HARD_LINK    0x28    /* Drop hard link */
#define SERVER_OP_CREATE_TYPE       0x7E    /* Create typed object */
#define SERVER_OP_SET_PROT          0x80    /* Set protection */
#define SERVER_OP_SET_ATTRIB        0x82    /* Set attribute with SIDs */
#define SERVER_OP_LOCK_EXTENDED     0x84    /* Extended lock request */
#define SERVER_OP_CREATE_AREA       0x86    /* Create area */
#define SERVER_OP_DELETE_AREA       0x88    /* Delete area */
#define SERVER_OP_GROW_AREA         0x8A    /* Grow area */

/*
 * Response header structure
 */
#define RESPONSE_MAGIC  0x80

/*
 * External data references
 */
extern ml_exclusion_t REM_FILE_$SOCK_LOCK;      /* Socket access lock */
extern int8_t NETLOG_$OK_TO_LOG_SERVER;         /* Server logging flag */
extern uint32_t NETWORK_$FILE_BACKLOG[];        /* Request backlog counters */
extern int8_t NETWORK_$DISKLESS;                /* Diskless node flag */
extern int8_t NETWORK_$REALLY_DISKLESS;         /* Really diskless flag */
extern uint32_t NETWORK_$MOTHER_NODE;           /* Mother node ID */
extern uint32_t DAT_00e823fc;                   /* Something counter */
extern status_$t File_Comms_Problem_With_Remote_Node_Err;

/* Reference for nil constant */
extern uint8_t DAT_00e61d18[];                  /* Nil/empty data constant */
extern uint8_t DAT_00e61718[];                  /* Project list constant */

/*
 * Case conversion table reference (for UNMAP_CASE/MAP_CASE)
 */
extern uint8_t DAT_00e62d48[];                  /* Case mapping table */

/*
 * External function declarations
 */
extern void APP_$RECEIVE(int16_t protocol, void *packet_info, status_$t *status);
extern void NETBUF_$RTN_HDR(void *buffer);
extern void NETBUF_$GETVA(int32_t packet, void *va_out, status_$t *status);
extern void PKT_$DUMP_DATA(void *packet_info, uint16_t len);
extern void PKT_$DAT_COPY(void *packet_info, int16_t len, void *dest);
extern void OS_$DATA_COPY(void *src, void *dest, uint32_t len);
extern void UNMAP_CASE(void *name_in, void *name_len_out, void *name_out,
                       void *table, void *result_len, void *status_bytes);
extern void MAP_CASE(void *name_in, void *name_len, void *name_out,
                     void *table, void *result_len, void *status_bytes);
extern void CRASH_SHOW_STRING(const char *msg);
extern void CRASH_SYSTEM(status_$t *status);
extern void AREA_$FREE_FROM(int16_t node);
extern int16_t AREA_$CREATE_FROM(int16_t node, uint16_t param2, uint16_t param3,
                                  int16_t param4, void *param5);
extern void AREA_$DELETE_FROM(uint16_t param1, uint32_t node, uint32_t param3, status_$t *status);
extern void AREA_$GROW_TO(uint16_t param1, uint32_t param2, uint32_t param3, status_$t *status);
extern void DIR_$DROP_MOUNT(uid_t *dir_uid, uid_t *uid2, void *node, status_$t *status);
extern void DIR_$OLD_SET_DEFAULT_ACL(void *file_uid, void *acl1, void *acl2, void *status);
extern void DIR_$OLD_ADD_HARD_LINKU(uid_t *dir_uid, uid_t *entry_uid, void *name,
                                     void *entry_info, status_$t *status);
extern void DIR_$OLD_DROP_HARD_LINKU(void *dir_uid, void *name, void *name_len,
                                      void *entry_info, void *status);
extern int8_t DIR_$GET_ENTRYU(void *dir_uid, void *name, void *name_len,
                               void *entry_type, status_$t *status);
extern void ACL_$GET_RE_ALL_SIDS(void *buf1, void *buf2, void *buf3, void *buf4, status_$t *status);
extern void ACL_$SET_RE_ALL_SIDS(void *buf1, void *buf2, void *buf3, void *buf4, status_$t *status);
extern void ACL_$GET_PROJ_LIST(void *proj_list, void *table, void *count, status_$t *status);
extern void ACL_$SET_PROJ_LIST(void *proj_list, void *count, status_$t *status);
extern void ACL_$ENTER_SUPER(void);
extern void ACL_$EXIT_SUPER(void);
extern void ACL_$OVERRIDE_LOCAL_LOCKSMITH(int16_t enable, status_$t *status);
extern int8_t ACL_$CONVERT_TO_10ACL(void *acl_uid, void *file_uid, void *result_uid,
                                     void *acl_data, status_$t *status);
extern void ACL_$GET_ACL_ATTRIBUTES(void *uid, int16_t flags, void *attrs, status_$t *status);
extern void AUDIT_$SUSPEND(void);
extern void AUDIT_$RESUME(void);
extern uid_t RGYC_$G_LOCKSMITH_UID;
extern uid_t SLINK_$UID;

/*
 * ============================================================================
 * Nested Helper Functions (static)
 * ============================================================================
 */

/*
 * REM_FILE_$SERVER__unmap_name - Convert network name to local format
 *
 * Uses UNMAP_CASE to convert a filename from network format (possibly uppercase)
 * to local case format.
 *
 * Original address: 0x00E62CEA
 *
 * @param name_in      Input name buffer (offset into stack frame)
 * @param name_len_out Output: name length pointer
 */
static void server_unmap_name(void *stack_frame, int name_offset, int len_offset)
{
    uint8_t temp_name[40];
    int16_t temp_len;
    uint8_t status_bytes[2];
    int16_t i;
    uint8_t *name_in = (uint8_t *)stack_frame + name_offset;
    int16_t *name_len = (int16_t *)((uint8_t *)stack_frame + len_offset);

    UNMAP_CASE(name_in, name_len, temp_name, DAT_00e62d48, &temp_len, status_bytes);

    *name_len = temp_len;

    /* Copy converted name back */
    for (i = 0; i < temp_len; i++) {
        name_in[i] = temp_name[i];
    }
}

/*
 * REM_FILE_$SERVER__get_entry - Get directory entry with case conversion
 *
 * Converts name, calls DIR_$GET_ENTRYU, then maps result back.
 *
 * Original address: 0x00E62D4A
 *
 * @param request   Request buffer with dir UID, name, name length
 * @param response  Response buffer for result
 * @return Directory entry type
 */
static int8_t server_get_entry(void *request, void *response)
{
    int8_t result;
    uint8_t *req = (uint8_t *)request;
    uint8_t *resp = (uint8_t *)response;
    int16_t name_len;
    int16_t entry_type;
    uint8_t temp_name[32];
    uint8_t entry_info[12];
    uint8_t status_bytes[2];
    int16_t i;

    /* Unmap name from request */
    server_unmap_name(request, 0x0C, 0x2C);

    /* Get directory entry */
    result = DIR_$GET_ENTRYU(req + 4, req + 0x0C, req + 0x2C, &entry_type,
                              (status_$t *)(resp + 4));

    if (*(status_$t *)(resp + 4) == status_$ok) {
        /* Map name back for response */
        MAP_CASE(req + 0x0C, req + 0x2C, resp + 0x0C, DAT_00e62d48,
                 resp + 0x0A, status_bytes);

        if (status_bytes[0] < 0) {
            /* Name mapping failed */
            *(uint16_t *)(resp + 0x0A) = 0x20;
            *(status_$t *)(resp + 4) = 0x0E002D;
        }

        *(uint16_t *)(resp + 8) = entry_type;

        /* Copy entry info (12 bytes) */
        for (i = 0; i < 12; i++) {
            resp[0x2C + i] = entry_info[i];
        }
    }

    return result;
}

/*
 * REM_FILE_$SERVER__set_attribute - Handle SET_ATTRIBUTE with ACL conversion
 *
 * Handles attribute type 3 (ACL) with format conversion, and type 4 (object type)
 * with special handling for symbolic links.
 *
 * Original address: 0x00E62DE8
 *
 * @param stack_frame Parent stack frame pointer
 */
static void server_set_attribute(uint8_t *stack_frame)
{
    status_$t status;
    uint8_t acl_attrs[12];
    uint32_t acl_data[11];
    uint8_t temp_buf1[8];
    uint8_t temp_buf2[8];
    uid_t result_uid;
    uint8_t *file_uid = stack_frame - 0x434;
    int16_t *attr_type = (int16_t *)(stack_frame - 0x42C);
    uint8_t *attr_value = stack_frame - 0x42A;
    status_$t *status_out = (status_$t *)(stack_frame - 0x19C);
    uint16_t *reply_len = (uint16_t *)(stack_frame - 0x4C8);

    *reply_len = 8;

    if (*attr_type == 3) {
        /* ACL attribute - may need format conversion */
        uint16_t acl_flags = (*(uint16_t *)(stack_frame - 0x426) & 0xFF0) >> 4;

        if ((acl_flags & 0xE0) == 0) {
            /* Standard ACL format - check if needs 10ACL conversion */
            uint8_t *uid_buf = stack_frame - 0x28;

            /* Copy file UID to local buffer */
            *(uint32_t *)(uid_buf) = *(uint32_t *)file_uid;
            *(uint32_t *)(uid_buf + 4) = *(uint32_t *)(file_uid + 4);
            uid_buf[8] &= ~0x40;  /* Clear flag */

            ACL_$GET_ACL_ATTRIBUTES((void *)uid_buf, 1, acl_attrs, &status);
            *status_out = status;

            if (status != status_$ok) {
                return;
            }

            if (acl_attrs[0] != 0) {
                /* Need to convert to 10ACL format */
                uint8_t *old_acl = stack_frame - 0x42A;
                uint8_t temp_acl[8];

                /* Save original ACL value */
                *(uint32_t *)temp_acl = *(uint32_t *)old_acl;
                *(uint32_t *)(temp_acl + 4) = *(uint32_t *)(old_acl + 4);

                ACL_$CONVERT_TO_10ACL(temp_acl, file_uid, (uid_t *)&result_uid,
                                       acl_data, &status);

                /* Store converted result */
                *(uint32_t *)old_acl = result_uid.high;
                *(uint32_t *)(old_acl + 4) = result_uid.low;
                *status_out = status;
            }
        } else {
            /* Funky ACL format */
            ACL_$CONVERT_FUNKY_ACL(attr_value, acl_data, (uid_t *)&result_uid,
                                    temp_buf1, &status);

            *(uint32_t *)attr_value = result_uid.high;
            *(uint32_t *)(attr_value + 4) = result_uid.low;
            *status_out = status;

            /* Clear bit 0 */
            attr_value[4] &= ~0x01;
        }

        if (*status_out != status_$ok) {
            return;
        }

        /* Copy to extended response area */
        *(uint32_t *)(stack_frame - 0x3FE) = *(uint32_t *)attr_value;
        *(uint32_t *)(stack_frame - 0x3FA) = *(uint32_t *)(attr_value + 4);

        /* Copy ACL data (44 bytes) */
        {
            int16_t i;
            uint32_t *src = acl_data;
            uint32_t *dst = (uint32_t *)attr_value;
            for (i = 0; i < 11; i++) {
                *dst++ = *src++;
            }
        }

        *attr_type = 0x14;  /* Extended attribute type */
    } else if (*attr_type == 4) {
        /* Object type attribute - check for symbolic link */
        if (*(uint32_t *)attr_value == SLINK_$UID.high &&
            *(uint32_t *)(attr_value + 4) == SLINK_$UID.low) {
            /* Cannot set type to symbolic link */
            status = 0x000F0015;  /* file_$incompatible_request */
            goto done;
        }
    }

    /* Set the attribute */
    AST_$SET_ATTRIBUTE((uid_t *)file_uid, *attr_type, attr_value, &status);

done:
    *status_out = status;
}

/*
 * REM_FILE_$SERVER__get_entry_sids - Get directory entry with SID impersonation
 *
 * For non-version-0x32 requests, temporarily sets the caller's SIDs before
 * performing the directory lookup, then restores original SIDs.
 *
 * Original address: 0x00E62F54
 *
 * @param stack_frame Parent stack frame pointer
 */
static void server_get_entry_sids(uint8_t *stack_frame)
{
    int16_t request_version = *(int16_t *)(stack_frame - 0x4CA);
    uint16_t *reply_len = (uint16_t *)(stack_frame - 0x4C8);
    status_$t *status_out = (status_$t *)(stack_frame - 0x19C);

    *reply_len = 0x38;

    if (request_version == 0x32) {
        /* Version 0x32 - simple get entry */
        server_get_entry(stack_frame - 0x438, stack_frame - 0x1A0);
    } else {
        /* Need to impersonate caller's SIDs */
        int8_t sids_set = 0;
        int8_t proj_set = 0;
        uint8_t saved_sids1[40];
        uint8_t saved_sids2[40];
        uint8_t saved_proj1[16];
        uint8_t saved_proj2[16];
        uid_t saved_proj_list[9];
        uint8_t saved_proj_count[2];
        status_$t temp_status;

        ACL_$ENTER_SUPER();
        AUDIT_$SUSPEND();

        ACL_$GET_RE_ALL_SIDS(saved_sids1, saved_sids2, saved_proj1, saved_proj2, status_out);
        if (*status_out != status_$ok) goto cleanup;

        ACL_$GET_PROJ_LIST(saved_proj_list, DAT_00e61718, saved_proj_count, status_out);
        if (*status_out != status_$ok) goto cleanup;

        /* Set caller's SIDs */
        ACL_$SET_RE_ALL_SIDS(saved_sids1, stack_frame - 0x404, saved_proj1, saved_proj2, status_out);
        if (*status_out != status_$ok) goto cleanup;

        sids_set = -1;

        /* Set caller's project list */
        ACL_$SET_PROJ_LIST(stack_frame - 0x3E0, stack_frame - 0x3A0, status_out);
        if (*status_out != status_$ok) goto cleanup;

        proj_set = -1;

        /* Perform the operation with caller's credentials */
        AUDIT_$RESUME();
        ACL_$EXIT_SUPER();

        server_get_entry(stack_frame - 0x438, stack_frame - 0x1A0);

        /* Re-enter super mode to restore SIDs */
        ACL_$ENTER_SUPER();
        AUDIT_$SUSPEND();

cleanup:
        /* Restore original SIDs if we changed them */
        if (sids_set < 0) {
            ACL_$SET_RE_ALL_SIDS(saved_sids1, saved_sids2, saved_proj1, saved_proj2, &temp_status);
        }
        if (proj_set < 0) {
            ACL_$SET_PROJ_LIST(saved_proj_list, saved_proj_count, &temp_status);
        }

        AUDIT_$RESUME();
        ACL_$EXIT_SUPER();
    }
}

/*
 * REM_FILE_$SERVER__drop_link - Drop hard link with locksmith override
 *
 * Handles drop hard link requests, optionally enabling locksmith privileges
 * if the request indicates admin operation.
 *
 * Original address: 0x00E63096
 *
 * @param stack_frame Parent stack frame pointer
 */
static void server_drop_link(uint8_t *stack_frame)
{
    status_$t *status_out = (status_$t *)(stack_frame - 0x19C);
    uint16_t *reply_len = (uint16_t *)(stack_frame - 0x4C8);
    int8_t admin_flag = *(int8_t *)(stack_frame - 0x406);
    uint8_t saved_sids1[40];
    uint8_t saved_sids2[40];
    uint8_t saved_proj1[16];
    uint8_t saved_proj2[16];
    uid_t locksmith_uid;
    status_$t temp_status;

    /* Unmap name */
    server_unmap_name(stack_frame, -(int)(0x42C - (uintptr_t)stack_frame),
                      -(int)(0x40C - (uintptr_t)stack_frame));

    if (admin_flag < 0) {
        /* Admin operation - need locksmith override */
        ACL_$ENTER_SUPER();
        AUDIT_$SUSPEND();

        ACL_$OVERRIDE_LOCAL_LOCKSMITH(-1, status_out);
        if (*status_out != status_$ok) goto cleanup_super;

        ACL_$GET_RE_ALL_SIDS(saved_sids1, saved_sids2, saved_proj1, saved_proj2, status_out);
        if (*status_out != status_$ok) goto cleanup_super;

        /* Set locksmith UID */
        locksmith_uid.high = RGYC_$G_LOCKSMITH_UID.high;
        locksmith_uid.low = RGYC_$G_LOCKSMITH_UID.low;

        ACL_$SET_RE_ALL_SIDS(saved_sids2, saved_sids2, saved_proj1, saved_proj2, status_out);
        if (*status_out != status_$ok) goto cleanup_super;
    }

    /* Perform the drop operation */
    DIR_$OLD_DROP_HARD_LINKU(stack_frame - 0x434, stack_frame - 0x42C,
                              stack_frame - 0x40C, stack_frame - 0x40A, status_out);

    if (admin_flag < 0) {
        /* Restore original SIDs */
        locksmith_uid.high = UID_$NIL.high;
        locksmith_uid.low = UID_$NIL.low;

        ACL_$SET_RE_ALL_SIDS(saved_sids1, saved_sids2, saved_proj1, saved_proj2, &temp_status);
        ACL_$OVERRIDE_LOCAL_LOCKSMITH(0, &temp_status);
    }

cleanup_super:
    if (admin_flag < 0) {
        AUDIT_$RESUME();
        ACL_$EXIT_SUPER();
    }

    /* Check for stale entry error */
    if (*status_out == 0x000E0016) {
        *(uint16_t *)(stack_frame - 0x1A0) = 0xFFFF;
        DAT_00e823fc++;
    }

    *reply_len = 8;
}

/*
 * REM_FILE_$SERVER__truncate_delete - Handle truncate or delete operations
 *
 * Handles file truncation (positive flag) or deletion (negative flag).
 * For truncation, returns the new file size.
 *
 * Original address: 0x00E631C2
 *
 * @param stack_frame Parent stack frame pointer
 */
static void server_truncate_delete(uint8_t *stack_frame)
{
    status_$t status;
    status_$t *status_out = (status_$t *)(stack_frame - 0x19C);
    uint16_t *reply_len = (uint16_t *)(stack_frame - 0x4C8);
    uid_t *file_uid = (uid_t *)(stack_frame - 0x434);
    int8_t delete_flag = *(int8_t *)(stack_frame - 0x42C);
    uint8_t *target_uid = stack_frame - 0x104;
    uint8_t attrs[0x100];
    uint16_t attr_value;

    *status_out = status_$ok;

    /* Copy file UID to target buffer */
    *(uint32_t *)target_uid = file_uid->high;
    *(uint32_t *)(target_uid + 4) = file_uid->low;
    target_uid[8] &= ~0x40;  /* Clear flag */

    if (delete_flag < 0 && *(uint8_t *)(stack_frame - 0x436) == 0) {
        /* Delete operation - check if file has refcount > 1 */
        AST_$GET_ATTRIBUTES((uid_t *)target_uid, 0x81, attrs, status_out);

        if (*status_out == status_$ok && attrs[0] == 0) {
            /* Set refcount attribute to 1 before delete */
            attr_value = 1;
            AST_$SET_ATTRIBUTE(file_uid, 7, &attr_value, status_out);
        }
    }

    *reply_len = 8;

    if (*status_out == status_$ok || *status_out == 0x00030007) {
        if (delete_flag < 0) {
            /* Delete the file */
            FILE_$DELETE(file_uid, status_out);
        } else {
            /* Truncate the file */
            uint32_t new_size = *(uint32_t *)(stack_frame - 0x42A);
            uint8_t truncate_result[8];

            AST_$TRUNCATE(file_uid, new_size, 0, truncate_result, status_out);

            /* Get updated attributes for response */
            AST_$GET_ATTRIBUTES((uid_t *)target_uid, 0x80, attrs, status_out);

            /* Copy size info to response */
            *(uint32_t *)(stack_frame - 0x198) = *(uint32_t *)(attrs + 0x38);
            *(uint16_t *)(stack_frame - 0x194) = *(uint16_t *)(attrs + 0x3C);

            *reply_len = 0x10;
        }
    }
}

/*
 * REM_FILE_$SERVER__generate_uid - Generate a unique UID
 *
 * Generates a new UID, verifying it doesn't already exist by attempting
 * to look it up. Retries up to 10 times if the generated UID exists.
 *
 * Original address: 0x00E632C2
 *
 * @param stack_frame Parent stack frame pointer
 */
static void server_generate_uid(uint8_t *stack_frame)
{
    status_$t *status_out = (status_$t *)(stack_frame - 0x19C);
    uint16_t *reply_len = (uint16_t *)(stack_frame - 0x4C8);
    uid_t *result_uid = (uid_t *)(stack_frame - 0x198);
    int16_t *retry_count = (int16_t *)(stack_frame - 0x4C0);
    uid_t generated_uid;
    uint8_t uid_buf[16];
    uint8_t location[8];
    uint8_t status_bytes[4];

    *retry_count = 0;

    do {
        (*retry_count)++;

        /* Generate a new UID */
        UID_$GEN(&generated_uid);

        /* Copy to buffer with flag cleared */
        uid_buf[8] &= ~0x40;
        *(uint32_t *)uid_buf = generated_uid.high;
        *(uint32_t *)(uid_buf + 4) = generated_uid.low;

        /* Try to get location - if it succeeds, UID exists */
        AST_$GET_LOCATION((uid_t *)uid_buf, 1, stack_frame - 0x470, status_bytes, status_out);

        if (*status_out == 0x000F0001) {
            /* Object not found - UID is unique */
            break;
        }
    } while (*retry_count < 11);

    if (*status_out == 0x000F0001) {
        /* Success - UID is unique */
        *status_out = status_$ok;
    }

    /* Store result */
    result_uid->high = generated_uid.high;
    result_uid->low = generated_uid.low;

    *reply_len = 0x12;
}

/*
 * REM_FILE_$SERVER__set_prot_attrib - Set protection or attributes with SID impersonation
 *
 * Handles SET_PROT (opcode 0x80) and SET_ATTRIB (opcode 0x82) operations,
 * temporarily impersonating the caller's SIDs.
 *
 * Original address: 0x00E63344
 *
 * @param stack_frame Parent stack frame pointer
 */
static void server_set_prot_attrib(uint8_t *stack_frame)
{
    status_$t *status_out = (status_$t *)(stack_frame - 0x19C);
    uint16_t *reply_len = (uint16_t *)(stack_frame - 0x4C8);
    int8_t opcode_flag = *(int8_t *)(stack_frame - 0x435);
    int8_t sids_set = 0;
    int8_t proj_set = 0;
    uint8_t saved_sids1[40];
    uint8_t saved_sids2[40];
    uint8_t saved_proj1[16];
    uint8_t saved_proj2[16];
    uid_t saved_proj_list[9];
    uint8_t saved_proj_count[2];
    status_$t temp_status[3];
    uint16_t prot_type = 0;

    *reply_len = 0xBE;

    /* Map attribute type to protection type for SET_PROT */
    if (opcode_flag == (int8_t)0x80) {
        uint16_t attr_type = *(uint16_t *)(stack_frame - 0x42A);
        switch (attr_type) {
            case 3:   prot_type = 6; break;
            case 0x10: prot_type = 0; break;
            case 0x11: prot_type = 1; break;
            case 0x12: prot_type = 2; break;
            case 0x13: prot_type = 4; break;
            case 0x14: prot_type = 5; break;
            case 0x15: prot_type = 3; break;
        }
    }

    /* Save and set caller's SIDs */
    ACL_$ENTER_SUPER();
    AUDIT_$SUSPEND();

    ACL_$GET_RE_ALL_SIDS(saved_sids1, saved_sids2, saved_proj1, saved_proj2, status_out);
    if (*status_out != status_$ok) goto cleanup;

    ACL_$GET_PROJ_LIST(saved_proj_list, DAT_00e61718, saved_proj_count, status_out);
    if (*status_out != status_$ok) goto cleanup;

    /* Set caller's SIDs */
    {
        uint8_t *caller_sids = (opcode_flag == (int8_t)0x80) ?
                               (stack_frame - 0x3F4) : (stack_frame - 0x3F0);
        ACL_$SET_RE_ALL_SIDS(saved_sids1, caller_sids, saved_proj1, saved_proj2, status_out);
    }
    if (*status_out != status_$ok) goto cleanup;

    sids_set = -1;

    /* Set caller's project list */
    {
        uint8_t *caller_proj = (opcode_flag == (int8_t)0x80) ?
                               (stack_frame - 0x3D0) : (stack_frame - 0x3CC);
        ACL_$SET_PROJ_LIST(caller_proj, DAT_00e61718, status_out);
    }
    if (*status_out != status_$ok) goto cleanup;

    proj_set = -1;

    /* Perform the operation */
    AUDIT_$RESUME();
    ACL_$EXIT_SUPER();

    if (opcode_flag == (int8_t)0x80) {
        /* SET_PROT */
        FILE_$SET_PROT_INT((uid_t *)(stack_frame - 0x434), stack_frame - 0x428,
                           *(uint16_t *)(stack_frame - 0x42A), prot_type,
                           *(int8_t *)(stack_frame - 0x42C), status_out);
    } else {
        /* SET_ATTRIB */
        FILE_$SET_ATTRIBUTE((uid_t *)(stack_frame - 0x434), *(int16_t *)(stack_frame - 0x42A),
                            stack_frame - 0x424, *(int16_t *)(stack_frame - 0x42C), status_out);
    }

    if (*status_out == status_$ok) {
        /* Get updated attributes for response */
        uint8_t *target_uid = stack_frame - 0x104;
        uid_t *file_uid = (uid_t *)(stack_frame - 0x434);

        *(uint32_t *)target_uid = file_uid->high;
        *(uint32_t *)(target_uid + 4) = file_uid->low;

        AST_$GET_ATTRIBUTES((uid_t *)target_uid, 0x81, stack_frame - 0x194, status_out);
    }

    /* Re-enter super mode to restore SIDs */
    ACL_$ENTER_SUPER();
    AUDIT_$SUSPEND();

cleanup:
    if (sids_set < 0) {
        ACL_$SET_RE_ALL_SIDS(saved_sids1, saved_sids2, saved_proj1, saved_proj2, temp_status);
    }
    if (proj_set < 0) {
        ACL_$SET_PROJ_LIST(saved_proj_list, saved_proj_count, temp_status);
    }

    AUDIT_$RESUME();
    ACL_$EXIT_SUPER();
}


/*
 * ============================================================================
 * Main Server Dispatcher
 * ============================================================================
 */

/*
 * REM_FILE_$SERVER - Main remote file operations server
 *
 * This function runs as a server process that receives and handles
 * remote file operation requests from other nodes.
 *
 * Original address: 0x00E63586
 *
 * Flow:
 * 1. Acquire socket lock
 * 2. Receive request via APP_$RECEIVE
 * 3. Parse request header and dispatch to appropriate handler
 * 4. Format and send response
 * 5. Release socket lock and repeat
 *
 * The function is a large dispatcher (~4KB) with inline handling for
 * simple operations and calls to nested helpers for complex ones.
 */
void REM_FILE_$SERVER(void)
{
    status_$t status;
    int8_t lock_held = -1;
    int8_t has_extra_data = 0;
    uint8_t opcode;
    uint16_t request_len;
    uint16_t reply_len;
    uint32_t node_id;

    /* Large stack frame for request/response buffers */
    struct {
        /* Request data copied from network packet */
        int16_t  msg_version;       /* -0x43C: Protocol version */
        uint8_t  flags1;            /* -0x43A */
        uint8_t  opcode;            /* -0x439: Operation code */
        uid_t    uid1;              /* -0x438: Primary UID */
        uid_t    uid2;              /* -0x430: Secondary UID */
        uid_t    uid3;              /* -0x428: Third UID */
        uid_t    uid4;              /* -0x420: Fourth UID */
        uint8_t  data[0x400];       /* Additional request data */

        /* Response data */
        uint16_t resp_type;         /* -0x1A4: Response type indicator */
        uint8_t  resp_magic;        /* -0x1A2: Response magic (0x80) */
        uint8_t  resp_opcode;       /* -0x1A1: Response opcode */
        status_$t resp_status;      /* -0x1A0: Response status */
        uint8_t  resp_data[0x100];  /* Response data */

        /* Working buffers */
        uint8_t  attrs[0x108];      /* Attribute buffer */
        uid_t    work_uid;          /* Working UID */

        /* Control variables */
        uint16_t reply_len;         /* -0x4C8: Reply length */
        uint16_t request_len;       /* -0x4CA: Request length */
    } frame;

    /* Acquire exclusion lock for socket operations */
    ML_$EXCLUSION_START(&REM_FILE_$SOCK_LOCK);

    /* Receive request */
    APP_$RECEIVE(2, &frame, &status);

    if (status == 0x000D0003) {  /* status_$network_buffer_queue_is_empty */
        goto done;
    }
    if (status != status_$ok) {
        goto done;
    }

    /* Log request if server logging enabled */
    if (NETLOG_$OK_TO_LOG_SERVER < 0) {
        clock_t timestamp;
        TIME_$ABS_CLOCK(&timestamp);
    }

    /* Update backlog statistics */
    {
        uint8_t backlog_index = frame.opcode;  /* Simplified */
        if (backlog_index < 9) {
            NETWORK_$FILE_BACKLOG[backlog_index]++;
        }
    }

    /* Validate and copy request data */
    request_len = frame.request_len;
    if (request_len > 0x294) {
        request_len = 0x294;
    }

    /* Prepare response header */
    frame.resp_magic = RESPONSE_MAGIC;
    frame.resp_opcode = frame.opcode + 1;
    frame.resp_type = 1;
    lock_held = -1;

    /* Check if requests are allowed */
    if ((DAT_00e24c3f & 2) == 0) {
        frame.resp_status = 0x000D0008;  /* status_$network_request_denied_by_remote_node */
        goto send_response;
    }

    /* Release lock for most operations */
    if (frame.opcode != SERVER_OP_NODE_CRASH) {
        ML_$EXCLUSION_STOP(&REM_FILE_$SOCK_LOCK);
        lock_held = 0;
    }

    opcode = frame.opcode;
    node_id = 0;  /* From packet header */

    /* Dispatch based on opcode */
    switch (opcode) {

    case SERVER_OP_TEST:
        /* Simple connectivity test */
        frame.resp_status = status_$ok;
        break;

    case SERVER_OP_SET_ATTRIBUTE:
        /* Set file attribute */
        server_set_attribute((uint8_t *)&frame + sizeof(frame));
        break;

    case SERVER_OP_TRUNCATE:
        /* Truncate or delete file */
        server_truncate_delete((uint8_t *)&frame + sizeof(frame));
        break;

    case SERVER_OP_LOCK:
    case SERVER_OP_LOCK_EXTENDED:
        /* Lock file */
        {
            uint16_t lock_flags = (opcode == SERVER_OP_LOCK_EXTENDED) ?
                                  (frame.uid3.low & 0xFFFF) | 2 : 0x8A;
            uint16_t lock_mode = frame.uid3.high >> 16;
            uint16_t rights = (frame.uid3.high >> 8) & 0xFF;

            FILE_$PRIV_LOCK(&frame.uid1, 0, lock_mode, rights, -1,
                            (lock_flags << 16) | frame.msg_version,
                            frame.uid2.high, frame.uid2.low, 0,
                            (opcode == SERVER_OP_LOCK_EXTENDED) ? frame.data : DAT_00e61d18,
                            0, NULL, NULL, &frame.resp_status);

            /* Handle lock status remapping */
            if (frame.resp_status == 0x000F0009) {
                frame.resp_status = 0x000F000A;
            } else if (frame.resp_status == status_$ok) {
                if (opcode == SERVER_OP_LOCK_EXTENDED) {
                    /* Get extended attributes for response */
                    AST_$GET_ATTRIBUTES(&frame.uid4, 0x81, frame.resp_data, &frame.resp_status);
                    reply_len = 0xBE;
                } else {
                    /* Simple lock response */
                    AST_$GET_DTV(&frame.uid1, 0, frame.resp_data, &status);
                    reply_len = (request_len == 0x1E) ? 0x0E : 0x10;
                }
            }
        }
        break;

    case SERVER_OP_UNLOCK:
        /* Unlock file */
        FILE_$PRIV_UNLOCK(&frame.uid1, 0, frame.uid3.high & 0xFFFF0000,
                          ((int32_t)0xFF << 8) | (frame.uid4.high & 0xFF),
                          frame.uid2.high, frame.uid2.low,
                          frame.resp_data, &frame.resp_status);
        reply_len = 0x16;
        break;

    case SERVER_OP_NEIGHBORS:
        /* Check if files are on same volume */
        FILE_$NEIGHBORS(&frame.uid1, &frame.uid2, &frame.resp_status);
        reply_len = 10;
        break;

    case SERVER_OP_NODE_CRASH:
        /* Remote node crashed - clean up its locks and areas */
        if (NETWORK_$DISKLESS < 0 && node_id == NETWORK_$MOTHER_NODE) {
            CRASH_SHOW_STRING("    diskless partner node has crashed");
            CRASH_SYSTEM(&File_Comms_Problem_With_Remote_Node_Err);
        }

        /* Release all locks held by the crashed node */
        {
            uint16_t index = 0;
            uint8_t lock_info[0x40];

            while (1) {
                FILE_$READ_LOCK_ENTRYI(&UID_$NIL, &index, lock_info, &status);
                if (status != status_$ok) break;

                /* Check if lock is from crashed node */
                if ((*(uint32_t *)(lock_info + 0x10) & 0xFFFFF) == node_id) {
                    FILE_$PRIV_UNLOCK((uid_t *)lock_info, 0, 0,
                                      ((int32_t)0xFF << 8) | (lock_info[0x1C] & 0xFF),
                                      *(int32_t *)(lock_info + 0x14),
                                      *(int32_t *)(lock_info + 0x10),
                                      NULL, &status);
                }
            }
        }

        /* Free areas allocated for the node */
        AREA_$FREE_FROM((int16_t)node_id);

        /* Drop mount point if really diskless */
        if (NETWORK_$REALLY_DISKLESS >= 0) {
            DIR_$DROP_MOUNT(&NAME_$NODE_UID, &UID_$NIL, &node_id, &status);
        }

        /* No response for node crash */
        goto done;

    case SERVER_OP_PURIFY:
        /* Purify file pages */
        AST_$PURIFY(&frame.uid1, (frame.uid2.high & 0xFF) | 4,
                    frame.uid2.high >> 16, DAT_00e61d18, 0, &frame.resp_status);
        break;

    case SERVER_OP_LOCAL_READ_LOCK:
        /* Read local lock info */
        FILE_$LOCAL_READ_LOCK(&frame.uid1, frame.resp_data, &frame.resp_status);
        reply_len = 0x2A;
        break;

    case SERVER_OP_SET_DEF_ACL:
        /* Set default ACL */
        if (frame.uid4.high >> 16 == 3) {
            if ((int8_t)(frame.uid4.high >> 24) < 0) {
                ACL_$ENTER_SUPER();
            }
            DIR_$OLD_SET_DEFAULT_ACL(&frame.uid1, &frame.uid2, &frame.uid3, &frame.resp_status);
            if ((int8_t)(frame.uid4.high >> 24) < 0) {
                ACL_$EXIT_SUPER();
            }
        } else {
            frame.resp_status = 0x000F0003;
        }

        if (frame.resp_status == 0x000E0016) {
            frame.resp_type = 0xFFFF;
            DAT_00e823fc++;
        }
        break;

    case SERVER_OP_LOCAL_LOCK_VERIFY:
        /* Verify lock ownership */
        FILE_$LOCAL_LOCK_VERIFY(&frame.uid2, &frame.resp_status);
        break;

    case SERVER_OP_GET_ENTRY:
        /* Get directory entry */
        server_get_entry_sids((uint8_t *)&frame + sizeof(frame));
        break;

    case SERVER_OP_GET_SEG_MAP:
        /* Get segment map */
        {
            uint16_t flags = (request_len == 0x14) ?
                             (frame.uid2.low >> 16) >= 0 : 0;
            uint32_t start_page = (request_len == 0x14) ?
                                  (frame.uid2.high & 0xFFFF) << 15 :
                                  frame.uid3.high << 10;
            uint16_t page_count = frame.uid3.low >> 16;
            uint16_t max_pages = frame.uid3.low & 0xFFFF;

            AST_$GET_SEG_MAP(&frame.uid1, start_page, 0, page_count, max_pages,
                             flags, frame.resp_data, &frame.resp_status);
            reply_len = 0x28;
        }
        break;

    case SERVER_OP_INVALIDATE:
        /* Invalidate AST entry */
        AST_$INVALIDATE(&frame.uid1, frame.uid2.high, frame.uid2.low,
                        frame.uid3.high & 0xFF, &frame.resp_status);
        break;

    case SERVER_OP_ADD_HARD_LINK:
        /* Add hard link */
        server_unmap_name(&frame.uid2, 0, 0);
        if ((int8_t)frame.data[0] < 0) {
            ACL_$ENTER_SUPER();
        }
        DIR_$OLD_ADD_HARD_LINKU(&frame.uid1, &frame.uid2, frame.data + 4,
                                 frame.data + 6, &frame.resp_status);
        if ((int8_t)frame.data[0] < 0) {
            ACL_$EXIT_SUPER();
        }

        if (frame.resp_status == 0x000E0016) {
            frame.resp_type = 0xFFFF;
            DAT_00e823fc++;
        }
        break;

    case SERVER_OP_GENERATE_UID:
        /* Generate unique UID */
        server_generate_uid((uint8_t *)&frame + sizeof(frame));
        break;

    case SERVER_OP_DROP_HARD_LINK:
        /* Drop hard link */
        server_drop_link((uint8_t *)&frame + sizeof(frame));
        break;

    case SERVER_OP_CREATE_PRESR10:
        /* Pre-SR10 create */
        {
            uint16_t create_flags = (frame.uid2.low == 0) ? 3 : 2;
            FILE_$PRIV_CREATE(frame.uid2.high & 0xFF, &UID_$NIL, &frame.uid1,
                              &frame.uid3, 0, create_flags, NULL, &frame.resp_status);
            reply_len = 0x12;
        }
        break;

    case SERVER_OP_CREATE_TYPE:
        /* Create typed object */
        FILE_$PRIV_CREATE(frame.data[0x10], &frame.uid3, &frame.uid4, &frame.uid2,
                          *(uint32_t *)frame.data, frame.data[0x12] | 2,
                          frame.data + 0x18, &frame.resp_status);

        if (frame.resp_status == status_$ok || frame.resp_status == 0x00020007) {
            /* Get attributes for response */
            uint8_t *target = frame.attrs;
            *(uint32_t *)target = frame.uid2.high;
            *(uint32_t *)(target + 4) = frame.uid2.low;
            target[8] &= ~0x40;

            AST_$GET_ATTRIBUTES((uid_t *)target, 1, frame.resp_data, &status);
            if (status != status_$ok) {
                frame.resp_status = status;
            }
        }
        reply_len = 0xBE;
        break;

    case SERVER_OP_SET_PROT:
    case SERVER_OP_SET_ATTRIB:
        /* Set protection or attribute with SID impersonation */
        server_set_prot_attrib((uint8_t *)&frame + sizeof(frame));
        break;

    case SERVER_OP_CREATE_AREA:
        /* Create area */
        {
            uint16_t page_count = (request_len < 0x1C) ?
                                  frame.uid2.high >> 16 : frame.uid3.low >> 16;
            *(uint16_t *)frame.resp_data = AREA_$CREATE_FROM((int16_t)node_id,
                                                              frame.uid2.high >> 16,
                                                              page_count,
                                                              frame.uid2.low >> 16,
                                                              &frame.resp_status);
            *(uint16_t *)(frame.resp_data + 2) = 0x400;
            reply_len = 0x0C;
        }
        break;

    case SERVER_OP_DELETE_AREA:
        /* Delete area */
        AREA_$DELETE_FROM(frame.uid3.high & 0xFF, node_id, frame.uid2.low, &frame.resp_status);
        break;

    case SERVER_OP_GROW_AREA:
        /* Grow area */
        {
            uint8_t area_id = (request_len < 0x1C) ?
                              (frame.uid2.high >> 8) : (frame.uid3.low >> 8);
            AREA_$GROW_TO(frame.uid3.high & 0xFF, frame.uid2.high,
                          ((frame.uid3.low & 0xFF) << 16) | area_id, &frame.resp_status);
        }
        break;

    default:
        /* Unknown opcode */
        frame.resp_opcode = 0x03;
        frame.resp_status = 0x000F0003;  /* file_$bad_reply */
        break;
    }

send_response:
    /* Send response back to requester */
    /* (Response sending code would go here - involves SOCK operations) */

done:
    /* Release lock if still held */
    if (lock_held < 0) {
        ML_$EXCLUSION_STOP(&REM_FILE_$SOCK_LOCK);
    }
}
