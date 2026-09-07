/*
 * MSG_$$RCV_INTERNAL - the shared receive body (0x00E59548)
 *
 * Pulls the next packet off a socket through APP_$RECEIVE, splits it into
 * the caller's template and data buffers, and reports the sender's address
 * and the message's protocol triple.
 *
 * Original address: 0x00E59548
 *
 * Assembly:
 *   00e59548  link.w A6,-0x40
 *   00e59550  movea.l (0x22,A6),A3      ; hw_addr
 *   00e59554  move.w (0x2e,A6),D2w      ; template_max
 *   00e59558  move.l (0x30,A6),D4       ; template_len_ret
 *   00e5955c  move.w (0x38,A6),D3w      ; data_max
 *   00e59560  movea.l (0x3a,A6),A4      ; data_len_ret
 *   00e59564  movea.l (0x46,A6),A2      ; status_ret
 *   00e59568  subq.l #0x2,SP / pea (A2) / pea (-0x30,A6) / move.w (0x8,A6)
 *   00e59574  jsr APP_$RECEIVE          ; fills app_$receive_rec_t at -0x30
 *   00e5957e  tst.l (A2) / bne -> return
 *   00e59584  move.l (-0x2c,A6),D0      ; rec.data
 *   00e59588  movea.l (-0x30,A6),A2     ; rec.reply
 *   00e5958c..00e595c0  the six address out-parameters and msg_type
 *   00e595c4..00e595ee  the msg_$hw_addr_t record
 *   00e595f6  cmpi.w #0x2,D5w / cmpi.w #0x29,D6w
 *   00e59602..00e59618  peel the 16-byte internet address off the template
 *   00e5961c  cmp.w (0x2,A2),D2w / bls  ; clamp to template_max (unsigned)
 *   00e59628  move.w D2w,(A0)           ; *template_len_ret
 *   00e59638  jsr OS_$DATA_COPY(rec.data, template, len)
 *   00e59642  move.l (-0x2c,A6),D2 / andi.w #-0x400,D2w   ; the netbuf page
 *   00e59650  move.w (0x3e0,A0),(A1)    ; *ec_param1_ret
 *   00e59658  move.w (0x3e2,A0),(A3)    ; *ec_param2_ret
 *   00e5965c  tst.l (-0x28,A6) / beq    ; no payload pages -> *data_len_ret=0
 *   00e59666  cmp.w (0x4,A2),D3w / bls  ; clamp to data_max (unsigned)
 *   00e5967e  jsr PKT_$DAT_COPY(rec.data_pages, len, data)
 *   00e59692  jsr PKT_$DUMP_DATA(rec.data_pages, reply->data_len)
 *   00e5969e  jsr NETBUF_$RTN_HDR(&netbuf_page)
 *
 * Note that the header is returned on the no-payload path too: the branch at
 * 0x00E59664 jumps to 0x00E5969A, which is the NETBUF_$RTN_HDR call.  Only
 * an APP_$RECEIVE failure skips it.
 */

#include "msg/msg_internal.h"
#include "app/app.h"
#include "os/os.h"

void MSG_$$RCV_INTERNAL(uint16_t socket,
                        uint32_t *dest_net, uint32_t *dest_node,
                        uint16_t *dest_sock,
                        uint32_t *src_net, uint32_t *src_node,
                        uint16_t *src_sock,
                        msg_$hw_addr_t *hw_addr, uint16_t *msg_type,
                        void *template, uint16_t template_max,
                        uint16_t *template_len_ret,
                        void *data, uint16_t data_max,
                        uint16_t *data_len_ret,
                        uint16_t *ec_param1_ret, uint16_t *ec_param2_ret,
                        status_$t *status_ret)
{
    app_$receive_rec_t rec;         /* A6-0x30 */
    uint32_t netbuf_page;           /* A6-0x3C */
    const msg_$reply_hdr_t *reply;  /* A2 */
    const uint8_t *payload;         /* D0, then A1 */
    const uint8_t *netbuf;
    uint16_t template_len;          /* D2 */
    uint16_t data_len;              /* D3 */
    uint16_t proto_type;            /* D5 */
    uint16_t proto_subtype;         /* D6 */
    int16_t i;

    /* 0xE59574 */
    APP_$RECEIVE(socket, &rec, status_ret);

    /* 0xE5957E: an empty queue leaves every out-parameter untouched */
    if (*status_ret != status_$ok) {
        return;
    }

    payload = (const uint8_t *)rec.data;             /* 0xE59584 */
    reply = (const msg_$reply_hdr_t *)rec.reply;     /* 0xE59588 */

    /* 0xE5958C - 0xE595C0 */
    *dest_net  = rec.hdr_f06;
    *src_net   = rec.hdr_f12;
    *dest_node = reply->dest_node;
    *dest_sock = reply->dest_sock;
    *src_node  = reply->src_node;
    *src_sock  = reply->src_sock;
    *msg_type  = reply->msg_type;

    /* 0xE595C4 - 0xE595EE: the hardware-address record */
    hw_addr->proto_family = reply->proto_family;
    hw_addr->flags =
        (uint16_t)((rec.flags_lo & MSG_HW_FLAGS_MASK) >> MSG_HW_FLAGS_SHIFT);
    proto_type = reply->proto_type;
    hw_addr->proto_type = proto_type;
    proto_subtype = reply->proto_subtype;
    hw_addr->proto_subtype = proto_subtype;

    /*
     * 0xE595EE: "move.l #0xffff,(0xa,A3)" writes reserved2 and reserved3 as
     * one longword, so reserved2 becomes 0 and reserved3 0xFFFF.  reserved1
     * at +0x08 is left alone.
     */
    hw_addr->reserved2 = 0x0000;
    hw_addr->reserved3 = 0xFFFF;

    /*
     * 0xE595F6 - 0xE59618: an internet message carries its 16-byte address
     * in front of the template.  The address is moved into the record and
     * the template shrinks by 0x10 - note that this writes back into the
     * REPLY header (`sub.w D1w,(0x2,A2)`), not into a local.
     */
    if (proto_type == MSG_PROTO_TYPE_INET &&
        proto_subtype == MSG_PROTO_SUBTYPE_INET) {
        for (i = 0; i <= 0xF; i++) {         /* moveq #0xf + dbf = 16 */
            hw_addr->inet_addr[i] = payload[i];
        }
        ((msg_$reply_hdr_t *)reply)->template_len =
            (uint16_t)(reply->template_len - 0x10);
        payload += 0x10;
        rec.data = (void *)payload;          /* 0xE59618 add.l D1,(-0x2c,A6) */
    }

    /* 0xE5961C: bls, so the compare is unsigned */
    template_len = template_max;
    if (template_len > reply->template_len) {
        template_len = reply->template_len;
    }
    *template_len_ret = template_len;                    /* 0xE59628 */

    /* 0xE59638 */
    OS_$DATA_COPY((const char *)rec.data, (char *)template,
                  (uint32_t)template_len);

    /*
     * 0xE59642: the netbuf page is the CURRENT rec.data rounded down to 1KB
     * - the original masks only the low word, which clears bits 0..9.
     */
    netbuf_page = ARCH_PTR_TO_VA(rec.data) & 0xFFFFFC00u;
    netbuf = (const uint8_t *)ARCH_VA_TO_PTR(netbuf_page);

    *ec_param1_ret = *(const uint16_t *)(netbuf + NETBUF_HDR_EC_PARAM1);
    *ec_param2_ret = *(const uint16_t *)(netbuf + NETBUF_HDR_EC_PARAM2);

    /* 0xE5965C: no payload pages at all */
    if (rec.data_pages[0] == 0) {
        *data_len_ret = 0;
    } else {
        /* 0xE59666: bls, unsigned */
        data_len = data_max;
        if (data_len > reply->data_len) {
            data_len = reply->data_len;
        }
        *data_len_ret = data_len;                        /* 0xE59670 */

        /* 0xE5967E */
        PKT_$DAT_COPY(rec.data_pages, (int16_t)data_len, (char *)data);

        /* 0xE59692: the FULL payload length, not the clamped one */
        PKT_$DUMP_DATA(rec.data_pages, (int16_t)reply->data_len);
    }

    /* 0xE5969E: reached on both paths */
    NETBUF_$RTN_HDR(&netbuf_page);
}
