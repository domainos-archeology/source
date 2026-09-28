/*
 * rem_name/dir_readu.c - REM_NAME_$DIR_READU (0x00E4AC2C, 236 bytes)
 *
 * Part of the REM_NAME module (SAU2 map: I 0xE4A408 size 0xB20,
 * D 0xE7DBB8 size 0x40; A5 = 0xE7DBB8, `lea (0xe7dbb8).l,A5` at 0x00E4AC34).
 *
 * Reads directory entries from the current name server in as many
 * REM_NAME_$READ_DIR round trips as it takes to fill *max_entries, locating
 * a server once if the very first read (continuation index 1) fails.
 *
 * Frame (A6+): 0x08 dir_uid -> D3, 0x0C entries_ret -> D4,
 * 0x10 continuation -> A4 (a longword whose LOW word is the entry index),
 * 0x14 max_entries -> D5, 0x18 count_ret -> A3, 0x1C status_ret -> A2.
 * Locals (A6-): -0x4 net (copy of A5+0x34), -0x8 node (copy of A5+0x30),
 * -0xA entries_read (word).  D2b = "a read succeeded / server located".
 */

#include "rem_name/rem_name_internal.h"

void REM_NAME_$DIR_READU(uid_t *dir_uid, void *entries_ret, int32_t *continuation,
                         uint16_t *max_entries, uint16_t *count_ret,
                         status_$t *status_ret)
{
    uint32_t net;                           /* A6-0x4 */
    uint32_t node;                          /* A6-0x8 */
    int16_t  entries_read;                  /* A6-0xA */
    boolean  progressed;                    /* D2b */
    status_$t st;

    *count_ret = 0;                                         /* 0x00E4AC52 */

    /* 0x00E4AC54-0x00E4AC5E: nothing wanted, or a zero continuation ->
     * 0x00E4ACFE: continuation and status both cleared. */
    if (*max_entries == 0 || *continuation == 0) {
        goto finished;
    }

    progressed = 0;                                         /* 0x00E4AC62 */
    net  = rem_name_$data.curr_net;                         /* 0x00E4AC64 (0x34,A5) */
    node = rem_name_$data.curr_node;                        /* 0x00E4AC6A (0x30,A5) */

    /* 0x00E4AD04-0x00E4AD0A: `cmp.w (A0),D0w / bcs` - while count < max
     * (unsigned); the test runs first (`bra.w 0x00E4AD04` at 0x00E4AC70). */
    while (*count_ret < *max_entries) {
        /* 0x00E4AC74-0x00E4ACA6: REM_NAME_$READ_DIR(net, node, dir_uid,
         * LOW word of *continuation (`move.w (0x2,A4)`),
         * entries_ret + count*0x30 (`lsl.l #4` then *3), max - count,
         * &entries_read, status_ret) */
        REM_NAME_$READ_DIR(net, node, dir_uid,
                           (uint16_t)((uint32_t)*continuation & 0xFFFFu),
                           (uint8_t *)entries_ret + ((uint32_t)*count_ret * DIR_ENTRY_SIZE),
                           (uint16_t)(*max_entries - *count_ret),
                           (uint16_t *)&entries_read,
                           status_ret);

        if (*status_ret == status_$ok) {                    /* 0x00E4ACAE */
            /* 0x00E4ACB2-0x00E4ACB8: `add.w D0w,(0x2,A4)` bumps the LOW
             * word of the continuation in place. */
            progressed = (boolean)-1;
            *continuation = (int32_t)(((uint32_t)*continuation & 0xFFFF0000u) |
                                      (((uint32_t)*continuation + (uint16_t)entries_read) & 0xFFFFu));
            *count_ret = (uint16_t)(*count_ret + entries_read);
            continue;
        }

        /* 0x00E4ACBC-0x00E4ACD2: end of a replicated root - accept what came
         * back, clear the continuation, report ok. */
        st = *status_ret;
        if (st == status_$naming_cannot_find_entry_in_replicated_root ||
            st == status_$naming_last_entry_in_replicated_root_returned) {
            *continuation = 0;
            *count_ret = (uint16_t)(*count_ret + entries_read);
            *status_ret = status_$ok;                       /* 0x00E4AD00 */
            return;
        }

        /* 0x00E4ACD4-0x00E4ACDE: any other failure after progress, or not
         * on the first entry, ends the read quietly. */
        if (progressed < 0 ||
            (uint16_t)((uint32_t)*continuation & 0xFFFFu) != 1) {
            goto finished;
        }

        /* 0x00E4ACE0-0x00E4ACFC: locate a server and retry from it */
        LOCATE_SERVER(&node, &net, status_ret);
        if (*status_ret != status_$ok) {
            *continuation = 0;                              /* 0x00E4ACF6 */
            return;                                         /* status kept */
        }
        progressed = (boolean)-1;                           /* 0x00E4ACFA */
    }
    return;                                                 /* 0x00E4AD0E */

finished:
    *continuation = 0;                                      /* 0x00E4ACFE */
    *status_ret = status_$ok;                               /* 0x00E4AD00 */
}
