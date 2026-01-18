/*
 * REM_NAME - Remote Naming Functions
 *
 * Functions to handle distributed naming operations across Apollo network nodes.
 * These functions communicate with remote naming servers to resolve names,
 * get directory information, and manage network-wide naming.
 *
 * Global data structure at 0xE7DBB8 (rem_name_$data):
 *   +0x00: Configuration data copied to request packets (30 bytes)
 *   +0x1E: Reserved/padding
 *   +0x20: Server timeout value
 *   +0x28: Time last heard from server
 *   +0x2C: Last status code
 *   +0x30: Current node ID
 *   +0x34: Current network ID
 *   +0x38: Packet sequence number for PKT_$SAR_INTERNET
 *   +0x3A: Retry counter
 *   +0x3C: Boolean - heard from server
 *
 * Original addresses: 0x00e4a408 - 0x00e4af24
 */

#include "name/name_internal.h"
#include "misc/string.h"

/* TIME subsystem reference */
extern uint32_t TIME_$CLOCKH;  /* 0x00e2b0d4 - high word of system clock */

/* NODE subsystem reference */
extern uint32_t NODE_$ME;  /* 0x00e245a4 - this node's ID */

/* UID_$NIL reference */
extern uid_t UID_$NIL;  /* 0x00e1737c */

/* NETBUF functions */
extern void NETBUF_$GET_HDR(void *out1, void *out2);
extern void NETBUF_$RTN_HDR(void *param);

/* OS data copy function */
extern void OS_$DATA_COPY(void *src, void *dst, uint32_t len);

/* PKT SAR Internet function - sends/receives network packets */
extern void PKT_$SAR_INTERNET(uint32_t net, uint32_t node, int16_t protocol,
                               void *config, uint16_t seq_num,
                               void *request, uint16_t req_size,
                               void *callback, int16_t callback_param,
                               void *out_buf, void *response, uint16_t resp_size,
                               int16_t *resp_len_ret, void *out1, int16_t out2,
                               void *out3, status_$t *status_ret);

/* Callback data for PKT_$SAR_INTERNET - at 0x00e4a584 */
static const uint8_t pkt_callback_data[] = { 0 };

/*
 * REM_NAME data area - complete structure at 0xE7DBB8
 */
typedef struct {
    uint16_t config[15];             /* +0x00: Config copied to request packets */
    uint16_t reserved1;              /* +0x1E: Reserved */
    uint32_t server_timeout;         /* +0x20: Timeout for server contact */
    uint32_t reserved2;              /* +0x24: Reserved */
    uint32_t time_heard_from_server; /* +0x28: TIME_$CLOCKH when last heard */
    status_$t last_status;           /* +0x2C: Last status code */
    uint32_t curr_node;              /* +0x30: Current name server node */
    uint32_t curr_net;               /* +0x34: Current name server network */
    uint16_t pkt_seq_num;            /* +0x38: Packet sequence number */
    uint16_t retry_count;            /* +0x3A: Server locate retry counter */
    int8_t   heard_from_server;      /* +0x3C: True if contacted server */
} rem_name_data_t;

extern rem_name_data_t rem_name_$data;  /* 0xE7DBB8 */

/* Event count for server local check - 0x00e28dd8 */
typedef struct {
    uint32_t value;  /* Pointer to event count data */
} ec2_eventcount_t;

extern ec2_eventcount_t ec2_$eventcount;  /* 0x00e28dd8 */

/*
 * Additional status codes for remote naming
 */
#define status_$naming_helper_sent_packets_with_errors      0x000e001c
#define status_$naming_directory_must_be_root               0x000e001e
#define status_$naming_last_entry_in_replicated_root_returned 0x000e0019
#define status_$naming_name_server_helper_is_shutdown       0x000e001a

/*
 * Request opcodes for remote naming operations
 */
#define REM_NAME_OP_GET_ENTRY_BY_NAME   0x10001
#define REM_NAME_OP_READ_DIR            0x1000b
#define REM_NAME_OP_READ_REP            0x1000d
#define REM_NAME_OP_GET_ENTRY_BY_NODE   0x10017
#define REM_NAME_OP_GET_INFO            0x10019
#define REM_NAME_OP_GET_ENTRY_BY_UID    0x1001b
#define REM_NAME_OP_LOCATE_SERVER       0x1001d

/*
 * Response type codes
 */
#define ENTRY_TYPE_NORMAL   1
#define ENTRY_TYPE_LINK     2
#define ENTRY_TYPE_LINK_ALT 3

/*
 * Entry structure size for directory reads
 */
#define DIR_ENTRY_SIZE      0x30
#define REP_ENTRY_SIZE      0x12

/* Forward declarations */
static boolean rem_name_$send_request(uint32_t net, uint32_t node, void *request,
                                       int16_t req_size, int16_t flags, int16_t opcode,
                                       void *response, int16_t resp_size,
                                       int16_t *resp_len_ret, status_$t *status_ret);
static void LOCATE_SERVER(uint32_t *node_ret, uint32_t *net_ret, status_$t *status_ret);

/*
 * REM_NAME_SERVER_LOCAL - Check if the naming server is local
 *
 * Examines a flag in the event count structure to determine if
 * the naming server is running on the local node.
 *
 * Returns:
 *   0xFF (true) if local, 0 (false) if remote
 *
 * Original address: 0x00e4a408
 * Original size: 24 bytes
 */
boolean REM_NAME_SERVER_LOCAL(void)
{
    /* Check bit 13 (0x2000) of the value at offset 0x16 in the event count */
    uint16_t *ptr = (uint16_t *)(ec2_$eventcount.value + 0x16);
    return ((*ptr & 0x2000) != 0) ? true : false;
}

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
 * rem_name_$send_request - Send a remote naming request (internal)
 *
 * Core RPC mechanism for remote naming operations. Copies configuration
 * data, adds flags, and calls PKT_$SAR_INTERNET to send the request.
 *
 * Parameters:
 *   net        - Network ID (0 for local)
 *   node       - Node ID to contact
 *   request    - Request data buffer
 *   req_size   - Size of request data
 *   flags      - Additional flags to OR into config[0]
 *   opcode     - Operation code for validation
 *   response   - Response buffer
 *   resp_size  - Size of response buffer
 *   resp_len_ret - Output: actual response length
 *   status_ret - Output: status code
 *
 * Returns:
 *   0xFF (true) on success with valid response, 0 (false) otherwise
 *
 * Original address: 0x00e4a4c8
 * Original size: 188 bytes
 */
static boolean rem_name_$send_request(uint32_t net, uint32_t node, void *request,
                                       int16_t req_size, int16_t flags, int16_t opcode,
                                       void *response, int16_t resp_size,
                                       int16_t *resp_len_ret, status_$t *status_ret)
{
    uint16_t config[16];
    uint8_t out_buf[40];
    uint8_t out1[4];
    uint8_t out2[2];
    status_$t internal_status;
    int i;

    /* Copy configuration data from global structure */
    for (i = 0; i < 15; i++) {
        config[i] = rem_name_$data.config[i];
    }
    config[15] = rem_name_$data.config[14];  /* Copy last word */

    /* OR in additional flags */
    config[0] |= (uint16_t)flags;

    /* Send the packet */
    PKT_$SAR_INTERNET(net, node, 10, config, rem_name_$data.pkt_seq_num,
                      request, req_size, (void *)pkt_callback_data, 0,
                      out_buf, response, resp_size, resp_len_ret,
                      out1, 0, out2, &internal_status);

    if (internal_status != status_$ok) {
        *status_ret = internal_status;
        rem_name_$data.last_status = internal_status;
        return false;
    }

    /* Validate response */
    if (*resp_len_ret < 0x12) {
        /* Response too short */
        *status_ret = *(status_$t *)((uint8_t *)response + 0x0e);
        rem_name_$data.last_status = *status_ret;
        return false;
    }

    /* Check opcode matches */
    if ((int16_t)opcode != *(int16_t *)((uint8_t *)response + 0x02)) {
        *status_ret = *(status_$t *)((uint8_t *)response + 0x0e);
        rem_name_$data.last_status = *status_ret;
        return false;
    }

    /* Check for error in response */
    if (*(status_$t *)((uint8_t *)response + 0x0e) != status_$ok) {
        *status_ret = *(status_$t *)((uint8_t *)response + 0x0e);
        rem_name_$data.last_status = *status_ret;
        return false;
    }

    *status_ret = status_$ok;
    return true;
}

/*
 * LOCATE_SERVER - Internal function to locate a naming server
 *
 * If we've recently heard from a server, uses cached location.
 * Otherwise attempts to locate a server, with retry limiting.
 *
 * Parameters:
 *   node_ret   - Output: node ID of server
 *   net_ret    - Output: network ID of server
 *   status_ret - Output: status code
 *
 * Original address: 0x00e4a420
 * Original size: 142 bytes
 */
static void LOCATE_SERVER(uint32_t *node_ret, uint32_t *net_ret, status_$t *status_ret)
{
    uint32_t time_diff;

    if (rem_name_$data.heard_from_server) {
        /* Check if contact is still recent */
        time_diff = rem_name_$data.time_heard_from_server - TIME_$CLOCKH;
        if ((int32_t)time_diff < 0) {
            time_diff = -time_diff;
        }

        if (time_diff > rem_name_$data.server_timeout) {
            /* Too long since last contact - need to relocate */
            rem_name_$data.heard_from_server = false;
            rem_name_$data.last_status = status_$naming_directory_must_be_root;
            *status_ret = status_$naming_directory_must_be_root;
            return;
        }

        /* Use cached server - call REM_NAME_$LOCATE_SERVER */
        REM_NAME_$LOCATE_SERVER(node_ret, net_ret, status_ret);
    } else {
        /* Haven't heard from server recently */
        if (rem_name_$data.retry_count > 3) {
            /* Too many retries */
            *status_ret = status_$naming_directory_must_be_root;
            return;
        }

        rem_name_$data.retry_count++;
        REM_NAME_$LOCATE_SERVER(node_ret, net_ret, status_ret);

        if (*status_ret == status_$ok) {
            rem_name_$data.heard_from_server = true;
            rem_name_$data.time_heard_from_server = TIME_$CLOCKH;
        }
    }
}

/*
 * REM_NAME_$GET_ENTRY_BY_NAME - Look up a directory entry by name
 *
 * Queries a remote naming server to resolve a name within a directory.
 *
 * Parameters:
 *   net        - Network ID
 *   node       - Node ID
 *   dir_uid    - UID of the directory to search
 *   name       - Name to look up
 *   name_len   - Length of name (max 32)
 *   entry_ret  - Output: entry information
 *   status_ret - Output: status code
 *
 * Original address: 0x00e4a588
 * Original size: 264 bytes
 */
void REM_NAME_$GET_ENTRY_BY_NAME(uint32_t net, uint32_t node, uid_t *dir_uid,
                                  char *name, uint16_t name_len,
                                  void *entry_ret, status_$t *status_ret)
{
    struct {
        uint32_t opcode;
        uid_t    dir_uid;
        uint16_t flags;
        uint8_t  reserved[0x24];
        uint16_t name_len;
        char     name[32];
    } request;

    uint8_t response[0x16a];
    uint8_t out_param[8];
    int16_t resp_len;
    uint16_t *entry = (uint16_t *)entry_ret;
    int16_t entry_type;
    int i;

    if (name_len > 32) {
        *status_ret = status_$naming_invalid_pathname;
        return;
    }

    /* Build request */
    request.opcode = REM_NAME_OP_GET_ENTRY_BY_NAME;
    request.dir_uid.high = dir_uid->high;
    request.dir_uid.low = dir_uid->low;
    request.flags = 1;
    request.name_len = name_len;

    /* Copy name */
    for (i = 0; i < name_len; i++) {
        request.name[i] = name[i];
    }

    /* Make remote call */
    if (!rem_name_$send_request(net, node, &request, 0x34 + name_len, 0, 2,
                                 response, 0x16a, &resp_len, status_ret)) {
        return;
    }

    /* Parse response - copy name length and name (32 bytes at offset 4) */
    entry[1] = *(uint16_t *)(response + 0x12 + 2);  /* name length */
    for (i = 0; i < 32; i++) {
        ((uint8_t *)entry)[4 + i] = response[0x12 + 4 + i];
    }

    entry_type = *(int16_t *)(response + 0x12);

    if (entry_type == ENTRY_TYPE_NORMAL) {
        entry[0] = 1;
        /* Copy additional 12 bytes of data (UID + type info) */
        for (i = 0; i < 12; i++) {
            ((uint8_t *)entry)[0x24 + i] = response[0x12 + 0x24 + i];
        }
    } else if (entry_type == ENTRY_TYPE_LINK) {
        entry[0] = ENTRY_TYPE_LINK_ALT;
        /* Set to UID_$NIL */
        *(uint32_t *)(entry + 0x12) = UID_$NIL.high;
        *(uint32_t *)(entry + 0x14) = UID_$NIL.low;
        *(uint32_t *)(entry + 0x16) = 0;
    } else {
        *status_ret = status_$naming_name_not_found;
        entry[0] = 0;
    }
}

/*
 * REM_NAME_$GET_INFO - Get information about a named object
 *
 * Queries a remote naming server for detailed info about an object.
 *
 * Original address: 0x00e4a690
 * Original size: 146 bytes
 */
void REM_NAME_$GET_INFO(uint32_t net, uint32_t node, uid_t *uid,
                        void *info_ret, status_$t *status_ret)
{
    struct {
        uint32_t opcode;
        uid_t    uid;
        uint16_t flags;
    } request;

    uint8_t response[0x16a];
    int16_t resp_len;
    uint32_t *info = (uint32_t *)info_ret;
    int i;

    request.opcode = REM_NAME_OP_GET_INFO;
    request.uid.high = uid->high;
    request.uid.low = uid->low;
    request.flags = 1;

    if (!rem_name_$send_request(net, node, &request, 0x32, 0, 0x1a,
                                 response, 0x16a, &resp_len, status_ret)) {
        return;
    }

    if (resp_len >= 0x28) {
        /* Copy 22 bytes (5 longs + 1 word) of info data */
        for (i = 0; i < 5; i++) {
            info[i] = *(uint32_t *)(response + 0x12 + i * 4);
        }
        *(uint16_t *)(info + 5) = *(uint16_t *)(response + 0x12 + 20);
    } else {
        *status_ret = status_$naming_helper_sent_packets_with_errors;
    }
}

/*
 * REM_NAME_$LOCATE_SERVER - Locate a naming server
 *
 * First tries the local node, then broadcasts to find a server.
 * Returns the node/network ID of the located server.
 *
 * Parameters:
 *   node_ret   - Output: server node ID
 *   net_ret    - Output: server network ID (0 if local)
 *   status_ret - Output: status code
 *
 * Original address: 0x00e4a722
 * Original size: 222 bytes
 */
void REM_NAME_$LOCATE_SERVER(uint32_t *node_ret, uint32_t *net_ret, status_$t *status_ret)
{
    struct {
        uint32_t opcode;
        uint8_t  reserved[8];
        uint16_t flags;
    } request;

    uint8_t response[0x16a];
    int16_t resp_len;
    uint32_t located_node;

    request.opcode = REM_NAME_OP_LOCATE_SERVER;
    request.flags = 1;

    /* First check if server is local */
    if (REM_NAME_SERVER_LOCAL()) {
        /* Try local node first */
        if (rem_name_$send_request(0, NODE_$ME, &request, 0x32, 0, 0x1e,
                                    response, 0x16a, &resp_len, status_ret)) {
            if (resp_len >= 0x28) {
                goto found_server;
            }
        }
    }

    /* Broadcast to find a server (node 0xFFFFFF = broadcast) */
    if (!rem_name_$send_request(0, 0xFFFFFF, &request, 0x32, 0x80, 0x1e,
                                 response, 0x16a, &resp_len, status_ret)) {
        return;
    }

    if (resp_len < 0x28) {
        *status_ret = status_$naming_helper_sent_packets_with_errors;
        rem_name_$data.last_status = *status_ret;
        return;
    }

found_server:
    /* Extract server node from response (20-bit node ID) */
    located_node = *(uint32_t *)(response + 0x18) & 0xFFFFF;

    *net_ret = 0;
    *node_ret = located_node;
    rem_name_$data.curr_node = located_node;
    rem_name_$data.curr_net = 0;
    rem_name_$data.last_status = *status_ret;
}

/*
 * REM_NAME_$GET_ENTRY_BY_NODE_ID - Look up entry by node ID
 *
 * Queries a remote naming server to find an entry by its node ID.
 *
 * Original address: 0x00e4a800
 * Original size: 204 bytes
 */
void REM_NAME_$GET_ENTRY_BY_NODE_ID(uint32_t net, uint32_t node, uid_t *dir_uid,
                                     uint32_t target_node, void *entry_ret,
                                     status_$t *status_ret)
{
    struct {
        uint32_t opcode;
        uid_t    dir_uid;
        uint16_t flags;
        uint8_t  reserved[0x24];
        uint16_t node_high;      /* 0x800 | (node >> 16) | 0x1e00 */
        uint16_t node_low;       /* node & 0xFFFF */
    } request;

    uint8_t response[0x16a];
    int16_t resp_len;
    uint16_t *entry = (uint16_t *)entry_ret;
    int16_t entry_type;
    int i;

    request.opcode = REM_NAME_OP_GET_ENTRY_BY_NODE;
    request.dir_uid.high = dir_uid->high;
    request.dir_uid.low = dir_uid->low;
    request.flags = 1;
    request.node_high = 0x800 | ((target_node >> 16) & 0xFF) | 0x1e00;
    request.node_low = (uint16_t)target_node;

    if (!rem_name_$send_request(net, node, &request, 0x38, 0, 0x18,
                                 response, 0x16a, &resp_len, status_ret)) {
        return;
    }

    /* Parse response */
    entry[1] = *(uint16_t *)(response + 0x12 + 2);  /* name length */
    for (i = 0; i < 32; i++) {
        ((uint8_t *)entry)[4 + i] = response[0x12 + 4 + i];
    }

    entry_type = *(int16_t *)(response + 0x12);

    if (entry_type == ENTRY_TYPE_NORMAL) {
        entry[0] = 1;
        for (i = 0; i < 12; i++) {
            ((uint8_t *)entry)[0x24 + i] = response[0x12 + 0x24 + i];
        }
    } else {
        *status_ret = status_$naming_name_not_found;
        entry[0] = 0;
    }
}

/*
 * REM_NAME_$GET_ENTRY_BY_UID - Look up entry by UID
 *
 * Queries a remote naming server to find an entry by its UID.
 *
 * Original address: 0x00e4a8cc
 * Original size: 184 bytes
 */
void REM_NAME_$GET_ENTRY_BY_UID(uint32_t net, uint32_t node, uid_t *dir_uid,
                                 uid_t *target_uid, void *entry_ret,
                                 status_$t *status_ret)
{
    struct {
        uint32_t opcode;
        uid_t    dir_uid;
        uint16_t flags;
        uint8_t  reserved[0x24];
        uid_t    target_uid;
    } request;

    uint8_t response[0x16a];
    int16_t resp_len;
    uint16_t *entry = (uint16_t *)entry_ret;
    int16_t entry_type;
    int i;

    request.opcode = REM_NAME_OP_GET_ENTRY_BY_UID;
    request.dir_uid.high = dir_uid->high;
    request.dir_uid.low = dir_uid->low;
    request.flags = 1;
    request.target_uid.high = target_uid->high;
    request.target_uid.low = target_uid->low;

    if (!rem_name_$send_request(net, node, &request, 0x3a, 0, 0x1c,
                                 response, 0x16a, &resp_len, status_ret)) {
        return;
    }

    /* Parse response */
    entry[1] = *(uint16_t *)(response + 0x12 + 2);
    for (i = 0; i < 32; i++) {
        ((uint8_t *)entry)[4 + i] = response[0x12 + 4 + i];
    }

    entry_type = *(int16_t *)(response + 0x12);

    if (entry_type == ENTRY_TYPE_NORMAL) {
        entry[0] = 1;
        for (i = 0; i < 12; i++) {
            ((uint8_t *)entry)[0x24 + i] = response[0x12 + 0x24 + i];
        }
    } else {
        *status_ret = status_$naming_name_not_found;
        entry[0] = 0;
    }
}

/*
 * REM_NAME_$READ_DIR - Read directory entries
 *
 * Reads multiple directory entries from a remote naming server.
 * Uses NETBUF for large response handling.
 *
 * Original address: 0x00e4a984
 * Original size: 448 bytes
 */
void REM_NAME_$READ_DIR(uint32_t net, uint32_t node, uid_t *dir_uid,
                        uint32_t start_index, void *entries_ret,
                        uint16_t max_entries, uint16_t *count_ret,
                        status_$t *status_ret)
{
    struct {
        uint32_t opcode;
        uid_t    dir_uid;
        uint16_t flags;
        uint8_t  reserved[0x24];
        uint32_t start_index;
    } request;

    uint8_t netbuf_hdr[4];
    uint8_t *response;
    uint32_t response_ptr;
    int16_t resp_len;
    uint8_t out_param[6];
    uint16_t entry_count;
    uint16_t i;
    uint8_t *src;
    uint8_t *dst;
    int16_t entry_type;
    uint16_t name_len;

    /* Get network buffer for large response */
    NETBUF_$GET_HDR(netbuf_hdr, &response_ptr);
    response = (uint8_t *)response_ptr;

    *count_ret = 0;

    request.opcode = REM_NAME_OP_READ_DIR;
    request.dir_uid.high = dir_uid->high;
    request.dir_uid.low = dir_uid->low;
    request.flags = 1;
    request.start_index = start_index;

    if (!rem_name_$send_request(net, node, &request, 0x36, 0, 0x0c,
                                 response, 0x200, &resp_len, status_ret)) {
        if (*status_ret != status_$naming_name_server_helper_is_shutdown) {
            NETBUF_$RTN_HDR(&response_ptr);
            return;
        }
        /* For shutdown status, still process any entries we got */
        if (max_entries < *(uint16_t *)(response + 0x16)) {
            *status_ret = status_$ok;
        }
    }

    /* Parse directory entries */
    entry_count = *(uint16_t *)(response + 0x16);
    if (entry_count == 0) {
        NETBUF_$RTN_HDR(&response_ptr);
        return;
    }

    src = response + 0x18;

    for (i = 0; i < entry_count && *count_ret < max_entries; i++) {
        (*count_ret)++;
        dst = (uint8_t *)entries_ret + (*count_ret - 1) * DIR_ENTRY_SIZE;

        /* Copy entry type */
        OS_$DATA_COPY(src, dst - 0x2e + DIR_ENTRY_SIZE, 2);
        entry_type = *(int16_t *)src;
        src += 2;

        /* Copy name length */
        OS_$DATA_COPY(src, dst + 2, 2);
        name_len = *(uint16_t *)src;
        src += 2;

        /* Copy name */
        OS_$DATA_COPY(src, dst + 4, name_len);

        /* Pad name with spaces to 32 chars */
        if (name_len < 32) {
            uint16_t j;
            for (j = name_len; j < 32; j++) {
                dst[4 + j] = ' ';
            }
        }

        if (entry_type == ENTRY_TYPE_NORMAL) {
            *(uint16_t *)dst = 1;
            src += name_len;
            /* Copy UID (8 bytes) and type info (4 bytes) */
            OS_$DATA_COPY(src, dst + 0x24, 8);
            src += 8;
            OS_$DATA_COPY(src, dst + 0x2c, 4);
            src += 4;
        } else if (entry_type == ENTRY_TYPE_LINK) {
            *(uint16_t *)dst = ENTRY_TYPE_LINK_ALT;
            src += name_len;
        } else {
            /* Unknown entry type - back out */
            (*count_ret)--;
            break;
        }
    }

    NETBUF_$RTN_HDR(&response_ptr);
}

/*
 * REM_NAME_$READ_REP - Read replication information
 *
 * Reads replica location entries from a remote naming server.
 *
 * Original address: 0x00e4ab44
 * Original size: 232 bytes
 */
void REM_NAME_$READ_REP(uint32_t net, uint32_t node, uid_t *dir_uid,
                        uint32_t start_index, void *rep_ret,
                        uint16_t max_entries, uint16_t *count_ret,
                        status_$t *status_ret)
{
    struct {
        uint32_t opcode;
        uid_t    dir_uid;
        uint16_t flags;
        uint8_t  reserved[0x24];
        uint32_t start_index;
    } request;

    uint8_t netbuf_hdr[4];
    uint8_t *response;
    uint32_t response_ptr;
    int16_t resp_len;
    uint8_t out_param[6];
    uint16_t entry_count;
    uint16_t i;
    uint8_t *src;
    uint32_t *dst;

    NETBUF_$GET_HDR(netbuf_hdr, &response_ptr);
    response = (uint8_t *)response_ptr;

    *count_ret = 0;

    request.opcode = REM_NAME_OP_READ_REP;
    request.dir_uid.high = dir_uid->high;
    request.dir_uid.low = dir_uid->low;
    request.flags = 1;
    request.start_index = start_index;

    if (!rem_name_$send_request(net, node, &request, 0x36, 0, 0x0e,
                                 response, 0x200, &resp_len, status_ret)) {
        if (*status_ret != status_$naming_name_server_helper_is_shutdown) {
            NETBUF_$RTN_HDR(&response_ptr);
            return;
        }
    }

    entry_count = *(uint16_t *)(response + 0x16);
    if (entry_count == 0) {
        NETBUF_$RTN_HDR(&response_ptr);
        return;
    }

    src = response + 0x18;

    for (i = 0; i < entry_count && *count_ret < max_entries; i++) {
        (*count_ret)++;
        dst = (uint32_t *)((uint8_t *)rep_ret + (*count_ret - 1) * REP_ENTRY_SIZE);

        /* Copy 18-byte replica entry (4 longs + 1 word) */
        dst[0] = *(uint32_t *)(src + 0);
        dst[1] = *(uint32_t *)(src + 4);
        dst[2] = *(uint32_t *)(src + 8);
        dst[3] = *(uint32_t *)(src + 12);
        *(uint16_t *)(dst + 4) = *(uint16_t *)(src + 16);

        src += REP_ENTRY_SIZE;
    }

    NETBUF_$RTN_HDR(&response_ptr);
}

/*
 * REM_NAME_$DIR_READU - Read directory entries with auto server location
 *
 * Higher-level directory read that automatically locates a server
 * and retries on failure.
 *
 * Original address: 0x00e4ac2c
 * Original size: 236 bytes
 */
void REM_NAME_$DIR_READU(uid_t *dir_uid, void *entries_ret, int32_t *continuation,
                         uint16_t *max_entries, uint16_t *count_ret,
                         status_$t *status_ret)
{
    uint32_t node, net;
    int16_t entries_read;
    boolean tried_locate = false;

    *count_ret = 0;

    if (*max_entries == 0 || *continuation == 0) {
        *continuation = 0;
        *status_ret = status_$ok;
        return;
    }

    node = rem_name_$data.curr_node;
    net = rem_name_$data.curr_net;

    while (*count_ret < *max_entries) {
        REM_NAME_$READ_DIR(net, node, dir_uid,
                           (uint32_t)(*continuation >> 16),
                           (uint8_t *)entries_ret + (*count_ret * DIR_ENTRY_SIZE),
                           *max_entries - *count_ret, (uint16_t *)&entries_read,
                           status_ret);

        if (*status_ret == status_$ok) {
            tried_locate = true;
            *continuation = (*continuation & 0xFFFF0000) |
                           (((*continuation & 0xFFFF) + entries_read) & 0xFFFF);
            *count_ret += entries_read;
        } else if (*status_ret == status_$naming_last_entry_in_replicated_root_returned ||
                   *status_ret == status_$naming_name_server_helper_is_shutdown) {
            *continuation = 0;
            *count_ret += entries_read;
            *status_ret = status_$ok;
            return;
        } else {
            if (tried_locate || ((*continuation & 0xFFFF) != 1)) {
                *continuation = 0;
                *status_ret = status_$ok;
                return;
            }
            LOCATE_SERVER(&node, &net, status_ret);
            if (*status_ret != status_$ok) {
                *continuation = 0;
                return;
            }
            tried_locate = true;
        }
    }

    *status_ret = status_$ok;
}

/*
 * REM_NAME_$GET_ENTRY - Get a directory entry with auto server location
 *
 * Higher-level entry lookup that automatically locates a server
 * and retries on failure.
 *
 * Original address: 0x00e4ad18
 * Original size: 190 bytes
 */
void REM_NAME_$GET_ENTRY(uid_t *dir_uid, char *name, uint16_t *name_len,
                         void *entry_ret, status_$t *status_ret)
{
    uint32_t node, net;
    boolean tried_locate = false;
    status_$t status;

    if (rem_name_$data.last_status == status_$ok) {
        node = rem_name_$data.curr_node;
        net = rem_name_$data.curr_net;
    } else {
        LOCATE_SERVER(&node, &net, status_ret);
        if (*status_ret != status_$ok) {
            return;
        }
        tried_locate = true;
    }

    REM_NAME_$GET_ENTRY_BY_NAME(net, node, dir_uid, name, *name_len, entry_ret, status_ret);
    status = *status_ret;

    if (status == status_$naming_name_not_found ||
        status == status_$naming_invalid_pathname ||
        status == status_$ok) {
        return;
    }

    /* Error - try to relocate server */
    if (!tried_locate) {
        LOCATE_SERVER(&node, &net, status_ret);
        if (*status_ret == status_$ok) {
            REM_NAME_$GET_ENTRY_BY_NAME(net, node, dir_uid, name, *name_len, entry_ret, status_ret);
        }
    }
}

/*
 * REM_NAME_$FIND_NETWORK - Find a network entry by node ID
 *
 * Higher-level lookup that automatically locates a server
 * and retries on failure.
 *
 * Original address: 0x00e4add6
 * Original size: 174 bytes
 */
void REM_NAME_$FIND_NETWORK(uid_t *dir_uid, uint32_t *target_node,
                            void *entry_ret, status_$t *status_ret)
{
    uint32_t node, net;
    boolean tried_locate = false;

    if (rem_name_$data.last_status == status_$ok) {
        node = rem_name_$data.curr_node;
        net = rem_name_$data.curr_net;
    } else {
        LOCATE_SERVER(&node, &net, status_ret);
        if (*status_ret != status_$ok) {
            return;
        }
        tried_locate = true;
    }

    REM_NAME_$GET_ENTRY_BY_NODE_ID(net, node, dir_uid, *target_node, entry_ret, status_ret);

    if (*status_ret == status_$naming_name_not_found ||
        *status_ret == status_$ok) {
        return;
    }

    if (!tried_locate) {
        LOCATE_SERVER(&node, &net, status_ret);
        if (*status_ret == status_$ok) {
            REM_NAME_$GET_ENTRY_BY_NODE_ID(net, node, dir_uid, *target_node, entry_ret, status_ret);
        }
    }
}

/*
 * REM_NAME_$FIND_UID - Find an object by UID
 *
 * Higher-level lookup that automatically locates a server
 * and retries on failure.
 *
 * Original address: 0x00e4ae84
 * Original size: 162 bytes
 */
void REM_NAME_$FIND_UID(uid_t *dir_uid, uid_t *target_uid,
                        void *entry_ret, status_$t *status_ret)
{
    uint32_t node, net;

    if (rem_name_$data.last_status == status_$ok) {
        node = rem_name_$data.curr_node;
        net = rem_name_$data.curr_net;
    } else {
        LOCATE_SERVER(&node, &net, status_ret);
        if (*status_ret != status_$ok) {
            return;
        }
    }

    REM_NAME_$GET_ENTRY_BY_UID(net, node, dir_uid, target_uid, entry_ret, status_ret);

    if (*status_ret == status_$naming_name_not_found ||
        *status_ret == status_$ok) {
        return;
    }

    LOCATE_SERVER(&node, &net, status_ret);
    if (*status_ret == status_$ok) {
        REM_NAME_$GET_ENTRY_BY_UID(net, node, dir_uid, target_uid, entry_ret, status_ret);
    }
}
