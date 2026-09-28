/*
 * rem_name/get_entry.c - REM_NAME_$GET_ENTRY (0x00E4AD18, 190 bytes)
 *
 * Part of the REM_NAME module (SAU2 map: I 0xE4A408 size 0xB20,
 * D 0xE7DBB8 size 0x40; A5 = 0xE7DBB8, `lea (0xe7dbb8).l,A5` at 0x00E4AD20).
 *
 * Look up an entry by name on the current name server, locating a server
 * first if the last request failed, and retrying once from a freshly located
 * server on any error other than "not found" / "invalid pathname".
 *
 * Frame (A6+): 0x08 dir_uid -> D3, 0x0C name -> D4, 0x10 name_len -> A4
 * (pointer; the word is pushed by value, `move.w (A4),-(SP)` 0x00E4AD6E),
 * 0x14 entry_ret -> A3, 0x18 status_ret -> A2.
 * Locals (A6-): -0x8 node, -0x4 net.  D2b = "server already located".
 * The `subq.l #0x2,SP` before each REM_NAME_$GET_ENTRY_BY_NAME push list
 * (0x00E4AD68, 0x00E4ADB4) pads the 26 bytes of arguments to keep SP
 * longword aligned; the callee is a procedure and the word is never read.
 */

#include "rem_name/rem_name_internal.h"

void REM_NAME_$GET_ENTRY(uid_t *dir_uid, char *name, uint16_t *name_len,
                         void *entry_ret, status_$t *status_ret)
{
    uint32_t node;                          /* A6-0x8 */
    uint32_t net;                           /* A6-0x4 */
    boolean  located;                       /* D2b */
    status_$t st;

    located = 0;                                            /* 0x00E4AD3A */

    /* 0x00E4AD3C `tst.l (0x2c,A5)` */
    if (rem_name_$data.last_status != status_$ok) {
        LOCATE_SERVER(&node, &net, status_ret);             /* 0x00E4AD4C */
        if (*status_ret != status_$ok) {
            return;                                         /* 0x00E4ADCC */
        }
        located = (boolean)-1;                              /* 0x00E4AD58 */
    } else {
        node = rem_name_$data.curr_node;                    /* 0x00E4AD5C (0x30,A5) */
        net  = rem_name_$data.curr_net;                     /* 0x00E4AD62 (0x34,A5) */
    }

    /* 0x00E4AD68-0x00E4AD80 */
    REM_NAME_$GET_ENTRY_BY_NAME(net, node, dir_uid, name, *name_len, entry_ret, status_ret);

    /* 0x00E4AD84-0x00E4AD9C */
    st = *status_ret;
    if (st == status_$naming_name_not_found ||
        st == status_$naming_invalid_pathname ||
        st == status_$ok || located < 0) {
        return;
    }

    /* 0x00E4AD9E-0x00E4ADC8: locate and retry once */
    LOCATE_SERVER(&node, &net, status_ret);
    if (*status_ret == status_$ok) {
        REM_NAME_$GET_ENTRY_BY_NAME(net, node, dir_uid, name, *name_len, entry_ret, status_ret);
    }
}
