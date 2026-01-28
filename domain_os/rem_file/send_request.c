/*
 * REM_FILE_$SEND_REQUEST - Core network request handler
 *
 * Sends a remote file operation request to a remote node and waits
 * for a response. Handles retransmission, timeouts, busy responses,
 * and node visibility tracking.
 *
 * Protocol flow:
 *   1. Set msg_type = 1 at start of request buffer
 *   2. Allocate a socket, generate packet ID
 *   3. Send/retry loop (max 60 retries):
 *      - PKT_$SEND_INTERNET to dest_sock=2
 *      - EC_$WAIT on socket EC + TIME_$CLOCKH with computed timeout
 *      - On socket event: APP_$RECEIVE, copy header, handle bulk data
 *      - On timeout: check quit signal, probe node visibility
 *      - On busy response (first word 0xFFFF): delay 2 ticks, retry
 *   4. Validate response[3] == request[3] + 1
 *   5. Extract status from response+4
 *   6. Cleanup: SOCK_$CLOSE, output packet_id
 *
 * Connection states:
 *   0 = initial (no timeout yet)
 *   1 = first timeout occurred
 *   2 = diskless mother node (never gives up)
 *   3 = confirmed visible (got at least one response or PKT_$LIKELY_TO_ANSWER)
 *
 * Original address: 0x00E60FD8
 * Size: 1368 bytes
 */

#include "rem_file/rem_file_internal.h"
#include "file/file.h"
#include "os/os.h"
#include "app/app.h"
#include "netbuf/netbuf.h"
#include "fim/fim.h"
#include "misc/crash_system.h"

/*
 * Static error status values used for CRASH_SYSTEM calls.
 * 0x00E61530 (PC+0x494 from 0xE6109A): socket allocation failure
 * 0x00E61534 (PC+0x43C from 0xE610F6): extra_data with split request
 */
static const status_$t crash_status_sock_alloc = 0x000F0004;
static const status_$t crash_status_split_extra = 0x000F0004;

/*
 * Maximum retry count before giving up
 */
#define SEND_REQUEST_MAX_RETRIES    60  /* 0x3C */

/*
 * Maximum request size that fits in a single packet header
 */
#define SEND_REQUEST_MAX_SINGLE     0x200  /* 512 bytes */

/*
 * Connection state tracking
 */
#define CONN_STATE_INITIAL          0
#define CONN_STATE_FIRST_TIMEOUT    1
#define CONN_STATE_DISKLESS_MOTHER  2
#define CONN_STATE_CONFIRMED        3

void REM_FILE_$SEND_REQUEST(void *addr_info, void *request, int16_t request_len,
                            void *extra_data, int16_t extra_len,
                            void *response, uint16_t response_max,
                            uint16_t *received_len, void *bulk_data, int16_t bulk_max,
                            int16_t *bulk_len, uint16_t *packet_id,
                            status_$t *status_ret)
{
    uint32_t *addr_words = (uint32_t *)addr_info;
    uint16_t *req_u16 = (uint16_t *)request;
    int16_t *resp_i16 = (int16_t *)response;
    uint8_t *req_u8 = (uint8_t *)request;

    int16_t sock_num;           /* local_d4: allocated socket number */
    int16_t pkt_id;             /* local_d2: outgoing packet ID */
    int16_t resp_pkt_id;        /* local_d0: received packet ID */
    int16_t retry_count;        /* local_ce: retry counter */
    int16_t send_hdr_len;       /* local_cc: header portion length */
    int16_t send_data_len;      /* local_ca: extra data portion length */
    uint16_t send_overhead;     /* auStack_c8: PKT_$SEND_INTERNET len_out */
    uint16_t send_c6;           /* local_c6: PKT_$SEND_INTERNET extra out */
    uint16_t split_flag;        /* local_c4: 1 if large packet / bulk data */
    int16_t conn_state;         /* local_c2: connection state */
    int32_t sock_ec_wait_val;   /* local_c0: next EC value to wait for */
    int32_t quit_saved;         /* local_bc: saved quit value */
    int32_t timeout_deadline;   /* local_b8: timeout clock target */
    status_$t local_status;     /* local_b4: temporary status */
    ec_$eventcount_t *sock_ec;  /* local_b0: socket event counter pointer */
    void *send_data_ptr;        /* local_ac: extra data pointer for send */
    char *bulk_va;              /* local_a8: bulk data virtual address */
    char *bulk_dest;            /* local_a4: destination for bulk copy */
    uint32_t hdr_page;          /* local_a0: header page address for RTN_HDR */
    uint32_t bulk_handle;       /* local_9c: bulk data buffer handle */
    uint8_t cleanup_buf[88];    /* auStack_8c: FIM_$CLEANUP handler buffer */

    /* APP_$RECEIVE result: local_34 through local_2c */
    uint32_t recv_hdr_ptr;      /* local_34: pointer to received header */
    char *recv_data_ptr;        /* local_30: pointer to received data */
    uint32_t recv_data_bufs[10]; /* local_2c: data buffer array */

    /* Early exit: process type 9 (special processes) cannot do remote ops */
    if (PROC1_$TYPE[PROC1_$CURRENT] == 9) {
        *status_ret = file_$object_not_found;
        return;
    }

    /* Early exit: network not capable and target is not local node */
    if ((NETWORK_$CAPABLE_FLAGS & 1) == 0 && addr_words[1] != NODE_$ME) {
        *status_ret = file_$comms_problem_with_remote_node;
        return;
    }

    /* Determine initial connection state */
    if (NETWORK_$DISKLESS < 0 && addr_words[1] == NETWORK_$MOTHER_NODE) {
        conn_state = CONN_STATE_DISKLESS_MOTHER;
    } else {
        conn_state = CONN_STATE_INITIAL;
    }

    /* Set msg_type = 1 at start of request buffer */
    *req_u16 = 1;

    /* Determine if we need a split packet (bulk data present or large response) */
    if (extra_len == 0 && (int16_t)response_max <= 0x200) {
        split_flag = 0;
    } else {
        split_flag = 1;
    }

    /* Allocate a socket: protocol=3, bufpages=split_flag|0x0400 */
    {
        int8_t result;
        result = SOCK_$ALLOCATE((uint16_t *)&sock_num, 0x30001,
                                ((uint32_t)split_flag << 16) | 0x0400);
        if (result >= 0) {
            CRASH_SYSTEM(&crash_status_sock_alloc);
        }
    }

    /* Get socket event counter pointer and initial wait value */
    sock_ec = SOCK_$EVENT_COUNTERS[sock_num];
    sock_ec_wait_val = sock_ec->value + 1;

    /* Save current quit value for this address space */
    quit_saved = FIM_$QUIT_VALUE[PROC1_$AS_ID];

    /* Generate packet ID */
    pkt_id = PKT_$NEXT_ID();

    /* Initialize retry counter */
    retry_count = 0;

    /* Set up send parameters - handle request splitting if needed */
    if (request_len <= SEND_REQUEST_MAX_SINGLE) {
        /* Request fits in single packet */
        send_hdr_len = request_len;
        send_data_len = extra_len;
        send_data_ptr = extra_data;
    } else {
        /* Request too large for single header - split at 0x200 */
        if (extra_len != 0) {
            /* Cannot have both split request and extra data */
            CRASH_SYSTEM(&crash_status_split_extra);
        }
        send_hdr_len = SEND_REQUEST_MAX_SINGLE;
        send_data_len = request_len - SEND_REQUEST_MAX_SINGLE;
        send_data_ptr = (uint8_t *)request + SEND_REQUEST_MAX_SINGLE;
    }

    /* Main send/retry loop */
retry_send:
    do {
        /* Check retry limit */
        if (retry_count > SEND_REQUEST_MAX_RETRIES && conn_state != CONN_STATE_DISKLESS_MOTHER) {
            PKT_$NOTE_VISIBLE(addr_words[1], 0);
            break;  /* Fall through to comms_problem */
        }

        /* Send the packet to dest_sock=2 (file server socket) */
        PKT_$SEND_INTERNET(addr_words[0], addr_words[1],
                           2,              /* dest_sock */
                           -1,             /* src_node_or = use default */
                           NODE_$ME,       /* src_node */
                           sock_num,       /* src_sock */
                           DAT_00e2e380,   /* pkt_info template */
                           pkt_id,         /* request ID */
                           request,        /* template = request header */
                           send_hdr_len,   /* template length */
                           send_data_ptr,  /* data = extra */
                           send_data_len,  /* data length */
                           &send_overhead, /* len_out */
                           &send_c6,       /* extra_out */
                           &local_status);

        if (local_status != status_$ok) {
            /* Send failed */
            if (conn_state == CONN_STATE_DISKLESS_MOTHER) {
                goto retry_send;
            }
            *status_ret = file_$comms_problem_with_remote_node;
            goto done;
        }

        /* Compute timeout deadline:
         * TIME_$CLOCKH + per-process timeout base (A5+8) + send overhead (local_c6)
         *
         * TODO: The per-process timeout base is accessed via A5+8. This is an
         * A5-relative global in the original Pascal runtime. For now we use 0
         * as a placeholder - the actual value would come from the process-specific
         * data area. This needs arch-specific abstraction.
         */
        timeout_deadline = (int32_t)TIME_$CLOCKH + 0 /* *(uint16_t *)(A5+8) */ + (uint32_t)send_c6;

        /* Wait loop for response */
        do {
            while (1) {
                /* EC_$WAIT: wait on socket EC (index 0) and timer (index 1)
                 *
                 * Assembly builds the EC array as:
                 *   ecs[0] = NULL (terminator placeholder, but overwritten)
                 *   ecs[1] = sock_ec (local_b0)
                 *   ecs[2] = &TIME_$CLOCKH
                 *
                 * wait_vals = sock_ec_wait_val (local_c0) used as the value
                 * for EC_$WAIT. The array layout is built on stack.
                 *
                 * Returns: 0 = sock event, 1 = timer, 2+ = quit
                 */
                int16_t which;
                do {
                    ec_$eventcount_t *ecs[3];
                    int32_t wait_vals[3];

                    ecs[0] = NULL;
                    ecs[1] = sock_ec;
                    ecs[2] = (ec_$eventcount_t *)&TIME_$CLOCKH;

                    /* EC_$WAIT uses the wait_val parameter as a pointer to
                     * an array of int32_t values corresponding to each EC */
                    wait_vals[0] = 0;
                    wait_vals[1] = sock_ec_wait_val;
                    wait_vals[2] = timeout_deadline;

                    which = EC_$WAIT(ecs, (int32_t *)&sock_ec_wait_val);
                } while (0);

                if (which == 0) {
                    /* Socket event */
                    break;
                }

                if (which == 1) {
                    /* Timeout on quit EC or similar - check quit signal */
                    int16_t quit_offset = PROC1_$AS_ID * 12;
                    int32_t *quit_ec_base = (int32_t *)&FIM_$QUIT_EC;

                    if (quit_saved != quit_ec_base[quit_offset / 4]) {
                        /* Quit was signalled: set status with high bit */
                        *status_ret = 0x00120010;
                        *(uint8_t *)status_ret |= 0x80;
                        /* Update saved quit value */
                        FIM_$QUIT_VALUE[PROC1_$AS_ID] = quit_ec_base[quit_offset / 4];
                        goto done;
                    }

                    /* Timeout without quit - add 12 to retry count */
                    retry_count += 12;

                    if (conn_state == CONN_STATE_INITIAL) {
                        conn_state = CONN_STATE_FIRST_TIMEOUT;
                        goto retry_send;
                    }
                    if (conn_state == CONN_STATE_FIRST_TIMEOUT) {
                        /* Probe if node is still reachable */
                        int8_t likely = PKT_$LIKELY_TO_ANSWER(addr_info, status_ret);
                        if (likely >= 0) {
                            /* Node not responding */
                            *status_ret = status_$network_remote_node_failed_to_respond;
                            goto done;
                        }
                        conn_state = CONN_STATE_CONFIRMED;
                        goto retry_send;
                    }
                    /* CONN_STATE_DISKLESS_MOTHER or CONN_STATE_CONFIRMED - just retry */
                    goto retry_send;
                }
                /* which == other: shouldn't happen, loop again */
            }

            /* Socket event received - advance wait value and try receive */
            sock_ec_wait_val++;
            APP_$RECEIVE(sock_num, &recv_hdr_ptr, &local_status);

            /* If queue is empty, keep waiting */
            if (local_status == status_$network_buffer_queue_is_empty) {
                continue;
            }

            if (local_status != status_$ok) {
                /* Receive error - return any pending bulk data buffer */
                if (bulk_handle != 0) {
                    NETBUF_$RTN_DAT(bulk_handle);
                }
                retry_count += 12;
                goto retry_send;
            }

            /* Successfully received packet. Parse the header:
             * recv_hdr_ptr points to header structure:
             *   +0x00: (uint32_t) misc
             *   +0x02: (uint16_t) header copy length
             *   +0x04: (int16_t)  bulk data length (stored to *bulk_len)
             *   +0x06: (int16_t)  response packet ID
             */
            *bulk_len = *(int16_t *)(recv_hdr_ptr + 4);
            resp_pkt_id = *(int16_t *)(recv_hdr_ptr + 6);

            /* Determine copy length (min of header length and response_max) */
            {
                uint16_t copy_len = *(uint16_t *)(recv_hdr_ptr + 2);
                if ((int16_t)response_max < (int16_t)copy_len) {
                    copy_len = response_max;
                }
                *received_len = copy_len;

                /* Copy response header to caller's response buffer.
                 * recv_data_ptr (local_30) contains the data pointer.
                 * The sign-extension of copy_len is used for the length arg. */
                OS_$DATA_COPY(recv_data_ptr, (char *)response,
                              (uint32_t)(int32_t)(int16_t)copy_len);
            }

            /* Return the header buffer page */
            hdr_page = (uint32_t)recv_data_ptr & 0xFFFFFC00;
            NETBUF_$RTN_HDR(&hdr_page);

            /* Check if bulk data length exceeds limit */
            if (*bulk_len >= 0x401) {
                local_status = status_$network_data_length_too_large;
                PKT_$DUMP_DATA(recv_data_bufs, *bulk_len);
                /* Continue waiting (goes back to inner do-while) */
                continue;
            }
            break;
        } while (1);

        /* Process bulk data */
        bulk_handle = recv_data_bufs[0];
        if (bulk_handle != 0) {
            NETBUF_$GETVA(bulk_handle, (uint32_t *)&bulk_va, &local_status);
            if (local_status != status_$ok) {
                CRASH_SYSTEM(&local_status);
            }

            if (bulk_max == 0) {
                /* No separate bulk buffer - append to response buffer */
                int32_t avail;
                bulk_dest = (char *)response + (int16_t)*received_len;
                avail = (int32_t)(int16_t)response_max - (int32_t)(int16_t)*received_len;
                if (*bulk_len < (int16_t)avail) {
                    avail = (int32_t)*bulk_len;
                }
                *bulk_len = (int16_t)avail;
            } else {
                /* Separate bulk data buffer provided */
                bulk_dest = (char *)bulk_data;
                if (bulk_max < *bulk_len) {
                    *bulk_len = bulk_max;
                }
            }

            if (*bulk_len > 0) {
                /* Set up FIM cleanup handler for safe copy */
                local_status = FIM_$CLEANUP(cleanup_buf);
                if (local_status == status_$cleanup_handler_set) {
                    /* Normal path - copy bulk data */
                    OS_$DATA_COPY(bulk_va, bulk_dest,
                                  (uint32_t)(int32_t)*bulk_len);
                    FIM_$RLS_CLEANUP(cleanup_buf);
                } else {
                    /* Cleanup was triggered - release buffers and signal */
                    uint32_t ppn = NETBUF_$RTNVA((uint32_t *)&bulk_va);
                    NETBUF_$RTN_DAT(ppn);
                    FIM_$SIGNAL(local_status);
                    /* FIM_$SIGNAL does not return */
                }
            }

            if (bulk_max == 0) {
                /* Clear bulk_len since data was merged into response */
                *bulk_len = 0;
            }

            /* Return the bulk data VA mapping and buffer */
            {
                uint32_t ppn = NETBUF_$RTNVA((uint32_t *)&bulk_va);
                NETBUF_$RTN_DAT(ppn);
            }
        }

        /* Check if response packet ID matches our request */
        if (pkt_id != resp_pkt_id) {
            /* Stale/mismatched response - keep waiting */
            continue;
        }

        /* Response matched our request */

        /* Update node visibility if this was first response */
        if (conn_state == CONN_STATE_FIRST_TIMEOUT || conn_state == CONN_STATE_INITIAL) {
            conn_state = CONN_STATE_CONFIRMED;
            PKT_$NOTE_VISIBLE(addr_words[1], -1);  /* 0xFF = visible */
        }

        /* Check for busy response (first word of response = 0xFFFF) */
        if (resp_i16[0] == -1) {
            /* Server is busy - increment per-process busy counter (A5+4)
             * and wait 2 ticks before retrying.
             *
             * TODO: The assembly does addq.l #1,(0x4,A5) which increments
             * a per-process counter. This needs arch-specific abstraction.
             */
            {
                ec_$eventcount_t *timer_ecs[3];
                int32_t timer_vals[3];

                timer_ecs[0] = NULL;
                timer_ecs[1] = (ec_$eventcount_t *)&TIME_$CLOCKH;
                timer_ecs[2] = NULL;

                timer_vals[0] = 0;
                timer_vals[1] = (int32_t)(TIME_$CLOCKH + 2);

                /* Wait on just the timer EC (address 0xe2b0d4) */
                EC_$WAIT(timer_ecs, timer_vals);
            }
            retry_count += 1;
            goto retry_send;
        }

        /* Validate response opcode: response[3] should be request[3] + 1 */
        if ((uint32_t)((uint8_t *)response)[3] == (uint32_t)req_u8[3] + 1) {
            /* Valid response - extract status from response at offset +4 */
            *status_ret = *(status_$t *)((uint8_t *)response + 4);
        } else {
            /* Bad response opcode mismatch */
            *status_ret = file_$bad_reply_received_from_remote_node;
        }
        goto done;

    } while (conn_state == CONN_STATE_DISKLESS_MOTHER);

    /* Retry limit exceeded (or non-diskless-mother broke out of loop) */
    *status_ret = file_$comms_problem_with_remote_node;

done:
    SOCK_$CLOSE(sock_num);
    *packet_id = (uint16_t)pkt_id;
}
