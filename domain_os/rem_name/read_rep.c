/*
 * rem_name/read_rep.c - REM_NAME_$READ_REP (0x00E4AB44, 232 bytes)
 *
 * Part of the REM_NAME module (SAU2 map: I 0xE4A408 size 0xB20,
 * D 0xE7DBB8 size 0x40; A5 = 0xE7DBB8, `lea (0xe7dbb8).l,A5` at 0x00E4AB4C).
 *
 * Read up to max_entries 0x12-byte replica records starting at start_index
 * from the name server on (net, node) through a NETBUF header page.  Unlike
 * READ_DIR, a "last entry in replicated root" status is carried on as is.
 *
 * Frame (A6+): 0x08 net, 0x0C node, 0x10 dir_uid, 0x14 start_index (WORD,
 * D2w), 0x16 rep_ret -> D5, 0x1A max_entries (word) -> D3w,
 * 0x1C count_ret -> A2, 0x20 status_ret -> D6.
 * Locals (A6-): -0x38 request (A3), -0x44 reply page VA (D4/D7), -0x48 its
 * physical address, -0x4E reply length word.
 */

#include "rem_name/rem_name_internal.h"

void REM_NAME_$READ_REP(uint32_t net, uint32_t node, uid_t *dir_uid,
                        uint16_t start_index, void *rep_ret,
                        uint16_t max_entries, uint16_t *count_ret,
                        status_$t *status_ret)
{
    rem_name_$read_req_t     request;               /* A6-0x38 */
    uint32_t                 page_phys;             /* A6-0x48 */
    uint32_t                 page_va;               /* A6-0x44 */
    int16_t                  reply_len;             /* A6-0x4E */
    rem_name_$read_reply_t  *reply;                 /* A1 */
    const rem_name_$rep_entry_t *src;               /* A4 = A0 + 6 */
    rem_name_$rep_entry_t   *dst;                   /* A1 */
    int16_t                  count;                 /* D0w */
    int16_t                  i;

    /* 0x00E4AB66-0x00E4AB7E */
    NETBUF_$GET_HDR(&page_phys, &page_va);
    reply = (rem_name_$read_reply_t *)ARCH_VA_TO_PTR(page_va);

    *count_ret = 0;                                             /* 0x00E4AB80 */

    /* 0x00E4AB82-0x00E4ABA2 */
    request.opcode   = REM_NAME_OP_READ_REP;
    request.dir_uid.high = dir_uid->high;
    request.dir_uid.low  = dir_uid->low;
    request.one      = 1;
    request.start_index = (uint32_t)start_index;

    /* 0x00E4ABA6-0x00E4ABCE: 0x36 bytes, opcode word 0x0E, 0x200-byte page */
    if (rem_name_$send_request(net, node, &request, 0x36, 0, 0x0E,
                               reply, REM_NAME_READ_REPLY_SIZE,
                               &reply_len, status_ret) >= 0) {
        /* 0x00E4ABD0-0x00E4ABD8 */
        if (*status_ret != status_$naming_last_entry_in_replicated_root_returned) {
            goto done;
        }
    }

    /* 0x00E4ABDA-0x00E4ABEA */
    if (reply->count == 0) {
        goto done;
    }
    count = (int16_t)(reply->count - 1);

    /* 0x00E4ABEC-0x00E4AC14: one 0x12-byte record per pass, source at
     * reply+0x12+6 stepping 0x12, destination rep_ret + (count-1)*0x12
     * (`lea (-0x12,A3,D1w*0x1),A1` with D1 = count*0x12). */
    src = (const rem_name_$rep_entry_t *)reply->data;
    do {
        /* `cmp.w (A2),D3w / bls`: stop once max_entries <= *count_ret */
        if (max_entries <= *count_ret) {
            goto done;
        }
        *count_ret = (uint16_t)(*count_ret + 1);                /* 0x00E4ABF0 */
        dst = (rem_name_$rep_entry_t *)rep_ret + (*count_ret - 1);
        for (i = 0; i < 4; i++) {
            dst->words[i] = src->words[i];                      /* 0x00E4AC06.. */
        }
        dst->tail = src->tail;                                  /* 0x00E4AC0E */
        src++;                                                  /* 0x00E4AC10 */
        count = (int16_t)(count - 1);                           /* 0x00E4AC14 dbf */
    } while (count != -1);

done:
    NETBUF_$RTN_HDR(&page_va);                                  /* 0x00E4AC18 */
}
