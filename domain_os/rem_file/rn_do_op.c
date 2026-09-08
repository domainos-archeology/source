/*
 * rem_file/rn_do_op.c - REM_FILE_$RN_DO_OP (0x00E61538, 480 bytes)
 *
 * The remote-naming transport: it fills the caller's request record with the
 * process's security context, works out how the opcode's payload should
 * travel (inline in the request, or as the packet's bulk data), calls
 * REM_FILE_$SEND_REQUEST and copies the transport status into the reply.
 *
 * Re-emitted against the listing for bead source-0i5f, which found the 0x58
 * arm reading its length and pointer from the wrong halves of the record and
 * the two status tests reading a longword where the image reads a word.
 *
 * Frame: `link.w A6,-0x50` + `movem.l {A5 A3 A2 D2}` (0x10 bytes), so the
 * epilogue is `movem.l (-0x60,A6)` (0x00E6170E).  A5 is loaded here -
 * `lea (0xe823fc).l,A5` at 0x00E61540 - which is what gives
 * REM_FILE_$SEND_REQUEST its module base.
 *
 * Arguments:
 *   0x08 addr_info      long
 *   0x0C op_buf         long   (A1 throughout)
 *   0x10 base_len       word   (D2 at 0x00E61546)
 *   0x12 response_size  word
 *   0x14 response       long   (A0)
 *   0x18 received_len   long, forwarded to SEND_REQUEST's received_len
 *
 * Locals: -0x38 the 40-byte ACL_$GET_RE_ALL_SIDS output, -0x10 its 16-byte
 * second output, -0x4E packet_id, -0x4C extra_len, -0x4A bulk_max,
 * -0x48 bulk_len, -0x46 request_len, -0x44 status.
 */

#include "rem_file/rem_file_internal.h"
#include "arch/arch.h"
#include "os/os.h"

void REM_FILE_$RN_DO_OP(void *addr_info, void *op_buffer,
                        int16_t base_len, uint16_t response_size,
                        void *response_buf, uint16_t *received_len_out)
{
    rem_file_$rn_op_buf_t  *op_buf   = (rem_file_$rn_op_buf_t *)op_buffer;
    rem_file_$rn_op_resp_t *response = (rem_file_$rn_op_resp_t *)response_buf;

    uint8_t   re_all_sids[40];  /* A6-0x38 */
    uint8_t   re_sids_out[16];  /* A6-0x10 */
    int16_t   packet_id;        /* A6-0x4E */
    int16_t   extra_len;        /* A6-0x4C */
    int16_t   bulk_max;         /* A6-0x4A */
    int16_t   bulk_len;         /* A6-0x48 */
    int16_t   request_len;      /* A6-0x46 */
    status_$t local_status;     /* A6-0x44 */

    /* A2.  On the 0x58 "data fits inline" path (0x00E615F6-0x00E61600 falling
     * into the shared copy at 0x00E6164E) the original never loads A2, so the
     * value it hands REM_FILE_$SEND_REQUEST as `extra_data` is whatever its
     * caller left in the register.  extra_len is 0 on that path, and
     * PKT_$SEND_INTERNET only reads the data pointer when the length is
     * positive, so nothing dereferences it - but the store is genuinely
     * absent.  C cannot express "indeterminate", so the local starts as NULL;
     * the choice is not observable, but the missing store is real. */
    void     *extra_data = NULL; /* A2 */
    void     *bulk_data;        /* A3 */

    /* 0x00E6154A-0x00E6156C.  Five by-reference arguments; the last is the
     * reply's own status longword (response+0x04). */
    ACL_$GET_RE_ALL_SIDS(re_all_sids, op_buf->sids, re_sids_out,
                         op_buf->re_sids, &response->status);

    /* 0x00E61570-0x00E61578: `tst.w (0x6,A0)` tests only the LOW WORD of the
     * status longword at response+0x04. */
    if ((uint16_t)((uint32_t)response->status & 0xFFFFu) != 0) {
        return;
    }

    /* 0x00E6157C-0x00E61596.  `pea (0x18e,PC)` at 0x00E61588 (extension word
     * 0x00E6158A + 0x18E) is 0x00E61718 = &REM_FILE_$MAX_PROJ_LIST, the word
     * 8 - not a NULL and not a UID. */
    ACL_$GET_PROJ_LIST((uid_t *)(void *)op_buf->proj_list,
                       (int16_t *)&REM_FILE_$MAX_PROJ_LIST,
                       &op_buf->proj_count, &response->status);

    /* 0x00E6159A-0x00E615A2: the same low-word test. */
    if ((uint16_t)((uint32_t)response->status & 0xFFFFu) != 0) {
        return;
    }

    /* 0x00E615A6-0x00E615BA.  ACL_$IN_SUBSYS returns a Domain boolean, so a
     * negative result is "yes"; `bset.b #0x2,(0x21,A1)` is bit 2 of the byte
     * at record offset 0x21, i.e. re_sids[0x0D]. */
    if ((int8_t)ACL_$IN_SUBSYS() < 0) {   /* `tst.b D0b / bpl` at 0x00E615AC */
        op_buf->re_sids[0x0D] |= 0x04;
    }

    op_buf->magic = REM_FILE_REQ_MAGIC;             /* 0x00E615BE */
    request_len = base_len;                         /* 0x00E615C4 */

    /* --- how the outbound payload travels -------------------------------
     * 0x00E615C8-0x00E6166E.  Each arm sets request_len, extra_len and A2.
     */
    if (op_buf->op_code == REM_FILE_RN_OP_DIR_LIST) {          /* 0x58 */
        /* 0x00E615D4-0x00E615E6.  The length is the WORD at +0x92,
         * zero-extended, and base_len is sign-extended; the compare is a
         * signed longword compare against 0x122. */
        int32_t total = (int32_t)(uint32_t)op_buf->tail.list.data_len +
                        (int32_t)base_len;
        if (total > 0x122) {
            /* 0x00E61602-0x00E6160C: send it as the packet's data. */
            extra_len  = (int16_t)op_buf->tail.list.data_len;
            extra_data = ARCH_VA_TO_PTR(op_buf->tail.list.data_va);
        } else {
            /* 0x00E615E8-0x00E61600, joining the shared copy at 0x00E6164E:
             * append it to the request instead. */
            request_len = (int16_t)(base_len + (int16_t)op_buf->tail.list.data_len);
            OS_$DATA_COPY(ARCH_VA_TO_PTR(op_buf->tail.list.data_va),
                          op_buf->tail.list.inline_data,
                          (uint32_t)op_buf->tail.list.data_len);
            extra_len = 0;                          /* 0x00E61658 */
            /* A2 is deliberately not set here - see the declaration. */
        }
    } else if (op_buf->op_code == REM_FILE_RN_OP_DIR_GET_ENTRY) {  /* 0x3C */
        /* 0x00E6161A-0x00E6162C: both operands sign-extended, cap 0x108. */
        int32_t total = (int32_t)(int16_t)op_buf->tail.entry.data_len +
                        (int32_t)base_len;
        if (total > 0x108) {
            /* 0x00E6165E-0x00E61668 */
            extra_len  = (int16_t)op_buf->tail.entry.data_len;
            extra_data = ARCH_VA_TO_PTR(op_buf->tail.entry.data_va);
        } else {
            /* 0x00E6162E-0x00E6164C.  The copy target is the record plus the
             * word at +0x8E plus 0x96 - and A2, which the send is given as
             * extra_data, is left holding record + that word. */
            request_len = (int16_t)(base_len + (int16_t)op_buf->tail.entry.data_len);
            extra_data = (uint8_t *)op_buf + (int16_t)op_buf->tail.entry.dest_off;
            OS_$DATA_COPY(ARCH_VA_TO_PTR(op_buf->tail.entry.data_va),
                          (uint8_t *)extra_data + 0x96,
                          (uint32_t)(int32_t)(int16_t)op_buf->tail.entry.data_len);
            extra_len = 0;                          /* 0x00E61658 */
        }
    } else {
        /* 0x00E6166A-0x00E6166E */
        extra_len  = 0;
        extra_data = op_buf;
    }

    /* --- where the bulk reply lands -------------------------------------
     * 0x00E61670-0x00E616D0.  The opcode is re-read from the record each
     * time; the tests are a chain, not a switch.
     */
    if (op_buf->op_code == REM_FILE_RN_OP_DIR_LIST) {          /* 0x58 */
        bulk_data = ARCH_VA_TO_PTR(op_buf->tail.list.reply_va);  /* 0x00E61680 */
        bulk_max  = 0x400;                          /* 0x00E61684 */
    } else if (op_buf->op_code == REM_FILE_RN_OP_DIR_READ_DIR) { /* 0x42 */
        /* 0x00E61698-0x00E616AC.  `cmp.l (0x96,A1),D1 / bls` is an UNSIGNED
         * longword compare, and only the low word of the result is kept. */
        uint32_t limit = 0x400;
        bulk_data = ARCH_VA_TO_PTR(op_buf->tail.read_dir.reply_va);
        if (limit > op_buf->tail.read_dir.reply_max) {
            limit = op_buf->tail.read_dir.reply_max;
        }
        bulk_max = (int16_t)(uint16_t)limit;
    } else if (op_buf->op_code == REM_FILE_RN_OP_DIR_READ_LINK) { /* 0x3E */
        bulk_data = ARCH_VA_TO_PTR(op_buf->tail.entry.data_va);  /* 0x00E616BE */
        bulk_max  = (int16_t)op_buf->tail.entry.data_len;  /* 0x00E616C2 */
    } else {
        /* 0x00E616CA-0x00E616CE */
        bulk_max  = 0;
        bulk_data = response;
    }

    /* 0x00E616D2-0x00E61700: thirteen arguments, no result slot. */
    REM_FILE_$SEND_REQUEST(addr_info, op_buf, request_len,
                           extra_data, extra_len,
                           response, response_size,
                           received_len_out,
                           bulk_data, bulk_max,
                           &bulk_len, (uint16_t *)&packet_id,
                           &local_status);

    /* 0x00E61704-0x00E61708 */
    response->status = local_status;
}
