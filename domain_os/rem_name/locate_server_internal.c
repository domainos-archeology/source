/*
 * rem_name/locate_server_internal.c - LOCATE_SERVER (0x00E4A420, 142 bytes)
 *
 * Part of the REM_NAME module (SAU2 map: I 0xE4A408 size 0xB20,
 * D 0xE7DBB8 size 0x40; A5 = 0xE7DBB8, `lea (0xe7dbb8).l,A5` at 0x00E4A428).
 * The map names this module-local helper LOCATE_SERVER; the exported
 * REM_NAME_$LOCATE_SERVER (0x00E4A722) is what it delegates to.
 *
 * Two regimes, selected by heard_from_server (A5+0x3C):
 *   heard:      if |time_heard - TIME_$CLOCKH| <= server_timeout (unsigned,
 *               `bls` at 0x00E4A452) delegate; otherwise forget the server
 *               and report "cant find name server helper" in both
 *               last_status and *status_ret.
 *   not heard:  allow at most four attempts (`cmpi.w #0x3 / ble`, signed),
 *               count one, delegate, and on success record the contact.
 *
 * Frame (A6+): 0x08 node_ret -> A2, 0x0C net_ret -> A3, 0x10 status_ret -> A4.
 */

#include "rem_name/rem_name_internal.h"

#include "time/time.h"     /* TIME_$CLOCKH */
void LOCATE_SERVER(uint32_t *node_ret, uint32_t *net_ret, status_$t *status_ret)
{
    uint32_t age;                                   /* D0 */

    /* 0x00E4A43A `tst.b (0x3c,A5) / bpl` */
    if (rem_name_$data.heard_from_server < 0) {
        /* 0x00E4A440-0x00E4A44C: signed absolute difference */
        age = rem_name_$data.time_heard_from_server - TIME_$CLOCKH;
        if ((int32_t)age < 0) {
            age = (uint32_t)(-(int32_t)age);
        }
        /* 0x00E4A44E `cmp.l (0x20,A5),D0 / bls`: unsigned */
        if (age > rem_name_$data.server_timeout) {
            /* 0x00E4A454-0x00E4A462 */
            rem_name_$data.heard_from_server = 0;
            rem_name_$data.last_status = status_$naming_cant_find_name_server_helper;
            *status_ret = status_$naming_cant_find_name_server_helper;
            return;
        }
        /* 0x00E4A466-0x00E4A46C */
        REM_NAME_$LOCATE_SERVER(node_ret, net_ret, status_ret);
        return;
    }

    /* 0x00E4A472 `cmpi.w #0x3,(0x3a,A5) / ble`: signed word */
    if ((int16_t)rem_name_$data.retry_count > 3) {
        *status_ret = status_$naming_cant_find_name_server_helper;   /* 0x00E4A47A */
        return;
    }

    /* 0x00E4A482-0x00E4A48C */
    rem_name_$data.retry_count = (uint16_t)(rem_name_$data.retry_count + 1);
    REM_NAME_$LOCATE_SERVER(node_ret, net_ret, status_ret);

    /* 0x00E4A494-0x00E4A49C */
    if (*status_ret == status_$ok) {
        rem_name_$data.heard_from_server = (int8_t)-1;
        rem_name_$data.time_heard_from_server = TIME_$CLOCKH;
    }
}
