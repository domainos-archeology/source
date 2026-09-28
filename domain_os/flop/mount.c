/*
 * flop/mount.c - flop_$mount_floppy (0x00E323E6, 338 bytes)
 *
 * Module FLOP_BOOT (map: 0x00E323A8, size 0x38C).  Mounts the floppy's
 * logical volume and links it into the node's root directory as "flp".
 *
 * This file also DEFINES the `pea (d,PC)` constant cells that sit in the
 * code segment right after this routine's `rts` (0x00E32538-0x00E3254B).
 * Four of them are read by FLOP_$BOOT in boot.c as well, with different
 * meanings; they are one object each in the image, so they are module
 * globals declared in flop_internal.h rather than a copy per file.
 */

#include "flop/flop_internal.h"

/*
 * ----------------------------------------------------------------------------
 * Constant cells, in address order, with their image bytes
 * (`gsk read 0xe32538 0x20`:  00 00 00 04 00 03 00 00 00 01 ff 00 66 6c 70 00
 *                             2f 66 6c 70)
 * ----------------------------------------------------------------------------
 */

/*
 * 0x00E32538: 00 - a zero byte.
 *   here:   VOLX_$MOUNT `write_prot`   (pea (0x136,PC) at 0x00E32400)
 *           VOLX_$DISMOUNT `force`     (pea (0x4c,PC)  at 0x00E324EA)
 *   boot.c: FILE_$LOCK `rights`, MST_$MAP_AT `concurrency`
 */
uint8_t flop_zero_byte = 0x00;

/*
 * 0x00E3253A: 00 04
 *   here:   NAME_$RESOLVE's length of "/flp"  (pea (0xac,PC) at 0x00E3248C)
 *   boot.c: FILE_$LOCK `lock_mode`
 */
uint16_t flop_word_four = 0x0004;

/* 0x00E3253C: 00 03 - the length of "flp"; DIR_$ADDU (pea (0xde,PC) at
 * 0x00E3245C) and DIR_$DROPU (pea (0x24,PC) at 0x00E32516).  Mount-only. */
static int16_t flop_flp_name_len = 0x0003;

/* 0x00E3253E: 00 00 - a zero word, passed TWICE to both VOLX calls as `bus`
 * and `ctlr` (pea (0x130,PC) at 0x00E3240C then `move.l (SP),-(SP)`;
 * pea (0x46,PC) at 0x00E324F6 then `move.l (SP),-(SP)`).  Mount-only. */
static int16_t flop_zero_word = 0x0000;

/*
 * 0x00E32540: 00 01
 *   here:   VOLX_$MOUNT / VOLX_$DISMOUNT `dev` AND `lv_num` (pea (0x12c,PC)
 *           at 0x00E32412 and pea (0x136,PC) at 0x00E32408; pea (0x42,PC)
 *           at 0x00E324FC and pea (0x4c,PC) at 0x00E324F2)
 *   boot.c: FILE_$LOCK `lock_index`
 */
uint16_t flop_word_one = 0x0001;

/*
 * 0x00E32542: ff - Pascal true.
 *   here:   VOLX_$MOUNT `salvage_ok`  (pea (0x13c,PC) at 0x00E32404)
 *   boot.c: MST_$MAP `concurrency`
 * (0x00E32543 is the 00 pad byte before the string.)
 */
uint8_t flop_ff_byte = 0xFF;

/* 0x00E32544: "flp" NUL - DIR_$ADDU (pea (0xe2,PC) at 0x00E32460) and
 * DIR_$DROPU (pea (0x28,PC) at 0x00E3251A). */
static char flop_flp_name[] = "flp";

/* 0x00E32548: "/flp" - NAME_$RESOLVE (pea (0xb6,PC) at 0x00E32490). */
static char flop_flp_path[] = "/flp";

/*
 * flop_$mount_floppy - mount the floppy volume and link it as /flp
 *
 * Frame (link.w A6,-0x34; D2 and A2 saved):
 *   A6-0x34  8  node_uid_tmp   NAME_$GET_NODE_UID's output
 *   A6-0x28  4  mount_status   VOLX_$MOUNT's status
 *   A6-0x24  4  local_status   NAME_$RESOLVE / VOLX_$DISMOUNT / DIR_$DROPU
 *   A6-0x20  8  mount_uid      the volume's root directory UID
 *   A6-0x18  8  existing_uid   what "/flp" already resolves to
 *   A6-0x10  8  node_uid       the copy the DIR calls are given
 *   D2       1  added_dir      -1 once DIR_$ADDU succeeded
 *   A2          status_ret (argument 1, (0x8,A6))
 *
 * The routine has three exits, all through the common epilogue at
 * 0x00E3252E: the mount-failure path (0x00E32438), the success path that
 * hands back mount_status (0x00E3252A), and the cleanup path (0x00E324E6)
 * which leaves *status_ret as the failing call set it.
 */
void flop_$mount_floppy(status_$t *status_ret)
{
    uid_t node_uid_tmp;                 /* A6-0x34 */
    status_$t mount_status;             /* A6-0x28 */
    status_$t local_status;             /* A6-0x24 */
    uid_t mount_uid;                    /* A6-0x20 */
    uid_t existing_uid;                 /* A6-0x18 */
    uid_t node_uid;                     /* A6-0x10 */
    int8_t added_dir;                   /* D2 - never initialised before the
                                           paths that read it set it */

    /*
     * 0x00E323F2-0x00E3241C: VOLX_$MOUNT(dev=&1, bus=&0, ctlr=&0, lv=&1,
     * salvage_ok=&0xFF, write_prot=&0, parent=&UID_$NIL, &mount_uid,
     * &mount_status).  `move.l #0xe1737c,-(SP)` is the address of UID_$NIL.
     */
    VOLX_$MOUNT((int16_t *)&flop_word_one, &flop_zero_word, &flop_zero_word,
                (int16_t *)&flop_word_one, (int8_t *)&flop_ff_byte,
                (int8_t *)&flop_zero_byte, &UID_$NIL, &mount_uid,
                &mount_status);

    /*
     * 0x00E32420-0x00E3243A: a non-zero status has bit 31 cleared IN PLACE
     * (`bclr.b #0x7,(-0x28,A6)`) and is then compared with the
     * write-protected warning; anything else is the result.  The cleared
     * value is what the success path returns at 0x00E3252A.
     */
    if (mount_status != status_$ok) {
        mount_status &= 0x7FFFFFFF;
        if (mount_status != status_$volume_disk_is_write_protected) {
            *status_ret = mount_status;
            return;                                 /* bra.w 0x00E3252E */
        }
    }

    /* 0x00E3243E-0x00E32454: fetch the node UID, then copy it into the
     * eight bytes at A6-0x10 that the DIR calls are given. */
    NAME_$GET_NODE_UID(&node_uid_tmp);
    node_uid.high = node_uid_tmp.high;
    node_uid.low = node_uid_tmp.low;

    /* 0x00E32456-0x00E3246E */
    DIR_$ADDU(&node_uid, flop_flp_name, &flop_flp_name_len, &mount_uid,
              status_ret);

    /* 0x00E32472-0x00E32478: added -> D2 = 0xFF and skip to the test at
     * 0x00E324BE (which the zero status passes). */
    if (*status_ret == status_$ok) {
        added_dir = -1;                             /* st D2b */
    } else if (*status_ret == status_$name_already_exists) {    /* 0x00E3247A */
        /* 0x00E32482-0x00E3249A: someone already linked "flp"; see whether
         * it is this very volume. */
        added_dir = 0;                              /* clr.b D2b */
        NAME_$RESOLVE(flop_flp_path, (int16_t *)&flop_word_four, &existing_uid,
                      &local_status);
        /* 0x00E3249E-0x00E324B8: `cmpm.l` twice; a match clears *status_ret
         * (the 0xE0003 is forgiven) and joins the SET_DAD path. */
        if (local_status == status_$ok &&
            mount_uid.high == existing_uid.high &&
            mount_uid.low == existing_uid.low) {
            *status_ret = status_$ok;
        }
    } else {
        added_dir = 0;                              /* 0x00E324BA clr.b D2b */
    }

    /* 0x00E324BC-0x00E324BE: any surviving error goes to cleanup. */
    if (*status_ret != status_$ok) {
        goto cleanup;
    }

    /* 0x00E324C0-0x00E324D0 */
    DIR_$SET_DAD(&mount_uid, &node_uid, status_ret);

    /* 0x00E324D4-0x00E324E0: `bclr.b #0x7,(A2)` strips bit 31, then a
     * write-protected disk is not an error here. */
    *status_ret &= 0x7FFFFFFF;
    if (*status_ret == status_$disk_write_protected) {
        *status_ret = status_$ok;
    }

    /* 0x00E324E2-0x00E324E4 */
    if (*status_ret == status_$ok) {
        /* 0x00E3252A: report the (bit-31-stripped) mount status, which may
         * be the write-protected warning. */
        *status_ret = mount_status;
        return;
    }

cleanup:
    /*
     * 0x00E324E6-0x00E32506: VOLX_$DISMOUNT(dev=&1, bus=&0, ctlr=&0, lv=&1,
     * &mount_uid, force=&0, &local_status) - the same cells as the mount.
     * Its status is discarded; *status_ret keeps the error.
     */
    VOLX_$DISMOUNT((int16_t *)&flop_word_one, &flop_zero_word, &flop_zero_word,
                   (int16_t *)&flop_word_one, &mount_uid,
                   (int8_t *)&flop_zero_byte, &local_status);

    /* 0x00E3250A-0x00E32528: `tst.b D2b` / `bpl` - only an entry this call
     * added is dropped again. */
    if (added_dir < 0) {
        DIR_$DROPU(&node_uid, flop_flp_name, (uint16_t *)&flop_flp_name_len,
                   &mount_uid, &local_status);
    }
}
