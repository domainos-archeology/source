/*
 * ASKNODE_$INTERNET_INFO - Get detailed node information
 *
 * Main function for querying node information over the network.
 * This is a large function (4796 bytes) that handles many different
 * request types for various kinds of system data.
 *
 * For local node queries (node_id == NODE_$ME or node_id == 0), it
 * retrieves data directly from system structures.
 *
 * For remote nodes, it sends a network request using PKT_$SAR_INTERNET
 * and waits for a response.
 *
 * Original address: 0x00E645EA
 * Size: 4796 bytes
 */

#include "asknode/asknode_internal.h"

/* Network statistics at 0x00E24C28 area */
extern uint16_t NETWORK_$PAGIN_RQST_CNT;    /* 0x00E24C28 */
extern uint16_t NETWORK_$BAD_CHKSUM_CNT;    /* 0x00E24C2A */
extern uint16_t NETWORK_$READ_VIOL_CNT;     /* 0x00E24C2C */
extern uint16_t NETWORK_$WRITE_VIOL_CNT;    /* 0x00E24C2E */
extern uint16_t NETWORK_$READ_CALL_CNT;     /* 0x00E24C30 */
extern uint16_t NETWORK_$WRITE_CALL_CNT;    /* 0x00E24C32 */
extern uint16_t NETWORK_$INFO_RQST_CNT;     /* 0x00E24C38 */
extern uint16_t NETWORK_$MULT_PAGIN_RQST_CNT; /* 0x00E24C3A */
extern uint16_t NETWORK_$PAGOUT_RQST_CNT;   /* 0x00E24C3C */
extern uint16_t NETWORK_$ATTRIB_RQST_CNT;
extern uint16_t NETWORK_$SET_ATTRIB_CALL_CNT;
extern uint16_t NETWORK_$RCV_READ_AHEAD;
extern uint16_t NETWORK_$2LONG1;

extern uint32_t NETWORK_$PAGING_BACKLOG;
extern uint32_t NETWORK_$FILE_BACKLOG;

/* Ring network globals */
extern uint16_t RING_$PAGING_OVERFLOW;
extern uint16_t RING_$FILE_OVERFLOW;
extern uint16_t RING_$OVERFLOW_OVERFLOW;
extern uint16_t RING_$DELIVERY_FAILED;
extern uint16_t RING_$XMIT_WAITED;
extern uint16_t RING_$SEND_NULL_CNT;
extern uint16_t RING_$CLOBBERED_HDR;
extern uint32_t RING_$RCV_INT_CNT;
extern uint16_t RING_$BUSY_ON_RCV_INT;
extern uint16_t RING_$ABORT_CNT;
extern uint16_t RING_$WAKEUP_CNT;
extern uint16_t RING_$BAD_DATA_CNT;
extern uint16_t REM_FILE_$2LONG1;

/* Memory subsystem */
extern uint32_t MEM_$MEM_REC;               /* 0x00E22934 */
extern uint32_t MMAP_$REAL_PAGES;           /* 0x00E23CA0 */

/* Boot device */
extern uint32_t OS_$BOOT_DEVICE;

/* Protocol version at offset 0x1E in packet info block */
extern uint16_t DAT_00e82426;

/* Empty data constant at 0x00E658CC (zero-filled buffer) */
extern uint32_t DAT_00e658cc;

/* Status codes */
#define status_$network_transmit_failed              0x00110001
#define status_$network_request_denied_by_local_node 0x0011000E
#define status_$network_unexpected_reply_type        0x00110020
#define status_$network_bad_asknode_version_number   0x00110021

/*
 * Handle local node query for request type
 */
static uint32_t handle_local_request(uint16_t req_type, uid_t *param,
                                     uint32_t *result, status_$t *local_status)
{
    uint32_t ret_val = 0;
    *local_status = 0;

    switch (req_type) {
    case ASKNODE_REQ_BOOT_TIME:   /* 0x02 */
        /* Return boot time and current time */
        result[2] = TIME_$BOOT_TIME;
        result[3] = TIME_$CURRENT_CLOCKH;
        break;

    case ASKNODE_REQ_NODE_UID:    /* 0x04 */
    case ASKNODE_REQ_ROOT_UID:    /* 0x18 */
        {
            uid_t temp_uid;
            if (req_type == ASKNODE_REQ_NODE_UID) {
                NAME_$GET_NODE_UID(&temp_uid);
            } else {
                NAME_$GET_ROOT_UID(&temp_uid);
            }
            result[2] = temp_uid.high;
            result[3] = temp_uid.low;
            result[1] = 0;
            /* Set mother node or local node based on diskless flag */
            uint32_t *word_1e = (uint32_t *)((char *)result + 0x1E);
            *word_1e = (*word_1e & 0xFFF00000);
            if (NETWORK_$DISKLESS < 0) {
                *word_1e |= NETWORK_$MOTHER_NODE;
            } else {
                *word_1e |= NODE_$ME;
            }
            *(uint32_t *)((char *)result + 0x22) = 1 - result[2];
            *(uint32_t *)((char *)result + 0x26) = ROUTE_$PORT;
        }
        break;

    case ASKNODE_REQ_STATS:       /* 0x06 */
        /* Return comprehensive node statistics */
        *(uint16_t *)(result + 2) = 3;
        *(uint32_t *)((char *)result + 10) = NODE_$ME;
        *(uint16_t *)((char *)result + 0x0E) = 1;
        *(uint16_t *)(result + 4) = NETWORK_$INFO_RQST_CNT;
        *(uint16_t *)((char *)result + 0x12) = NETWORK_$MULT_PAGIN_RQST_CNT + NETWORK_$PAGIN_RQST_CNT;
        *(uint16_t *)(result + 5) = NETWORK_$PAGOUT_RQST_CNT;
        *(uint16_t *)((char *)result + 0x16) = NETWORK_$READ_CALL_CNT;
        *(uint16_t *)(result + 6) = NETWORK_$WRITE_CALL_CNT;
        *(uint16_t *)((char *)result + 0x1A) = NETWORK_$READ_VIOL_CNT;
        *(uint16_t *)(result + 7) = NETWORK_$WRITE_VIOL_CNT;
        *(uint16_t *)((char *)result + 0x1E) = NETWORK_$BAD_CHKSUM_CNT;
        /* Copy RING_$DATA (15 words) */
        {
            uint32_t *src = (uint32_t *)RING_$DATA;
            uint32_t *dst = result + 8;
            int16_t i;
            for (i = 0; i < 15; i++) {
                *dst++ = *src++;
            }
        }
        /* Get disk stats */
        {
            uint8_t temp[6];
            DISK_$GET_STATS(0, 0, (uint16_t *)temp, result + 0x17);
        }
        /* Copy memory stats (21 words) */
        {
            uint16_t *src = (uint16_t *)&MEM_$MEM_REC;
            uint16_t *dst = (uint16_t *)((char *)result + 0x72);
            int16_t i;
            for (i = 0; i < 21; i++) {
                *dst++ = *src++;
            }
        }
        /* Real pages count */
        if (MMAP_$REAL_PAGES < 0x10000) {
            *(uint16_t *)((char *)result + 0x76) = (uint16_t)MMAP_$REAL_PAGES;
        } else {
            *(uint16_t *)((char *)result + 0x76) = 0;
        }
        break;

    case ASKNODE_REQ_TIMEZONE:    /* 0x08 */
        /* Return timezone information */
        {
            uint32_t *dst = result + 2;
            cal_$timezone_rec_t *src = &CAL_$TIMEZONE;
            dst[0] = *(uint32_t *)src;
            dst[1] = *(uint32_t *)(src->tz_name + 2);
            dst[2] = *(uint32_t *)((char *)&src->drift.high + 2);
        }
        break;

    case ASKNODE_REQ_VOLUME_INFO: /* 0x0A */
        ret_val = VOLX_$GET_INFO(param, result + 2, result + 4, result + 5, local_status);
        break;

    case ASKNODE_REQ_PAGING_INFO: /* 0x0C */
        /* Return paging information */
        *(char *)(result + 3) = NETWORK_$DISKLESS;
        if (NETWORK_$DISKLESS < 0) {
            result[2] = NETWORK_$PAGING_FILE_UID.low & 0xFFFFF;
        } else {
            result[2] = NODE_$ME;
        }
        break;

    case ASKNODE_REQ_PROC_LIST:   /* 0x12 */
        PROC2_$LIST((uid_t *)((char *)result + 10),
                    (uint16_t *)0x00E658AE, /* constant buffer */
                    (uint16_t *)(result + 2));
        if ((*(uint16_t *)result < 3) && ((int16_t)*(uint16_t *)(result + 2) > 0x19)) {
            *(uint16_t *)(result + 2) = 0x19;
        }
        break;

    case ASKNODE_REQ_PROC_INFO:   /* 0x14 */
        PROC2_$GET_INFO(param, (void *)(result + 2),
                        (uint16_t *)0x00E658C6, local_status);
        break;

    case ASKNODE_REQ_SIGNAL:      /* 0x16 */
        PROC2_$SIGNAL_PGROUP_OS(param, (int16_t *)0x00E658C8,
                                (uint32_t *)(param + 1), local_status);
        break;

    case ASKNODE_REQ_BUILD_TIME:  /* 0x1A */
        GET_BUILD_TIME((char *)result + 10, (int16_t *)(result + 2));
        break;

    case ASKNODE_REQ_LOG_READ:    /* 0x31 */
        if ((param->high & 0x10000) == 0) {
            /* Read by line number - param points to max_len */
            LOG_$READ((void *)((char *)result + 10), (uint16_t *)param,
                      (uint16_t *)((char *)result + 8));
        } else {
            /* Read by entry index */
            LOG_$READ2((void *)((char *)result + 10), (uint16_t)(param->high),
                       0x400, (uint16_t *)((char *)result + 8));
            *local_status = (*local_status & 0xFFFF0000) | 0xFFFF;
        }
        break;

    case ASKNODE_REQ_WHO:         /* 0x45 */
        /* Time sync WHO query - just return local node */
        /* This is handled by the caller with time synchronization */
        break;

    default:
        /* Unknown request type */
        *local_status = status_$network_unknown_request_type;
        break;
    }

    return ret_val;
}

/*
 * Main ASKNODE_$INTERNET_INFO function
 */
uint32_t ASKNODE_$INTERNET_INFO(uint16_t *req_type, uint32_t *node_id,
                                int32_t *req_len, uid_t *param,
                                uint16_t *resp_len, uint32_t *result,
                                status_$t *status)
{
    uint32_t target_node = *node_id;
    status_$t local_status = 0;
    uint32_t ret_val = 0;
    uint16_t request = *req_type;

    /*
     * Check if this is a local node query.
     * Local queries are handled directly without network communication.
     */
    if (target_node == NODE_$ME || target_node == 0) {
        /* Initialize status and set protocol version */
        *status = 0;
        result[1] = 0;  /* Clear status word in result */

        /* Set protocol version (2 or 3) */
        if (*(uint16_t *)result != 2) {
            *(uint16_t *)result = 3;
        }

        /* Handle the request locally */
        ret_val = handle_local_request(request, param, result, &local_status);

        /* Set response type (request + 1) */
        *(uint16_t *)((char *)result + 2) = request + 1;
        result[1] = local_status;

        return ret_val;
    }

    /*
     * Remote node query - check if network requests are enabled
     */
    if ((NETWORK_$CAPABLE_FLAGS & 1) == 0) {
        *status = status_$network_request_denied_by_local_node;
        return ret_val;
    }

    /*
     * Special case for request 0x1F (network diagnostics)
     * which handles retries differently
     */
    if (request == 0x1F) {
        int32_t routing = *req_len;
        int8_t retry_flag = 0;

        if (*(uint16_t *)result != 2) {
            *(uint16_t *)result = 3;
        }
        *(uint16_t *)((char *)result + 2) = request + 1;

        if (routing == -1) {
            routing = 0;
            retry_flag = 0;
        }

        /* Try NETWORK_$RING_INFO with retry on failure */
        do {
            NETWORK_$RING_INFO(&routing, (ring_info_t *)(result + 2), status);
            if (*status != status_$network_transmit_failed || *req_len != -1 || retry_flag < 0) {
                break;
            }
            routing = DIR_$FIND_NET(0x29C, (int16_t)node_id);
            ret_val = 0;
            if (routing == 0) break;
            retry_flag = -1;
        } while (1);

        result[1] = *status;
        return ret_val;
    }

    /*
     * Standard remote query using PKT_$SAR_INTERNET
     */
    {
        /* Build request packet */
        uint16_t req_buf[12];   /* Request buffer (0x18 bytes) */
        uint32_t pkt_info[8];   /* Packet info block */
        uint8_t temp1[2], temp2[4];
        uint16_t data_len = 0;
        uint32_t routing = *node_id;
        int32_t port = *req_len;
        int8_t retry_flag = 0;

        /* Initialize request */
        req_buf[0] = 3;         /* Protocol version */
        req_buf[1] = request;   /* Request type */
        req_buf[2] = 0;         /* Reserved */

        /* Copy parameter based on request type */
        switch (request) {
        case 0x2B:
        case 0x23:
        case 0x14:
        case 0x4B:
            /* Copy 8-byte UID */
            *(uint32_t *)&req_buf[4] = param->high;
            *(uint32_t *)&req_buf[6] = param->low;
            break;

        case 0x16:
            /* Copy 12 bytes */
            {
                uint8_t *src = (uint8_t *)param;
                uint8_t *dst = (uint8_t *)&req_buf[4];
                int i;
                for (i = 0; i < 12; i++) *dst++ = *src++;
            }
            break;

        case 0x25:
            /* Copy 10 bytes */
            *(uint32_t *)&req_buf[4] = param->high;
            *(uint32_t *)&req_buf[6] = param->low;
            *(uint32_t *)&req_buf[8] = param[1].high;
            req_buf[10] = *(uint16_t *)&param[1].low;
            break;

        case 0x35:
            /* Copy 16 bytes */
            {
                uint8_t *src = (uint8_t *)param;
                uint8_t *dst = (uint8_t *)&req_buf[4];
                int i;
                for (i = 0; i < 16; i++) *dst++ = *src++;
            }
            break;

        case 0x5B:
        case 0x3D:
        case 0x3B:
            /* Copy high word and partial low */
            *(uint32_t *)&req_buf[4] = param->high;
            req_buf[6] = *(uint16_t *)&param->low;
            break;

        case 0x31:
            /* Log read request */
            *(uint32_t *)&req_buf[4] = param->high;
            if ((param->high & 0x10000) == 0) {
                data_len = *(uint16_t *)&param->high;
                if (data_len > 0x400) data_len = 0x400;
            } else {
                data_len = 0x400;
            }
            break;

        default:
            *(uint32_t *)&req_buf[4] = param->high;
            break;
        }

        /* If port is -1, use hint system to find routing */
        if (port == -1) {
            uid_t hint_uid;
            int32_t hints[10];
            hint_uid.high = UID_$NIL.high;
            hint_uid.low = (UID_$NIL.low & 0xFFF00000) | *node_id;
            HINT_$GET_HINTS(&hint_uid, hints);
            port = hints[0];
            retry_flag = 0;
        }

        /* Copy packet info block */
        {
            uint32_t *src = PKT_$DEFAULT_INFO;
            uint32_t *dst = pkt_info;
            int i;
            for (i = 0; i < 7; i++) *dst++ = *src++;
            *(uint16_t *)dst = *(uint16_t *)src;
        }

        /* Send and receive */
        do {
            uint16_t resp_data_len;

            PKT_$SAR_INTERNET(port, *node_id, 4, pkt_info, 6,
                              req_buf, 0x18,
                              &DAT_00e658cc, 0,  /* No request data */
                              NULL, (char *)result, *resp_len,
                              temp1, (uint16_t *)((char *)result + 10), data_len,
                              &resp_data_len, status);

            if ((*status != status_$network_transmit_failed &&
                 *status != status_$network_remote_node_failed_to_respond) ||
                *req_len != -1 || retry_flag < 0) {
                break;
            }

            port = DIR_$FIND_NET(0x29C, (int16_t)node_id);
            if (port == 0) break;
            retry_flag = -1;
        } while (1);

        /* Check response */
        if (*status != 0) {
            /* Set high bit to indicate remote error */
            *(uint8_t *)status |= 0x80;
            return *status;
        }

        /* Validate response type */
        if ((uint16_t)*result != request + 1) {
            *status = status_$network_unexpected_reply_type;
            return (uint16_t)*result;
        }

        /* Validate protocol version */
        if (*(uint16_t *)result != 3 && *(uint16_t *)result != 2 && DAT_00e82426 != 3) {
            *status = status_$network_bad_asknode_version_number;
            return (uint16_t)*result;
        }

        /* Check for remote error status */
        if (result[1] != 0) {
            *status = result[1];
        }

        /* Handle hint updates for certain request types */
        if (result[1] == 0) {
            if (request == 0x0A || request == 0x04 || request == 0x18) {
                /* Update hints based on response - extract UID from result */
                uid_t response_uid;
                response_uid.high = result[2];
                response_uid.low = result[3];
                HINT_$ADDI(&response_uid, (uint32_t *)&port);
            }
        }

        /* Special handling for log read response */
        if (request == 0x31) {
            *(uint16_t *)(result + 2) = data_len;
        }
    }

    return ret_val;
}
