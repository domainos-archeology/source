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

/*
 * Globals: NETWORK_$*_CNT statistics and NETWORK_$CAPABLE_FLAGS come from
 * network/network.h, MEM_$MEM_REC from mem/mem.h, MMAP_$REAL_PAGES from
 * mmap/mmap.h, RING_$CTL from ring/ring.h and the status_$network_* codes
 * from network/network.h (all via asknode_internal.h).
 */

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
        /* Copy RING_$CTL (15 words) */
        {
            uint32_t *src = (uint32_t *)&RING_$CTL;
            uint32_t *dst = result + 8;
            int16_t i;
            for (i = 0; i < 15; i++) {
                *dst++ = *src++;
            }
        }
        /* Get disk stats */
        {
            uint8_t temp;
            DISK_$GET_STATS(0, 0, &temp, result + 0x17);
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
            /*
             * Raw 12-byte copy of the timezone record (bytes 0..11 of
             * CAL_$TIMEZONE, done as three longword moves in the original).
             */
            uint8_t *dst = (uint8_t *)(result + 2);
            const uint8_t *src = (const uint8_t *)&CAL_$TIMEZONE;
            int i;
            for (i = 0; i < 12; i++) {
                dst[i] = src[i];
            }
        }
        break;

    case ASKNODE_REQ_VOLUME_INFO: /* 0x0A */
        /* Note: param is uid_t* but VOLX_$GET_INFO expects int16_t* vol_idx.
         * The caller likely passes the volume index in the first field. */
        VOLX_$GET_INFO((int16_t *)param, (uid_t *)(result + 2),
                       result + 4, result + 5, local_status);
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

    case ASKNODE_REQ_TIME_SYNC:   /* 0x45 */
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
            /*
             *   00e655a6    move.l D4,-(SP)          ; node_id (pointer)
             *   00e655a8    pea (A2)                 ; A2 = &NAME_$ROOT_UID (0xE8029C)
             */
            routing = DIR_$FIND_NET(&NAME_$ROOT_UID, node_id);
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
        uint32_t pkt_info[8];   /* A6-0xB8: the 30-byte PKT_$DEFAULT_INFO copy */
        /*
         * A6-0xD8: PKT_$SAR_INTERNET's tenth argument, a second packet-info
         * record of the same shape that the callee WRITES.  When a request
         * times out with no reply it stores the attempt counter there:
         * "movea.l (0x24,A6),A0 / move.w D3w,(0x8,A0)" at
         * 0x00E7205E-0x00E72062, just before setting status 0x00110007.
         * ASKNODE_$INTERNET_INFO never reads it back - the record only has
         * to exist so the callee's store lands in this frame and not
         * through a nil pointer.  (source-0fks)
         */
        uint32_t sar_resp_info[8];
        uint16_t resp_tpl_len;      /* A6-0x146 */
        uint8_t temp2[4];
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

            /*
             * 0x00E656FC - 0x00E6573C, seventeen arguments and a 0x38-byte
             * caller cleanup.  Pushed last to first:
             *    1  (-0xE8,A6)          the routing key
             *    2  *node_id            "movea.l D4,A0 / move.l (A0),-(SP)"
             *    3  #4                  the ASKNODE socket
             *    4  &(-0xB8,A6)         the PKT_$DEFAULT_INFO copy
             *    5  #6                  timeout
             *    6  &(-0x100,A6)        the request template
             *    7  #0x18               its length
             *    8  (0x1AE,PC)          the empty request-data cell at
             *                           0x00E658CC
             *    9  #0                  request data length
             *   10  &(-0xD8,A6)         sar_resp_info, see above
             *   11  (0x1C,A6)           the caller's reply record
             *   12  *(D7)               its capacity
             *   13  &(-0x146,A6)        resp_tpl_len
             *   14  (0x1C,A6) + 0x0A    the reply data area
             *   15  (-0x144,A6)         its capacity
             *   16  &(-0x142,A6)        resp_data_len
             *   17  A4                  status_ret
             */
            PKT_$SAR_INTERNET(port, *node_id, 4, pkt_info, 6,
                              req_buf, 0x18,
                              &ASKNODE_$EMPTY_DATA, 0,  /* No request data */
                              sar_resp_info, (char *)result, *resp_len,
                              &resp_tpl_len, (uint16_t *)((char *)result + 10),
                              data_len,
                              &resp_data_len, status);

            if ((*status != status_$network_transmit_failed &&
                 *status != status_$network_remote_node_failed_to_respond) ||
                *req_len != -1 || retry_flag < 0) {
                break;
            }

            /*
             *   00e6576a    move.l D4,-(SP)          ; node_id (pointer)
             *   00e6576c    pea (A3)                 ; A3 = &NAME_$ROOT_UID (0xE8029C)
             */
            port = DIR_$FIND_NET(&NAME_$ROOT_UID, node_id);
            if (port == 0) break;
            retry_flag = -1;
        } while (1);

        /* Check response */
        if (*status != 0) {
            /* Set high bit to indicate remote error */
            *(uint8_t *)status |= 0x80;
            return *status;
        }

        /*
         * Validate response type (0x00E65798 - 0x00E657AA).  The constant the
         * original stores is 0x11000B, "unexpected reply type"; the response
         * type is the word at +0x02 of the reply record.
         */
        if ((uint16_t)*result != request + 1) {
            *status = status_$network_unexpected_reply_type;
            return (uint16_t)*result;
        }

        /* Validate protocol version (0x00E657AE - 0x00E657CA): 0x110015 */
        if (*(uint16_t *)result != 3 && *(uint16_t *)result != 2 && ASKNODE_$PROTOCOL_VERSION != 3) {
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
