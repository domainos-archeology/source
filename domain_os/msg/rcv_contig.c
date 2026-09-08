/*
 * MSG_$RCV_CONTIG, MSG_$RCV_CONTIGI - Receive a message into one buffer
 *
 * The same receive as MSG_$RCVI, except that the template and the payload
 * are concatenated into a single caller buffer instead of two, so the body
 * is inlined here rather than shared with MSG_$$RCV_INTERNAL.
 *
 * Original addresses:
 *   MSG_$RCV_CONTIG:  0x00E59756 (80 bytes)
 *   MSG_$RCV_CONTIGI: 0x00E597A6 (426 bytes)
 */

#include "msg/msg_internal.h"
#include "app/app.h"
#include "os/os.h"

/*
 * MSG_$RCV_CONTIGI
 *
 * Assembly:
 *   00e597ae  lea (0xe80d84).l,A5       ; MSG_$DATA
 *   00e597b4  movea.l (0x8,A6),A4       ; socket
 *   00e597b8  movea.l (0x24,A6),A3      ; hw_addr
 *   00e597bc  move.l (0x30,A6),D3       ; max_len
 *   00e597c0  move.l (0x34,A6),D4       ; data_len
 *   00e597c4  movea.l (0x38,A6),A2      ; status_ret
 *   00e597c8  move.w (A4),D0w / ble / cmpi.w #0xe0,D0w / ble
 *   00e597d2  0x290001 socket out of range
 *   00e597dc..00e597f2  the 1-based ownership bitmap test
 *   00e597f8  0x290005 no owner
 *   00e5980c  jsr APP_$RECEIVE(*socket, &rec, status_ret)
 *   00e59816  tst.l (A2) / bne -> return
 *   00e5981c  movea.l (-0x2c,A6),A0     ; rec.data
 *   00e59820  movea.l (-0x30,A6),A2     ; rec.reply  - A2 is REUSED here, so
 *                                       ; status_ret is no longer addressable
 *   00e59824..00e59858  the six address out-parameters and msg_type
 *   00e5985c..00e59886  the msg_$hw_addr_t record
 *   00e5988e  cmpi.w #0x2 / cmpi.w #0x29  ; the internet-address special case
 *   00e598b4  cmp.w (A1),D0w / bls      ; clamp the template to *max_len
 *   00e598c2  move.w D0w,(A4)           ; *data_len = that length
 *   00e598d2  jsr OS_$DATA_COPY(rec.data, data_buf, len)
 *   00e598dc  tst.l (-0x28,A6) / beq    ; no payload pages -> skip
 *   00e598e2..00e598fe  available = min(*max_len - copied, clamped data_len)
 *   00e59912  jsr PKT_$DAT_COPY(rec.data_pages, available,
 *                                data_buf + *data_len)
 *   00e59926  jsr PKT_$DUMP_DATA(rec.data_pages, reply->prefix.data_len)
 *   00e5992e  add.w D2w,(A4)            ; *data_len += available
 *   00e59930..00e59940  NETBUF_$RTN_HDR(rec.data rounded down to 1KB)
 */
void MSG_$RCV_CONTIGI(msg_$socket_t *socket,
                      uint32_t *dest_net,
                      uint32_t *dest_node,
                      uint16_t *dest_sock,
                      uint32_t *src_net,
                      uint32_t *src_node,
                      uint16_t *src_sock,
                      msg_$hw_addr_t *hw_addr,
                      uint16_t *msg_type,
                      char *data_buf,
                      uint16_t *max_len,
                      uint16_t *data_len,
                      status_$t *status_ret)
{
    int16_t sock_num;
    uint16_t asid;
    uint16_t byte_index;
    const uint8_t *bitmap;

    app_$receive_rec_t rec;         /* A6-0x30 */
    uint32_t netbuf_page;           /* A6-0x38 */
    const msg_$reply_hdr_t *reply;  /* A2 */
    const uint8_t *payload;         /* A0 */
    uint16_t proto_type;            /* D0 */
    uint16_t proto_subtype;         /* D2 */
    uint16_t copy_len;              /* D0, then D2 */
    uint16_t overflow_len;
    int32_t available;              /* D2 */
    int16_t i;

    sock_num = *socket;

    /* 0xE597C8 */
    if (sock_num <= 0 || sock_num > MSG_MAX_SOCKET) {
        *status_ret = status_$msg_socket_out_of_range;
        return;
    }

    /* 0xE597DC - 0xE597F2 */
    asid = PROC1_$AS_ID;
    bitmap = MSG_$SOCK_OWNERS[sock_num];
    byte_index = (uint16_t)((0x3Fu - asid) >> 3);

    if ((bitmap[byte_index] & (1u << (asid & 7))) == 0) {
        *status_ret = status_$msg_no_owner;
        return;
    }

    /* 0xE5980C: the socket number is re-read from the caller's word */
    APP_$RECEIVE((uint16_t)*socket, &rec, status_ret);

    if (*status_ret != status_$ok) {
        return;
    }

    payload = (const uint8_t *)ARCH_VA_TO_PTR(rec.data);   /* 0xE5981C */
    reply = (const msg_$reply_hdr_t *)ARCH_VA_TO_PTR(rec.reply); /* 0xE59820 */

    /* 0xE59824 - 0xE59858 */
    *dest_net  = rec.hdr_f06;
    *src_net   = rec.hdr_f12;
    *dest_node = reply->dest_node;
    *dest_sock = reply->dest_sock;
    *src_node  = reply->src_node;
    *src_sock  = reply->src_sock;
    *msg_type  = reply->prefix.request_id;

    /*
     * 0xE5985C - 0xE59886.  Note the order differs from
     * MSG_$$RCV_INTERNAL's - proto_subtype is stored before proto_type -
     * but the result is identical.
     */
    hw_addr->proto_family = reply->proto_family;
    hw_addr->flags =
        (uint16_t)((rec.flags_lo & MSG_HW_FLAGS_MASK) >> MSG_HW_FLAGS_SHIFT);
    proto_subtype = reply->proto_subtype;
    hw_addr->proto_subtype = proto_subtype;
    proto_type = reply->proto_type;
    hw_addr->proto_type = proto_type;
    hw_addr->reserved2 = 0x0000;    /* one "move.l #0xffff,(0xa,A3)" */
    hw_addr->reserved3 = 0xFFFF;

    /* 0xE5988E - 0xE598B0 */
    if (proto_type == MSG_PROTO_TYPE_INET &&
        proto_subtype == MSG_PROTO_SUBTYPE_INET) {
        for (i = 0; i <= 0xF; i++) {
            hw_addr->inet_addr[i] = payload[i];
        }
        ((msg_$reply_hdr_t *)reply)->prefix.template_len =
            (uint16_t)(reply->prefix.template_len - 0x10);
        payload += 0x10;
        rec.data = ARCH_PTR_TO_VA(payload);
    }

    /* 0xE598B4: bls, unsigned */
    copy_len = reply->prefix.template_len;
    if (copy_len > *max_len) {
        copy_len = *max_len;
    }
    *data_len = copy_len;                            /* 0xE598C2 */

    /* 0xE598D2 */
    OS_$DATA_COPY((const char *)ARCH_VA_TO_PTR(rec.data), data_buf,
                  (uint32_t)copy_len);

    /* 0xE598DC */
    if (rec.data_pages[0] != 0) {
        /* 0xE598E2: clamp the payload length to *max_len as well */
        overflow_len = reply->prefix.data_len;
        if (overflow_len > *max_len) {
            overflow_len = *max_len;
        }

        /*
         * 0xE598EE - 0xE598FE: available = *max_len - copy_len, then
         * min()-ed against the clamped payload length.  Both are longword
         * quantities and the compare is SIGNED, so an over-long template
         * leaves a negative budget and PKT_$DAT_COPY is asked for a
         * negative count.  Reproduced.
         */
        available = (int32_t)(uint32_t)*max_len - (int32_t)(uint32_t)copy_len;
        if (available > (int32_t)(uint32_t)overflow_len) {
            available = (int32_t)(uint32_t)overflow_len;
        }

        /* 0xE59912: the destination advances by the word already stored */
        PKT_$DAT_COPY(rec.data_pages, (int16_t)available,
                      data_buf + *data_len);

        /* 0xE59926: the FULL payload length, not the clamped one */
        PKT_$DUMP_DATA(rec.data_pages, (int16_t)reply->prefix.data_len);

        *data_len = (uint16_t)(*data_len + (uint16_t)available);
    }

    /* 0xE59930 */
    netbuf_page = rec.data & 0xFFFFFC00u;
    NETBUF_$RTN_HDR(&netbuf_page);
}

/*
 * MSG_$RCV_CONTIG - the short form.
 *
 * Assembly:
 *   00e5975c  movea.l (0x1c,A6),A0 / movea.l (A0),A2
 *                                       ; the buffer is reached through a
 *                                       ; pointer CELL, not passed directly
 *   00e59774  pea (-0x20,A6)            ; the msg_$hw_addr_t local
 *   00e59780  pea (-0x28,A6)            ; src_net   (discarded)
 *   00e59784  pea (-0x32,A6)            ; dest_sock (discarded)
 *   00e59788  pea (-0x2c,A6)            ; dest_node (discarded)
 *   00e5978c  pea (-0x30,A6)            ; dest_net  (discarded)
 *   00e59794  bsr MSG_$RCV_CONTIGI
 *   00e59796  movea.l (0x14,A6),A0 / move.w (-0x20,A6),(A0)
 */
void MSG_$RCV_CONTIG(msg_$socket_t *socket,
                     uint32_t *src_node,
                     uint16_t *src_sock,
                     uint16_t *proto_family_ret,
                     uint16_t *msg_type,
                     char **data_buf_ptr,
                     uint16_t *max_len,
                     uint16_t *data_len,
                     status_$t *status_ret)
{
    uint32_t dest_net;              /* A6-0x30 */
    uint32_t dest_node;             /* A6-0x2C */
    uint16_t dest_sock;             /* A6-0x32 */
    uint32_t src_net;               /* A6-0x28 */
    msg_$hw_addr_t hw_addr;         /* A6-0x20 */

    MSG_$RCV_CONTIGI(socket,
                     &dest_net, &dest_node, &dest_sock,
                     &src_net, src_node, src_sock,
                     &hw_addr, msg_type,
                     *data_buf_ptr,     /* 0xE59760 movea.l (A0),A2 */
                     max_len, data_len,
                     status_ret);

    /* 0xE5979A: only the record's first word survives */
    *proto_family_ret = hw_addr.proto_family;
}
