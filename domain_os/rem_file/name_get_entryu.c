/*
 * REM_FILE_$NAME_GET_ENTRYU - Get directory entry by name from remote server
 *
 * Looks up a directory entry by name on a remote file server and returns
 * the entry information including UID and type.
 *
 * Original address: 0x00E6209A
 * Size: 286 bytes
 */

#include "rem_file/rem_file_internal.h"

/* proc_priv_table is indexed by PROC1_$CURRENT to check process privileges */
extern int16_t proc_priv_table[];  /* At 0xe7dacc */

/*
 * Name get entry request structure
 */
typedef struct {
    uint8_t magic;              /* 0x80 */
    uint8_t opcode;             /* 0x1C = Name get entry */
    uid_t dir_uid;              /* Directory UID (8 bytes) */
    char name[32];              /* Entry name (padded with spaces) */
    uint16_t name_len;          /* Name length */
    uint16_t flags;             /* Flags (value 3) */
    int8_t priv_flag;           /* Privilege flag */
    uint8_t padding;
    uint8_t re_sids[36];        /* RE SIDs from ACL_$GET_RE_SIDS */
    uid_t proj_list[8];         /* Project list */
    uint8_t proj_extra[2];      /* Extra project data */
    uint32_t zero1;             /* Zero padding */
    uint32_t zero2;             /* Zero padding */
} rem_file_name_get_entry_req_t;

/*
 * Name get entry response structure
 */
typedef struct {
    uint8_t padding[REM_FILE_RESPONSE_BUF_SIZE - 0xE4];
    uint16_t entry_type;        /* Entry type */
    uint8_t padding2[0x1C];
    uid_t entry_uid;            /* Entry UID (8 bytes) */
    uint32_t extra_info;        /* Extra info (only if received_len != 0x22) */
} rem_file_name_get_entry_resp_t;

/*
 * Entry result structure
 */
typedef struct {
    uint16_t entry_type;        /* Entry type */
    uid_t entry_uid;            /* Entry UID */
    uint32_t extra_info;        /* Extra info */
} rem_file_entry_result_t;

void REM_FILE_$NAME_GET_ENTRYU(void *addr_info, uid_t *dir_uid,
                                char *name, uint16_t name_len,
                                void *result_ptr, status_$t *status)
{
    rem_file_entry_result_t *result_out = (rem_file_entry_result_t *)result_ptr;
    rem_file_name_get_entry_req_t request;
    uint8_t response[REM_FILE_RESPONSE_BUF_SIZE];
    uint16_t received_len;
    uint16_t packet_id;
    uint16_t zero = 0;
    int i;
    uint8_t re_sids[40];
    uint8_t sids_out[36];
    uid_t proj_list[8];
    uint8_t proj_out[2];
    static const uid_t default_proj = {0};  /* Default project UID */

    /* Initialize name field with spaces */
    request.name[0] = ' ';
    request.name[1] = ' ';
    request.name[2] = ' ';
    request.name[3] = ' ';
    /* Note: only first 4 bytes explicitly initialized, rest filled by copy loop */

    /* Build request */
    request.magic = 0x80;
    request.opcode = 0x1C;  /* NAME_GET_ENTRY opcode */
    request.dir_uid = *dir_uid;

    /* Copy name (up to 32 bytes) */
    for (i = 0; i < 32 && i < name_len; i++) {
        request.name[i] = name[i];
    }

    request.name_len = name_len;
    request.flags = 3;

    /* Set privilege flag based on process privilege level */
    request.priv_flag = (proc_priv_table[PROC1_$CURRENT] > 0) ? -1 : 0;

    /* Get RE SIDs */
    ACL_$GET_RE_SIDS(re_sids, sids_out, status);
    if (*status != status_$ok) {
        return;
    }

    /* Get project list */
    ACL_$GET_PROJ_LIST(proj_list, (void *)&default_proj, proj_out, status);
    if (*status != status_$ok) {
        return;
    }

    /* Copy SIDs and project data into request */
    for (i = 0; i < 36; i++) {
        request.re_sids[i] = sids_out[i];
    }

    for (i = 0; i < 8; i++) {
        request.proj_list[i] = proj_list[i];
    }

    request.proj_extra[0] = proj_out[0];
    request.proj_extra[1] = proj_out[1];
    request.zero1 = 0;
    request.zero2 = 0;

    /* Send request */
    REM_FILE_$SEND_REQUEST(addr_info, &request, 0xA2,
                           &zero, 0,
                           response, REM_FILE_RESPONSE_BUF_SIZE,
                           &received_len, &zero, 0,
                           (int16_t *)&zero, &packet_id,
                           status);

    if (*status != status_$ok) {
        return;
    }

    /* Extract result from response */
    rem_file_name_get_entry_resp_t *resp = (rem_file_name_get_entry_resp_t *)response;
    result_out->entry_type = resp->entry_type;
    result_out->entry_uid = resp->entry_uid;

    /* Extra info only present in longer responses */
    if (received_len == 0x22) {
        result_out->extra_info = 0;
    } else {
        result_out->extra_info = resp->extra_info;
    }
}
