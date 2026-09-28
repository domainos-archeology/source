/*
 * rem_name/send_request.c - rem_name_$send_request (0x00E4A4C8, 188 bytes)
 *
 * Part of the REM_NAME module (SAU2 map: I 0xE4A408 size 0xB20,
 * D 0xE7DBB8 size 0x40).  No symbol of its own in the map; A5 (= 0xE7DBB8)
 * is inherited from the REM_NAME_$* caller.
 *
 * The module's RPC helper: copies the 15-word request template from the
 * module block, ORs `flags` into its first word, sends it with
 * PKT_$SAR_INTERNET to socket 10 on (net, node), and validates the reply.
 * Returns 0xFF and clears *status_ret when the reply is acceptable; on any
 * failure stores the status (the transport's, or the reply's own at +0x0E)
 * in both *status_ret and rem_name_$data.last_status and returns 0.
 *
 * Frame (A6+): 0x08 net, 0x0C node, 0x10 request, 0x14 req_size (word),
 * 0x16 flags (word), 0x18 opcode (word) - callers push the last two as one
 * longword, e.g. `pea (0x2).w` = flags 0 / opcode 2 and `move.l #0x80001e`
 * = flags 0x80 / opcode 0x1E - 0x1A response -> A4, 0x1E resp_size (word),
 * 0x20 resp_len_ret -> A3, 0x24 status_ret -> A2.
 * Locals (A6-): -0x48 the 30-byte template copy, -0x28 PKT_$SAR_INTERNET's
 * result record, -0x4C its 4-byte data-length cell, -0x50 transport status,
 * -0x52 reply data length word.
 */

#include "rem_name/rem_name_internal.h"

/*
 * 0x00E4A584: the four zero bytes PKT_$SAR_INTERNET is handed as the
 * (empty, length 0) request data block: `pea (0x6a,PC)` at 0x00E4A518,
 * PC = 0x00E4A51A.  Image bytes `00 00 00 00`.
 */
static const uint8_t rem_name_$empty_req_data_00e4a584[4] = { 0, 0, 0, 0 };

boolean rem_name_$send_request(uint32_t net, uint32_t node, void *request,
                               int16_t req_size, int16_t flags, int16_t opcode,
                               void *response, int16_t resp_size,
                               int16_t *resp_len_ret, status_$t *status_ret)
{
    uint16_t            config[15];         /* A6-0x48 */
    pkt_$sar_result_t   sar_result;         /* A6-0x28 */
    uint8_t             data_len_out[4];    /* A6-0x4C */
    status_$t           xport_status;       /* A6-0x50 */
    uint16_t            data_len_word;      /* A6-0x52 */
    boolean             ok;                 /* D2b */
    const rem_name_$reply_hdr_t *hdr = (const rem_name_$reply_hdr_t *)response;
    int16_t             i;

    ok = 0;                                                     /* 0x00E4A4E4 */

    /* 0x00E4A4E6-0x00E4A4F6: seven longwords + one word = 15 words */
    for (i = 0; i < 15; i++) {
        config[i] = rem_name_$data.config[i];
    }
    config[0] = (uint16_t)(config[0] | (uint16_t)flags);        /* 0x00E4A4F8 */

    /* 0x00E4A4FC-0x00E4A53E: seventeen pushes, last push = argument 1 */
    PKT_$SAR_INTERNET(net, node, REM_NAME_$SOCK, config,
                      rem_name_$data.service_delay,             /* (0x38,A5) */
                      request, (uint16_t)req_size,
                      (void *)rem_name_$empty_req_data_00e4a584, 0,
                      &sar_result,
                      response, (uint16_t)resp_size, (uint16_t *)resp_len_ret,
                      data_len_out, 0, &data_len_word,
                      &xport_status);

    /* 0x00E4A542-0x00E4A54E: transport failure */
    if (xport_status != status_$ok) {
        *status_ret = xport_status;
        rem_name_$data.last_status = xport_status;
        goto done;
    }

    /* 0x00E4A550 `cmpi.w #0x12,(A3) / blt` (signed): too short.
     * 0x00E4A556-0x00E4A562: the sign-extended opcode argument against the
     * zero-extended reply word at +0x02 (`ext.l D0` vs `clr.l D4 / move.w`).
     * 0x00E4A564: the reply's own status at +0x0E must be zero. */
    if (*resp_len_ret < 0x12 ||
        (int32_t)opcode != (int32_t)(uint32_t)hdr->opcode ||
        hdr->status != status_$ok) {
        /* 0x00E4A56A-0x00E4A572 */
        *status_ret = hdr->status;
        rem_name_$data.last_status = *status_ret;
        goto done;
    }

    /* 0x00E4A574-0x00E4A576 */
    *status_ret = status_$ok;
    ok = (boolean)-1;

done:
    return ok;                                                  /* 0x00E4A578 */
}
