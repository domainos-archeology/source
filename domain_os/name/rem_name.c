/*
 * REM_NAME - Remote Naming Functions
 *
 * Functions to handle distributed naming operations across Apollo network nodes.
 * These functions communicate with remote naming servers to resolve names,
 * get directory information, and manage network-wide naming.
 *
 * Data structures at 0xE7DBB8 (rem_name_$data_base):
 *   +0x28: REM_NAME_$TIME_HEARD_FROM_SERVER
 *   +0x3C: REM_NAME_$HEARD_FROM_SERVER (boolean)
 */

#include "name/name_internal.h"
#include "misc/string.h"

/* TIME subsystem reference */
extern uint32_t TIME_$CLOCKH;  /* 0x00e2b0d4 - high word of system clock */

/* REM_NAME data area */
typedef struct {
    uint8_t  reserved[0x28];
    uint32_t time_heard_from_server;  /* +0x28 */
    uint8_t  reserved2[0x10];
    boolean  heard_from_server;       /* +0x3C */
} rem_name_data_t;

extern rem_name_data_t rem_name_$data;  /* 0xE7DBB8 */

/* Internal communication function */
extern boolean FUN_00e4a4c8(void *param1, void *param2, void *request,
                            int16_t req_size, int16_t param5, int16_t opcode,
                            void *response, int16_t resp_size,
                            void *out_param, status_$t *status_ret);

/* Additional status codes */
#define status_$naming_helper_sent_packets_with_errors  0x000e001c

/*
 * REM_NAME_$REGISTER_SERVER - Register that we've heard from a name server
 *
 * Updates the last-heard-from timestamp and sets the server contacted flag.
 *
 * Original address: 0x00e4a4ae
 * Original size: 26 bytes
 */
void REM_NAME_$REGISTER_SERVER(void)
{
    rem_name_$data.time_heard_from_server = TIME_$CLOCKH;
    rem_name_$data.heard_from_server = true;
}

/*
 * REM_NAME_$GET_ENTRY_BY_NAME - Look up a directory entry by name
 *
 * Queries a remote naming server to resolve a name within a directory.
 *
 * Parameters:
 *   param_1    - Network communication handle
 *   param_2    - Additional comm parameter
 *   dir_uid    - UID of the directory to search
 *   name       - Name to look up
 *   name_len   - Length of name (max 32)
 *   entry_ret  - Output: entry information
 *   status_ret - Output: status code
 *
 * Original address: 0x00e4a588
 * Original size: 264 bytes
 */
void REM_NAME_$GET_ENTRY_BY_NAME(void *param_1, void *param_2, uid_t *dir_uid,
                                  char *name, uint16_t name_len,
                                  void *entry_ret, status_$t *status_ret)
{
    /* Request structure */
    struct {
        uint32_t opcode;       /* 0x10001 */
        uid_t    dir_uid;      /* Directory to search */
        uint16_t flags;        /* 1 */
        uint8_t  reserved[0x24];
        uint16_t name_len;     /* Length of name */
        char     name[32];     /* Name to look up */
    } request;

    /* Response structure (partial - actual is larger) */
    uint8_t response[0x16a];
    uint8_t out_param[8];
    uint16_t *entry = (uint16_t *)entry_ret;

    if (name_len > 32) {
        *status_ret = status_$naming_invalid_pathname;
        return;
    }

    /* Build request */
    request.opcode = 0x10001;
    request.dir_uid.high = dir_uid->high;
    request.dir_uid.low = dir_uid->low;
    request.flags = 1;
    request.name_len = name_len;

    /* Copy name */
    for (uint16_t i = 0; i < name_len; i++) {
        request.name[i] = name[i];
    }

    /* Make remote call */
    if (FUN_00e4a4c8(param_1, param_2, &request, 0x34 + name_len, 0, 2,
                     response, 0x16a, out_param, status_ret)) {
        /* Parse response and fill entry_ret */
        /* The response parsing is complex and depends on entry type */
        /* TODO: Complete response parsing based on Ghidra analysis */
        uint16_t entry_type = *(uint16_t *)(response + 0x12);

        if (entry_type == 1) {
            entry[0] = 1;
            /* Copy additional data */
        } else if (entry_type == 2) {
            entry[0] = 3;
            /* Copy UID_$NIL and zero */
        } else {
            *status_ret = status_$naming_name_not_found;
            entry[0] = 0;
        }
    }
}

/*
 * REM_NAME_$GET_INFO - Get information about a named object
 *
 * Original address: 0x00e4a690
 * Original size: 146 bytes
 */
void REM_NAME_$GET_INFO(void *param_1, void *param_2, uid_t *uid,
                        void *info_ret, status_$t *status_ret)
{
    struct {
        uint32_t opcode;   /* 0x10019 */
        uid_t    uid;
        uint16_t flags;    /* 1 */
    } request;

    uint8_t response[0x16a];
    uint16_t out_param[3];

    request.opcode = 0x10019;
    request.uid.high = uid->high;
    request.uid.low = uid->low;
    request.flags = 1;

    if (FUN_00e4a4c8(param_1, param_2, &request, 0x32, 0, 0x1a,
                     response, 0x16a, out_param, status_ret)) {
        if (out_param[0] == 0x28) {
            /* Copy info data - 22 bytes */
            memcpy(info_ret, response + 0x12, 22);
        } else {
            *status_ret = status_$naming_helper_sent_packets_with_errors;
        }
    }
}

/*
 * REM_NAME_$LOCATE_SERVER - Locate a naming server
 *
 * Original address: 0x00e4a722
 * Original size: 222 bytes
 */
void REM_NAME_$LOCATE_SERVER(void *param_1, void *param_2, uid_t *uid,
                              void *server_ret, status_$t *status_ret)
{
    /* TODO: Implement based on Ghidra analysis */
    *status_ret = status_$naming_name_not_found;
}

/*
 * REM_NAME_$GET_ENTRY_BY_NODE_ID - Look up entry by node ID
 *
 * Original address: 0x00e4a800
 * Original size: 204 bytes
 */
void REM_NAME_$GET_ENTRY_BY_NODE_ID(void *param_1, void *param_2,
                                     uint32_t node_id, void *entry_ret,
                                     status_$t *status_ret)
{
    /* TODO: Implement based on Ghidra analysis */
    *status_ret = status_$naming_name_not_found;
}

/*
 * REM_NAME_$GET_ENTRY_BY_UID - Look up entry by UID
 *
 * Original address: 0x00e4a8cc
 * Original size: 184 bytes
 */
void REM_NAME_$GET_ENTRY_BY_UID(void *param_1, void *param_2, uid_t *uid,
                                 void *entry_ret, status_$t *status_ret)
{
    /* TODO: Implement based on Ghidra analysis */
    *status_ret = status_$naming_name_not_found;
}

/*
 * REM_NAME_$READ_DIR - Read directory entries
 *
 * Original address: 0x00e4a984
 * Original size: 448 bytes
 */
void REM_NAME_$READ_DIR(void *param_1, void *param_2, uid_t *dir_uid,
                        void *entries_ret, int16_t *count_ret,
                        status_$t *status_ret)
{
    /* TODO: Implement based on Ghidra analysis */
    *count_ret = 0;
    *status_ret = status_$naming_name_not_found;
}

/*
 * REM_NAME_$READ_REP - Read replication information
 *
 * Original address: 0x00e4ab44
 * Original size: 232 bytes
 */
void REM_NAME_$READ_REP(void *param_1, void *param_2, uid_t *uid,
                        void *rep_ret, status_$t *status_ret)
{
    /* TODO: Implement based on Ghidra analysis */
    *status_ret = status_$naming_name_not_found;
}

/*
 * REM_NAME_$DIR_READU - Read directory entry (unsigned version)
 *
 * Original address: 0x00e4ac2c
 * Original size: 236 bytes
 */
void REM_NAME_$DIR_READU(void *param_1, void *param_2, uid_t *dir_uid,
                         void *entry_ret, status_$t *status_ret)
{
    /* TODO: Implement based on Ghidra analysis */
    *status_ret = status_$naming_name_not_found;
}

/*
 * REM_NAME_$GET_ENTRY - Get a directory entry
 *
 * Original address: 0x00e4ad18
 * Original size: 190 bytes
 */
void REM_NAME_$GET_ENTRY(void *param_1, void *param_2, uid_t *dir_uid,
                         int16_t index, void *entry_ret, status_$t *status_ret)
{
    /* TODO: Implement based on Ghidra analysis */
    *status_ret = status_$naming_name_not_found;
}

/*
 * REM_NAME_$FIND_NETWORK - Find a network by name
 *
 * Original address: 0x00e4add6
 * Original size: 174 bytes
 */
void REM_NAME_$FIND_NETWORK(void *param_1, void *param_2, char *name,
                            uint16_t name_len, void *net_ret,
                            status_$t *status_ret)
{
    /* TODO: Implement based on Ghidra analysis */
    *status_ret = status_$naming_name_not_found;
}

/*
 * REM_NAME_$FIND_UID - Find an object by UID
 *
 * Original address: 0x00e4ae84
 * Original size: 178 bytes
 */
void REM_NAME_$FIND_UID(void *param_1, void *param_2, uid_t *uid,
                        void *result_ret, status_$t *status_ret)
{
    /* TODO: Implement based on Ghidra analysis */
    *status_ret = status_$naming_name_not_found;
}
