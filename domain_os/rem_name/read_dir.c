/*
 * rem_name/read_dir.c - REM_NAME_$READ_DIR (0x00E4A984, 448 bytes)
 *
 * Part of the REM_NAME module (SAU2 map: I 0xE4A408 size 0xB20,
 * D 0xE7DBB8 size 0x40; A5 = 0xE7DBB8, `lea (0xe7dbb8).l,A5` at 0x00E4A98C).
 *
 * Read up to max_entries directory entries starting at start_index from the
 * name server on (net, node) into a NETBUF header page (0x200 bytes), then
 * unpack the variable-length wire entries into 0x30-byte
 * rem_name_$dir_entry_t records.  The page is returned on every exit.
 *
 * Frame (A6+): 0x08 net, 0x0C node, 0x10 dir_uid, 0x14 start_index (WORD,
 * D2w), 0x16 entries_ret -> A4, 0x1A max_entries (word) -> D4w,
 * 0x1C count_ret -> A2, 0x20 status_ret -> D3.
 * Locals (A6-): -0x38 request (0x36 bytes sent, D6), -0x54 reply page VA
 * (D5/A3), -0x58 its physical address, -0x5A reply length word,
 * -0x5C the wire entry type word (its address is parked in -0x78).
 * D2 = wire cursor, D3w = dbf count (reply count - 1).
 */

#include "rem_name/rem_name_internal.h"

void REM_NAME_$READ_DIR(uint32_t net, uint32_t node, uid_t *dir_uid,
                        uint16_t start_index, void *entries_ret,
                        uint16_t max_entries, uint16_t *count_ret,
                        status_$t *status_ret)
{
    rem_name_$read_req_t    request;                /* A6-0x38 */
    uint32_t                page_phys;              /* A6-0x58 */
    uint32_t                page_va;                /* A6-0x54 */
    int16_t                 reply_len;              /* A6-0x5A */
    int16_t                 wire_type;              /* A6-0x5C */
    rem_name_$read_reply_t *reply;                  /* A3 */
    const uint8_t          *src;                    /* D2 */
    rem_name_$dir_entry_t  *entry;                  /* A3 - 0x30 */
    int16_t                 count;                  /* D3w */
    uint16_t                pad_from;               /* D0w */
    int16_t                 pad_count;              /* D1w */
    uint32_t                name_len;               /* D1 zero-extended */

    /* 0x00E4A9A2-0x00E4A9B2 */
    NETBUF_$GET_HDR(&page_phys, &page_va);
    reply = (rem_name_$read_reply_t *)ARCH_VA_TO_PTR(page_va);

    *count_ret = 0;                                             /* 0x00E4A9BE */

    /* 0x00E4A9C0-0x00E4A9E0 */
    request.opcode   = REM_NAME_OP_READ_DIR;
    request.dir_uid.high = dir_uid->high;
    request.dir_uid.low  = dir_uid->low;
    request.one      = 1;
    request.start_index = (uint32_t)start_index;

    /* 0x00E4A9E4-0x00E4AA0C: 0x36 bytes, opcode word 0x0C, 0x200-byte page */
    if (rem_name_$send_request(net, node, &request, 0x36, 0, 0x0C,
                               reply, REM_NAME_READ_REPLY_SIZE,
                               &reply_len, status_ret) >= 0) {
        /* 0x00E4AA0E-0x00E4AA20: only "last entry in replicated root" goes
         * on; its status is cleared when the reply holds MORE entries than
         * were asked for (`cmp.w (0x16,A3),D4w / bcc`). */
        if (*status_ret != status_$naming_last_entry_in_replicated_root_returned) {
            goto done;
        }
        if (max_entries < reply->count) {
            *status_ret = status_$ok;
        }
    }

    /* 0x00E4AA22-0x00E4AA36 */
    src = reply->data;
    if (reply->count == 0) {
        goto done;
    }
    count = (int16_t)(reply->count - 1);

    /* 0x00E4AA40-0x00E4AB2C: one wire entry per pass */
    do {
        /* `cmp.w (A2),D4w / bls`: stop once max_entries <= *count_ret */
        if (max_entries <= *count_ret) {
            goto done;
        }
        *count_ret = (uint16_t)(*count_ret + 1);                /* 0x00E4AA46 */
        /* 0x00E4AA48-0x00E4AA58: A3 = entries_ret + count*0x30, the END of
         * the record being filled */
        entry = (rem_name_$dir_entry_t *)entries_ret + (*count_ret - 1);

        /* 0x00E4AA5C-0x00E4AA72: type word into the local, cursor += 2 */
        OS_$DATA_COPY(src, &wire_type, 2);
        src += 2;
        /* 0x00E4AA74-0x00E4AA8A: name length into the record, cursor += 2 */
        OS_$DATA_COPY(src, &entry->name_len, 2);
        src += 2;
        /* 0x00E4AA8C-0x00E4AA9E: the name bytes (longword count) */
        name_len = entry->name_len;
        OS_$DATA_COPY(src, entry->name, name_len);

        /* 0x00E4AAA2-0x00E4AAC0: pad with spaces.  D0 = name_len + 1; when
         * D0 <= 0x20 (unsigned) store 0x20 - D0 + 1 spaces at name[D0-1..]. */
        pad_from = (uint16_t)(entry->name_len + 1);
        if (pad_from <= 0x20) {
            pad_count = (int16_t)(0x20 - pad_from);
            do {
                ((uint8_t *)entry)[3 + pad_from] = 0x20;    /* (-0x2d,A3,D7) */
                pad_from = (uint16_t)(pad_from + 1);
                pad_count = (int16_t)(pad_count - 1);
            } while (pad_count != -1);
        }

        /* 0x00E4AAC4-0x00E4AAD4 */
        if (wire_type == ENTRY_TYPE_NORMAL) {
            /* 0x00E4AAD6-0x00E4AB10: type 1, skip the name, uid + extra */
            entry->type = 1;
            src += name_len;
            OS_$DATA_COPY(src, &entry->uid, 8);
            src += 8;
            OS_$DATA_COPY(src, &entry->extra, 4);
            src += 4;
        } else if (wire_type == ENTRY_TYPE_LINK) {
            /* 0x00E4AB12-0x00E4AB26: type 3, skip the name only */
            entry->type = ENTRY_TYPE_LINK_ALT;
            src += name_len;
        } else {
            /* 0x00E4AB28: unknown - give the record back and stop */
            *count_ret = (uint16_t)(*count_ret - 1);
            goto done;
        }

        count = (int16_t)(count - 1);                           /* 0x00E4AB2C dbf */
    } while (count != -1);

done:
    NETBUF_$RTN_HDR(&page_va);                                  /* 0x00E4AB30 */
}
