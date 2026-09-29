/*
 * rem_file/send_request.c - REM_FILE_$SEND_REQUEST (0x00E60FD8, 1368 bytes)
 *
 * The one transport every REM_FILE_$* client stub goes through: it stamps the
 * message type into the caller's request record, allocates a reply socket,
 * sends the request to socket 2 on the remote node, waits for the matching
 * reply, copies the reply header (and any bulk payload) back to the caller and
 * closes the socket again.
 *
 * Re-emitted block for block against the listing for bead source-ldrp; every
 * store the earlier transcription dropped is cited by address below.
 *
 * Frame: `link.w A6,-0xd8` + a nine-register movem (0x24 bytes), so the
 * epilogue is `movem.l (-0xfc,A6)` (0x00E61526).
 *
 * Arguments (A6 displacements taken straight from the listing):
 *   0x08 addr_info      long, {network, node}; node is addr_info[1]
 *   0x0C request        long
 *   0x10 request_len    word  (D2 at 0x00E60FE0)
 *   0x12 extra_data     long  (0x00E61128)
 *   0x16 extra_len      word  (D3 at 0x00E60FE4)
 *   0x18 response       long
 *   0x1C response_max   word
 *   0x1E received_len   long, word out (0x00E6128C)
 *   0x22 bulk_data      long  (0x00E6133C)
 *   0x26 bulk_max       word
 *   0x28 bulk_len       long, word in/out
 *   0x2C packet_id      long, word out (0x00E61522)
 *   0x30 status_ret     long
 *
 * A5 is the REM_FILE module base 0x00E823FC, inherited from the caller (this
 * routine never loads it); A5+0x04 is REM_FILE_$BUSY_RETRY_COUNT and A5+0x08
 * REM_FILE_$COMPLETION_TIME.  See rem_file/rem_file_data.c.
 */

#include "rem_file/rem_file_internal.h"
#include "arch/arch.h"
#include "app/app.h"
#include "ec/ec.h"
#include "file/file.h"
#include "fim/fim.h"
#include "netbuf/netbuf.h"
#include "os/os.h"
#include "misc/crash_system.h"

/*
 * The two `pea (d,PC)` status cells this function hands CRASH_SYSTEM.  Both
 * sit in the code region right after the `rts` at 0x00E6152E; the map exports
 * no symbol for them.  `gsk read 0xE61528 16`:
 *
 *   00e61528  1c fc ff 04 4e 5e 4e 75  00 11 00 05 00 11 00 01
 *                                      ^0xE61530   ^0xE61534
 */

/* 0x00E61530, bytes 00 11 00 05.  Reached from `pea (0x494,PC)` at
 * 0x00E6109A (extension word 0x00E6109C + 0x494).  The SR10.2 status table
 * calls 0x00110005 "no available socket", which is what this call site means;
 * the tree's single name for the code comes from the SR10.4 table. */
static const status_$t rem_file_$no_socket_status =
    status_$network_receive_process_failed_to_start;

/* 0x00E61534, bytes 00 11 00 01 - "buffer error".  Reached from
 * `pea (0x43c,PC)` at 0x00E610F6 (extension word 0x00E610F8 + 0x43C). */
static const status_$t rem_file_$buffer_error_status = status_$network_buffer_error;

/*
 * Retry budget (0x00E61130 "cmpi.w #0x3c,(-0xca,A6)").  A plain timeout adds
 * 12 (0x00E61252, 0x00E614CC) and a "server busy" reply adds 1 (0x00E61446).
 */
#define SEND_REQUEST_RETRY_BUDGET   0x3C

/* The largest request that still fits in the packet template (0x00E610EC),
 * and the response-size threshold that forces a data page (0x00E61066). */
#define SEND_REQUEST_MAX_TEMPLATE   0x200

/* The largest bulk payload the reply may carry (0x00E612C2). */
#define SEND_REQUEST_MAX_BULK       0x400

/*
 * Connection states (the word at A6-0xBE).
 *   0  nothing heard yet
 *   1  one timeout seen; the next one probes with PKT_$LIKELY_TO_ANSWER
 *   2  diskless mother node - never give up, never probe
 *   3  the node is known to be answering
 */
#define SEND_REQUEST_STATE_INITIAL          0
#define SEND_REQUEST_STATE_FIRST_TIMEOUT    1
#define SEND_REQUEST_STATE_MOTHER           2
#define SEND_REQUEST_STATE_CONFIRMED        3

/* The remote file server's well-known socket (0x00E6118C "move.w #0x2,-(SP)"). */
#define SEND_REQUEST_SERVER_SOCKET  2

void REM_FILE_$SEND_REQUEST(void *addr_info, void *request, int16_t request_len,
                            void *extra_data, int16_t extra_len,
                            void *response, uint16_t response_max,
                            uint16_t *received_len, void *bulk_data, int16_t bulk_max,
                            int16_t *bulk_len, uint16_t *packet_id,
                            status_$t *status_ret)
{
    const uint32_t *addr = (const uint32_t *)addr_info;   /* (0x8,A6) */
    uint8_t *request_b = (uint8_t *)request;
    uint8_t *response_b = (uint8_t *)response;

    int16_t   sock_num;         /* A6-0xD0 */
    int16_t   pkt_id;           /* A6-0xCE */
    int16_t   reply_id;         /* A6-0xCC */
    int16_t   retry_count;      /* A6-0xCA */
    int16_t   template_len;     /* A6-0xC8 */
    int16_t   send_data_len;    /* A6-0xC6 */
    uint16_t  retry_hint;       /* A6-0xC4, PKT_$SEND_INTERNET output */
    uint16_t  timeout_out;      /* A6-0xC2, PKT_$SEND_INTERNET output */
    int16_t   conn_state;       /* A6-0xBE */
    int16_t   want_data_page;   /* A6-0xC0 */
    void     *send_data_ptr;    /* A6-0xA8, a caller pointer */
    ec_$eventcount_t *sock_ec;  /* A6-0xAC */
    int32_t   sock_wait_val;    /* A6-0xBC */
    int32_t   quit_saved;       /* A6-0xB8 */
    int32_t   deadline;         /* A6-0xB4 */
    status_$t local_status;     /* A6-0xB0 */
    uint32_t  bulk_va;          /* A6-0xA4, NETBUF_$GETVA output (a VA) */
    void     *bulk_dest;        /* A6-0xA0, a caller pointer */
    uint32_t  hdr_va;           /* A6-0x9C, NETBUF_$RTN_HDR argument */
    uint32_t  bulk_handle;      /* A6-0x98 */
    uint8_t   cleanup_rec[88];  /* A6-0x88, FIM_$CLEANUP handler record */
    app_$receive_rec_t rcv;     /* A6-0x30 */

    const app_$reply_hdr_t *reply;  /* A0 at 0x00E6125A */
    int32_t   copy_len;         /* D3/D4 */
    int32_t   resp_max_l;       /* D2, the sign-extended response_max */

    /* 0x00E60FE8-0x00E61008: a type-9 (server) process may not do remote
     * file operations. */
    if (PROC1_$DATA.type[PROC1_$CURRENT] == 9) {
        *status_ret = file_$object_not_found;            /* 0x00E61002 */
        goto function_exit;                              /* bra 0x00E61526 */
    }

    /* 0x00E6100C-0x00E61030: without the network capability bit only the
     * local node may be addressed. */
    if ((NETWORK_$CAPABLE_FLAGS & 1) == 0 && addr[1] != NODE_$ME) {
        *status_ret = file_$comms_problem_with_remote_node;  /* 0x00E6102A */
        goto function_exit;
    }

    /* 0x00E61034-0x00E61054 */
    if (NETWORK_$DISKLESS < 0 && addr[1] == NETWORK_$MOTHER_NODE) {
        conn_state = SEND_REQUEST_STATE_MOTHER;
    } else {
        conn_state = SEND_REQUEST_STATE_INITIAL;
    }

    /* 0x00E61058: stamp the message type into the caller's request record. */
    ((rem_file_request_hdr_t *)request)->msg_type = 1;    /* 0x00E6105C */

    /* 0x00E61060-0x00E61076: a data page is needed when the caller expects
     * bulk data or a reply larger than the template. */
    if (extra_len != 0 || (int16_t)response_max > SEND_REQUEST_MAX_TEMPLATE) {
        want_data_page = 1;
    } else {
        want_data_page = 0;
    }

    /* 0x00E6107A-0x00E610A4.  SOCK_$ALLOCATE's third argument is a packed
     * word pair: the high word (want_data_page) is the netbuf DATA page count
     * and the low word (0x400) the maximum accepted packet data length; the
     * compiler pushes the two halves separately.  The second argument's
     * 0x30001 is queue depth 3 / one header page.
     *
     * The result is a Domain boolean: 0xFF means the socket was allocated, so
     * a NON-negative result is the failure ("tst.b D0b / bmi" at 0x00E61096).
     */
    if (SOCK_$ALLOCATE((uint16_t *)&sock_num, 0x00030001,
                       ((uint32_t)(uint16_t)want_data_page << 16) | 0x0400) >= 0) {
        CRASH_SYSTEM(&rem_file_$no_socket_status);       /* 0x00E6109A */
    }

    /* 0x00E610A6-0x00E610C8.  The table base 0xE28DB4 is indexed with a -4
     * displacement, i.e. SOCK_$DATA.socket_ptr[sock_num] is socket
     * sock_num's descriptor (see sock/sock.h). */
    sock_ec = &SOCK_$DATA.socket_ptr[sock_num]->ec;
    sock_wait_val = sock_ec->value + 1;

    /* 0x00E610CA-0x00E610DC */
    quit_saved = FIM_$WIRED_DATA.quit_value[PROC1_$AS_ID];

    pkt_id = PKT_$NEXT_ID();                             /* 0x00E610DE */
    retry_count = 0;                                     /* 0x00E610E8 */

    /* 0x00E610EC-0x00E6112E: a request longer than the template is split, the
     * tail riding along as the packet's data.  A split request may not also
     * carry caller-supplied extra data. */
    if (request_len > SEND_REQUEST_MAX_TEMPLATE) {
        if (extra_len != 0) {
            CRASH_SYSTEM(&rem_file_$buffer_error_status); /* 0x00E610F6 */
        }
        template_len  = SEND_REQUEST_MAX_TEMPLATE;
        send_data_len = (int16_t)(request_len - SEND_REQUEST_MAX_TEMPLATE);
        send_data_ptr = request_b + SEND_REQUEST_MAX_TEMPLATE;
    } else {
        template_len  = request_len;
        send_data_len = extra_len;
        send_data_ptr = extra_data;
    }

send_request:                                            /* 0x00E61130 */
    /* 0x00E61130-0x00E61154: out of retries.  The diskless mother node is
     * exempt - a diskless node has nowhere else to go. */
    if (retry_count > SEND_REQUEST_RETRY_BUDGET &&
        conn_state != SEND_REQUEST_STATE_MOTHER) {
        PKT_$NOTE_VISIBLE(addr[1], false);               /* 0x00E61140 */
        *status_ret = file_$comms_problem_with_remote_node;  /* 0x00E611BC */
        goto close_socket;
    }

    /* 0x00E61156-0x00E611A0: fifteen arguments plus a 2-byte Pascal result
     * slot; the caller pops 0x34 bytes. */
    PKT_$SEND_INTERNET(addr[0], addr[1],
                       SEND_REQUEST_SERVER_SOCKET,
                       -1,
                       NODE_$ME,
                       (uint16_t)sock_num,
                       REM_FILE_$DATA,                   /* 0x00E61178 */
                       (uint16_t)pkt_id,
                       request,
                       (uint16_t)template_len,
                       send_data_ptr,
                       send_data_len,
                       &retry_hint,                      /* A6-0xC4 */
                       &timeout_out,                     /* A6-0xC2 */
                       &local_status);

    /* 0x00E611A4-0x00E611C2.  D0 is loaded from the timeout output BEFORE the
     * status is tested, and is only used on the success path. */
    if (local_status != status_$ok) {
        if (conn_state == SEND_REQUEST_STATE_MOTHER) {
            goto send_request;                           /* 0x00E611B4 */
        }
        *status_ret = file_$comms_problem_with_remote_node;  /* 0x00E611BC */
        goto close_socket;
    }

    /* 0x00E611C6-0x00E611DA: deadline = TIME_$CLOCKH + REM_FILE_$COMPLETION_TIME
     * + the per-request timeout PKT_$SEND_INTERNET produced.  Both words are
     * zero-extended before the adds. */
    deadline = (int32_t)((uint32_t)TIME_$CLOCKH +
                         (uint32_t)REM_FILE_$COMPLETION_TIME +
                         (uint32_t)timeout_out);

await_event:                                             /* 0x00E611E0 */
    for (;;) {
        int16_t which;

        /* 0x00E611E0-0x00E61200: six longwords, no result slot.
         *   ecs  = { sock_ec, &TIME_$CLOCKH, NIL }
         *   vals = { sock_wait_val, deadline, 0 }
         */
        which = EC_$WAIT((ec_$wait_ecs_t){{ sock_ec,
                                            (ec_$eventcount_t *)&TIME_$CLOCKH,
                                            NULL }},
                         (ec_$wait_vals_t){{ sock_wait_val, deadline, 0 }});

        if (which == 0) {
            break;                                       /* 0x00E61206 */
        }
        if (which == 1) {
            goto timer_fired;                            /* 0x00E6120C */
        }
        /* 0x00E61210: anything else re-waits. */
    }

    /* --- the socket event: 0x00E61212 ------------------------------------ */
    sock_wait_val++;                                     /* 0x00E61212 */
    APP_$RECEIVE((uint16_t)sock_num, &rcv, &local_status);   /* 0x00E61224 */

    if (local_status == status_$network_buffer_queue_is_empty) {
        goto await_event;                                /* 0x00E61238 */
    }
    if (local_status != status_$ok) {
        /* 0x00E6123E-0x00E61256.  bulk_handle is read here even on the first
         * pass, when the frame slot A6-0x98 still holds whatever the caller
         * left there - an original hazard, preserved. */
        if (bulk_handle != 0) {
            NETBUF_$RTN_DAT(bulk_handle);                /* 0x00E61248 */
        }
        retry_count = (int16_t)(retry_count + 12);       /* 0x00E61252 */
        goto send_request;
    }

    /* 0x00E6125A-0x00E61266: unpack the reply header. */
    reply = (const app_$reply_hdr_t *)ARCH_VA_TO_PTR(rcv.reply);
    *bulk_len = (int16_t)reply->data_len;                /* 0x00E61262 */
    reply_id  = (int16_t)reply->request_id;                /* 0x00E61266 */

    /* 0x00E6126C-0x00E6128C: copy_len = min(reply->template_len, response_max).
     * template_len is zero-extended, response_max sign-extended, and the
     * compare is a signed longword compare. */
    copy_len   = (int32_t)(uint32_t)reply->template_len;
    resp_max_l = (int32_t)(int16_t)response_max;
    if (copy_len > resp_max_l) {
        copy_len = resp_max_l;
    }
    *received_len = (uint16_t)(int16_t)copy_len;         /* 0x00E6128C */
    copy_len = (int32_t)(int16_t)copy_len;               /* ext.l D4, 0x00E6128E */

    /* 0x00E61292-0x00E612A2 */
    OS_$DATA_COPY(ARCH_VA_TO_PTR(rcv.data), response, (uint32_t)copy_len);

    /* 0x00E612A6-0x00E612BC: round the payload VA down to its 1KB page and
     * give the header buffer back.  `andi.w #-0x400,D5w` only touches the low
     * word, which is the same as masking the longword with 0xFFFFFC00. */
    hdr_va = rcv.data & 0xFFFFFC00u;
    NETBUF_$RTN_HDR(&hdr_va);

    /* 0x00E612BE-0x00E612E0: a bulk payload larger than 0x400 is refused and
     * its pages dumped; the wait resumes. */
    if (*bulk_len > SEND_REQUEST_MAX_BULK) {
        local_status = status_$network_data_length_too_large;  /* 0x00E612C8 */
        PKT_$DUMP_DATA(rcv.data_pages, *bulk_len);       /* 0x00E612D8 */
        goto await_event;                                /* 0x00E612E0 */
    }

    /* 0x00E612E4-0x00E612EA */
    bulk_handle = rcv.data_pages[0];
    if (bulk_handle != 0) {
        /* 0x00E612EE-0x00E61314 */
        NETBUF_$GETVA(bulk_handle, &bulk_va, &local_status);
        if (local_status != status_$ok) {
            CRASH_SYSTEM(&local_status);                 /* 0x00E6130E */
        }

        if (bulk_max == 0) {
            /* 0x00E6131C-0x00E6133A: no separate bulk buffer, so the payload
             * is appended to the reply buffer and clipped to what is left of
             * response_max. */
            bulk_dest = response_b + copy_len;
            resp_max_l -= copy_len;
            if (resp_max_l > (int32_t)*bulk_len) {
                resp_max_l = (int32_t)*bulk_len;
            }
            *bulk_len = (int16_t)resp_max_l;             /* 0x00E61338 */
        } else {
            /* 0x00E6133C-0x00E61354: word compare against bulk_max. */
            bulk_dest = bulk_data;
            if (*bulk_len > bulk_max) {
                *bulk_len = bulk_max;
            }
        }

        /* 0x00E61356-0x00E613BA */
        if (*bulk_len > 0) {
            local_status = FIM_$CLEANUP(cleanup_rec);    /* 0x00E6135E */
            if (local_status == status_$cleanup_handler_set) {
                OS_$DATA_COPY(ARCH_VA_TO_PTR(bulk_va), bulk_dest,
                              (uint32_t)(int32_t)*bulk_len);
                FIM_$RLS_CLEANUP(cleanup_rec);           /* 0x00E61392 */
            } else {
                NETBUF_$RTN_DAT(NETBUF_$RTNVA(&bulk_va));  /* 0x00E6139A */
                FIM_$SIGNAL(local_status);               /* 0x00E613B4 */
            }
        }

        /* 0x00E613BC-0x00E613C6: the appended-payload case reports a bulk
         * length of zero, because the caller already has it in `response`. */
        if (bulk_max == 0) {
            *bulk_len = 0;
        }

        /* 0x00E613C8-0x00E613DC */
        NETBUF_$RTN_DAT(NETBUF_$RTNVA(&bulk_va));
    }

    /* 0x00E613DE-0x00E613E6: a reply for some other request is discarded. */
    if (pkt_id != reply_id) {
        goto await_event;
    }

    /* 0x00E613EA-0x00E61410: the node has answered. */
    if (conn_state == SEND_REQUEST_STATE_FIRST_TIMEOUT ||
        conn_state == SEND_REQUEST_STATE_INITIAL) {
        conn_state = SEND_REQUEST_STATE_CONFIRMED;
        PKT_$NOTE_VISIBLE(addr[1], true);                /* 0x00E6140A */
    }

    /* 0x00E61412-0x00E6144A: a reply whose first word is 0xFFFF means the
     * server was busy.  Count it, sleep two ticks and send again. */
    if (*(int16_t *)response == -1) {
        REM_FILE_$BUSY_RETRY_COUNT++;                    /* 0x00E6141C */

        /* 0x00E61420-0x00E61442.  ecs[1] is `move.l (SP),-(SP)`, a copy of
         * the zero just pushed, so the list is { &TIME_$CLOCKH, NIL, NIL }. */
        EC_$WAIT((ec_$wait_ecs_t){{ (ec_$eventcount_t *)&TIME_$CLOCKH,
                                    NULL, NULL }},
                 (ec_$wait_vals_t){{ (int32_t)(TIME_$CLOCKH + 2), 0, 0 }});

        retry_count = (int16_t)(retry_count + 1);        /* 0x00E61446 */
        goto send_request;
    }

    /* 0x00E6144E-0x00E6147E: the reply opcode must be the request opcode plus
     * one (see the REM_FILE_OP_* table).  Both bytes are zero-extended to a
     * longword before the compare. */
    if ((uint32_t)response_b[3] == (uint32_t)request_b[3] + 1) {
        /* 0x00E6146C: the server's own status is the longword at response+4. */
        *status_ret = *(const status_$t *)(response_b + 4);
    } else {
        *status_ret = file_$bad_reply_received_from_remote_node;  /* 0x00E61478 */
    }
    goto close_socket;

timer_fired:                                             /* 0x00E61482 */
    /* 0x00E61482-0x00E6149E: the wait also ends when the process is quit.
     * FIM_$QUIT_EC is a 12-byte-per-address-space array and the eventcount
     * value is its head longword. */
    if (quit_saved != FIM_$WIRED_DATA.quit_ec[PROC1_$AS_ID].value) {
        /* 0x00E614A0-0x00E614C8 */
        *status_ret = status_$fault_process_quit;        /* 0x00E614A4 */
        /* 0x00E614AA `bset.b #0x7,(A1)` sets bit 7 of the status's first
         * (most significant) byte, i.e. bit 31 of the longword. */
        *status_ret = (status_$t)((uint32_t)*status_ret | 0x80000000u);
        FIM_$WIRED_DATA.quit_value[PROC1_$AS_ID] = FIM_$WIRED_DATA.quit_ec[PROC1_$AS_ID].value;
        goto close_socket;
    }

    /* 0x00E614CA-0x00E614DC */
    retry_count = (int16_t)(retry_count + 12);
    if (conn_state == SEND_REQUEST_STATE_INITIAL) {
        conn_state = SEND_REQUEST_STATE_FIRST_TIMEOUT;   /* 0x00E614E0 */
        goto send_request;
    }
    if (conn_state != SEND_REQUEST_STATE_FIRST_TIMEOUT) {
        goto send_request;                               /* 0x00E614DC */
    }

    /* 0x00E614EA-0x00E6150C: after a second timeout ask whether the node is
     * answering anything at all.  PKT_$LIKELY_TO_ANSWER returns a Domain
     * boolean, so a non-negative result means "no". */
    if (PKT_$LIKELY_TO_ANSWER(addr_info, status_ret) >= 0) {
        *status_ret = status_$network_remote_node_failed_to_respond;
        goto close_socket;
    }
    conn_state = SEND_REQUEST_STATE_CONFIRMED;           /* 0x00E614FE */
    goto send_request;

close_socket:                                            /* 0x00E61512 */
    SOCK_$CLOSE((uint16_t)sock_num);
    *packet_id = (uint16_t)pkt_id;                       /* 0x00E61522 */

function_exit:                                           /* 0x00E61526 */
    return;
}
