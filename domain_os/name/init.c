/*
 * NAME_$INIT - Initialise the naming subsystem (0x00E31624, 630 bytes)
 *
 * SAU2 map: NAME code segment at E31578 (size 0x394) holds
 *   name_$init_check_status  0xE31578  (nested procedure of NAME_$INIT)
 *   NAME_$INIT               0xE31624
 * followed by the literal pool (0xE3189A-0xE3190B); the module's data
 * segment is `D E35040 NAME size = 108`: a 256-byte path buffer at 0xE35040
 * and the crash-pause delay at 0xE35140.  NAME_$INIT sets A5 = 0xE35040
 * (`lea (0xe35040).l,A5` at 0x00E3162C) and the nested procedure inherits it.
 *
 * Boot sequence:
 *   1. ACL_$ENTER_SUPER, DIR_$INIT
 *   2. root/node UIDs from the arguments, or from the boot volume's VTOC when
 *      vol_root_uid is UID_$NIL
 *   3. 58 per-ASID slots <- node UID, mapped-info flags cleared
 *   4. map the node directory; resolve and map "/com" (falling back to the
 *      node directory)
 *   5. when the UIDs came from the VTOC, make NAME_$CANNED_ROOT_UID the
 *      father of the node directory
 *   6. format "/sys/node_data.<node id>", lower-case the id (or cut the
 *      name to "/sys/node_data" in the VTOC case), resolve it into
 *      NAME_$DATA.node_data_uid
 *   7. ACL_$EXIT_SUPER
 */

#include "name/name_internal.h"
#include "node/node.h"

/* Global NAME data area at 0xE80264 (layout: name_$data_t in name/name.h) */
name_$data_t NAME_$DATA;

/* 0xE173E4 (map: NAME_$CANNED_ROOT_UID, in the UID_LIST segment); image
 * bytes `00 00 03 08 00 00 00 00`. */
uid_t NAME_$CANNED_ROOT_UID = UID_CONST(0x00000308u, 0u);

/*
 * ============================================================================
 * Module data segment, 0xE35040 (size 0x108)
 * ============================================================================
 */

/* 0xE35040: the 256-byte pathname buffer NAME_$INIT builds "/com" and
 * "/sys/node_data.<id>" in (A5 base).  Zero in the image. */
static char name_$init_path_buf[256];

/* 0xE35140: clock_t delay handed to TIME_$WAIT by name_$init_check_status
 * before crashing (`pea (0x100,A5)` at 0x00E315D2).  Image bytes
 * `00 00 00 28 00 00 00 00`. */
static clock_t name_$init_crash_delay = { 0x00000028u, 0 };

_Static_assert(sizeof(name_$init_path_buf) == 0x100, "name init path buffer");

/*
 * ============================================================================
 * Literal pool (code segment)
 * ============================================================================
 */

/* 0xE315F6: "%/%/%/%/%/%/%/Unable to %$" - seven newlines then the prefix.
 * `pea (0x68,PC)` at 0x00E3158C. */
static const char name_$init_unable_fmt_00e315f6[] = "%/%/%/%/%/%/%/Unable to %$";

/* 0xE31610: word 0 - TIME_$WAIT's delay-type argument (`pea (0x38,PC)` at
 * 0x00E315D6).  It doubles as the NUL that ends the string above. */
static const uint16_t name_$init_zero_w_00e31610 = 0;

/* 0xE31612: ' "%a" -- %lh%.' - the failing pathname and the status.
 * `pea (0x50,PC)` at 0x00E315C0. */
static const char name_$init_detail_fmt_00e31612[] = " \"%a\" -- %lh%.";

/* 0xE31620: longword 0 - the "no further arguments" cell passed twice to
 * every VFMT_$WRITE10 call, as VFMT_$FORMATN's sixth argument, and as
 * name_$init_check_status' second argument.  (`pea (0x98,PC)` 0x00E31586,
 * `pea (0x84,PC)` 0x00E3159A, `pea (0x70,PC)` 0x00E315AE, `pea (-0x5a,PC)`
 * 0x00E31678, `pea (-0x10a,PC)` 0x00E31728, `pea (-0x1b8,PC)` 0x00E317D6,
 * `pea (-0x1da,PC)` 0x00E317F8.) */
static const uint32_t name_$init_zero_l_00e31620 = 0;

/* 0xE3189A: word 0x0100 - VFMT_$FORMATN's buffer size (`pea (0x92,PC)` at
 * 0x00E31806). */
static const int16_t name_$init_buf_size_00e3189a = 0x100;

/* 0xE3189C: "resolve%$" (`pea (0x1c,PC)` at 0x00E3187E). */
static const char name_$init_msg_resolve_00e3189c[] = "resolve%$";

/* 0xE318A8: "get root directory uids from vtoc%$" (`pea (0x22a,PC)` at
 * 0x00E3167C). */
static const char name_$init_msg_vtoc_00e318a8[] = "get root directory uids from vtoc%$";

/* 0xE318CC: 12-byte Pascal set, bound 0x5F: ['A'..'Z'] (`lea (0x88,PC)` at
 * 0x00E31842).  Image bytes `07 ff ff fe 00 00 00 00 00 00 00 00`. */
static const uint8_t name_$init_set_upper_00e318cc[12] = {
    0x07, 0xff, 0xff, 0xfe, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};

/* 0xE318D8: "/sys/node_data.%lh%$" (`pea (0xca,PC)` at 0x00E3180C). */
static const char name_$init_node_data_fmt_00e318d8[] = "/sys/node_data.%lh%$";

/* 0xE318EC: "set // as father of /%$" (`pea (0x110,PC)` at 0x00E317DA). */
static const char name_$init_msg_father_00e318ec[] = "set // as father of /%$";

/* 0xE31904: "map //%$" (`pea (0x1d6,PC)` at 0x00E3172C). */
static const char name_$init_msg_map_00e31904[] = "map //%$";

/*
 * name_$init_check_status (0x00E31578, 122 bytes) - nested procedure
 *
 * Reaches NAME_$INIT's frame through the static link (`movea.l (A6),A2` at
 * 0x00E3157E) to test the status at A6-0x18 - only its LOW word
 * (`tst.w (-0x16,A2)` at 0x00E31580).  On a non-zero low word it prints
 *
 *     <7 newlines>Unable to <msg> "<param1>" -- <status>
 *
 * with three VFMT_$WRITE10 calls, pauses for the module delay through
 * TIME_$WAIT, and crashes with the same status.  Flattened here with the
 * parent's status passed explicitly.
 *
 * Frame: (0x8,A6) msg, (0xc,A6) param1, (0x10,A6) param2 (a longword whose
 * ADDRESS is what "%a" receives, `pea (0x10,A6)` at 0x00E315B8); local
 * status for TIME_$WAIT at A6-0x8.
 */
static void name_$init_check_status(const char *msg, const void *param1,
                                    int32_t param2, status_$t *parent_status)
{
    status_$t wait_status;                                  /* A6-0x8 */

    /* 0x00E31580: low word only */
    if ((uint16_t)((uint32_t)*parent_status & 0xFFFFu) == 0) {
        return;
    }

    /* 0x00E31586-0x00E31596 */
    VFMT_$WRITE10(name_$init_unable_fmt_00e315f6,
                  &name_$init_zero_l_00e31620, &name_$init_zero_l_00e31620);
    /* 0x00E3159A-0x00E315AA */
    VFMT_$WRITE10(msg,
                  &name_$init_zero_l_00e31620, &name_$init_zero_l_00e31620);
    /* 0x00E315AE-0x00E315CA */
    VFMT_$WRITE10(name_$init_detail_fmt_00e31612,
                  param1, &param2, parent_status,
                  &name_$init_zero_l_00e31620, &name_$init_zero_l_00e31620);

    /* 0x00E315CE-0x00E315E0: TIME_$WAIT(&0, &delay at A5+0x100, &local) */
    TIME_$WAIT((uint16_t *)&name_$init_zero_w_00e31610,
               &name_$init_crash_delay, &wait_status);

    /* 0x00E315E4-0x00E315E8 */
    CRASH_SYSTEM(parent_status);
}

/*
 * NAME_$INIT
 *
 * Frame: (0x8,A6) vol_root_uid -> A2, (0xc,A6) vol_node_uid.
 * Locals: -0x10 root_uid, -0x8 node_uid, -0x18 status, -0x1c path_len.
 * Registers: D2b = "UIDs were supplied" (0xFF/0), D3w = path length.
 */
void NAME_$INIT(uid_t *vol_root_uid, uid_t *vol_node_uid)
{
    uid_t      root_uid;                                    /* A6-0x10 */
    uid_t      node_uid;                                    /* A6-0x8 */
    status_$t  status;                                      /* A6-0x18 */
    int16_t    path_len;                                    /* A6-0x1C */
    boolean    uids_supplied;                               /* D2b */
    int16_t    len;                                         /* D3w */
    int16_t    i;
    int16_t    count;
    int16_t    idx;
    uint8_t    ch;
    uint16_t   d;

    ACL_$ENTER_SUPER();                                     /* 0x00E31636 */
    DIR_$INIT();                                            /* 0x00E3163C */

    /* 0x00E31642-0x00E31656: `cmpm.l` x2 / `sne D2b` - true when the caller
     * supplied a root UID other than UID_$NIL. */
    uids_supplied = (boolean)((vol_root_uid->high == UID_$NIL.high &&
                               vol_root_uid->low  == UID_$NIL.low) ? 0 : -1);

    if (uids_supplied >= 0) {
        /* 0x00E31658-0x00E31672: VTOC_$GET_NAME_DIRS(CAL_$BOOT_VOLX, &root,
         * &node, &status) */
        VTOC_$GET_NAME_DIRS(CAL_$BOOT_VOLX, &root_uid, &node_uid, &status);
        /* 0x00E31676-0x00E31684 */
        name_$init_check_status(name_$init_msg_vtoc_00e318a8,
                                &name_$init_zero_l_00e31620, 0, &status);
    } else {
        /* 0x00E3168A-0x00E3169C */
        root_uid.high = vol_root_uid->high;
        root_uid.low  = vol_root_uid->low;
        node_uid.high = vol_node_uid->high;
        node_uid.low  = vol_node_uid->low;
    }

    /* 0x00E316A0-0x00E316BA: NAME_$DATA+0x38 <- root, +0x30 <- node */
    NAME_$DATA.root_uid.high = root_uid.high;
    NAME_$DATA.root_uid.low  = root_uid.low;
    NAME_$DATA.node_uid.high = node_uid.high;
    NAME_$DATA.node_uid.low  = node_uid.low;

    /* 0x00E316BE-0x00E316FE: `moveq #0x39` + dbf = 58 slots.  Each pass
     * copies node_uid (+0x30) into wdir_uid[i] and ndir_uid[i] and clears
     * the first byte of wdir_mapped_info[i] and ndir_mapped_info[i]. */
    for (i = 0; i < NAME_$MAX_ASIDS; i++) {
        NAME_$DATA.wdir_uid[i].high = NAME_$DATA.node_uid.high;
        NAME_$DATA.wdir_uid[i].low  = NAME_$DATA.node_uid.low;
        NAME_$DATA.ndir_uid[i].high = NAME_$DATA.node_uid.high;
        NAME_$DATA.ndir_uid[i].low  = NAME_$DATA.node_uid.low;
        NAME_$DATA.wdir_mapped_info[i].active = 0;
        NAME_$DATA.ndir_mapped_info[i].active = 0;
    }

    /* 0x00E31702-0x00E31708: node (+0x20) and com (+0x08) mapping flags */
    NAME_$DATA.node_mapped_info.active = 0;
    NAME_$DATA.com_mapped_info.active  = 0;

    /* 0x00E3170C-0x00E31722: map the node directory into ASID 0 */
    name_$map_dir(&NAME_$DATA.node_uid, 0, &NAME_$DATA.node_mapped_info, &status);
    /* 0x00E31726-0x00E31734 */
    name_$init_check_status(name_$init_msg_map_00e31904,
                            &name_$init_zero_l_00e31620, 0, &status);

    /* 0x00E31738-0x00E3174E: "/com" then 63 longwords of spaces (252 bytes)
     * fill the 256-byte buffer; length 4. */
    name_$init_path_buf[0] = '/';
    name_$init_path_buf[1] = 'c';
    name_$init_path_buf[2] = 'o';
    name_$init_path_buf[3] = 'm';
    for (i = 4; i < 256; i++) {
        name_$init_path_buf[i] = ' ';
    }
    path_len = 4;

    /* 0x00E31754-0x00E31768: resolve "/com" into NAME_$DATA.com_uid (+0x18) */
    NAME_$RESOLVE(name_$init_path_buf, &path_len, &NAME_$DATA.com_uid, &status);

    /* 0x00E3176C-0x00E3178E: on success map it into com_mapped_info
     * (+0x08); a false result (`tst.b D0b / bmi`) also takes the fallback. */
    if (status != status_$ok ||
        name_$map_dir(&NAME_$DATA.com_uid, 0, &NAME_$DATA.com_mapped_info, &status) >= 0) {
        /* 0x00E31790-0x00E317B0: com <- node: UID and the 16-byte info */
        NAME_$DATA.com_uid.high = NAME_$DATA.node_uid.high;
        NAME_$DATA.com_uid.low  = NAME_$DATA.node_uid.low;
        NAME_$DATA.com_mapped_info = NAME_$DATA.node_mapped_info;
    }

    /* 0x00E317B2-0x00E317F6: only when the UIDs came from the VTOC */
    if (uids_supplied >= 0) {
        /* FILE_$SET_DIRPTR(&node_uid (+0x30), &NAME_$CANNED_ROOT_UID, &status) */
        FILE_$SET_DIRPTR(&NAME_$DATA.node_uid, &NAME_$CANNED_ROOT_UID, &status);
        name_$init_check_status(name_$init_msg_father_00e318ec,
                                &name_$init_zero_l_00e31620, 0, &status);
        /* 0x00E317E6-0x00E317F6: the nested check only crashes on a non-zero
         * LOW word; this catches the rest. */
        if (status != status_$ok) {
            CRASH_SYSTEM(&status);
        }
    }

    /* 0x00E317F8-0x00E31816: VFMT_$FORMATN("/sys/node_data.%lh%$", buf,
     * &0x100, &path_len, &NODE_$ME, &0) */
    VFMT_$FORMATN(name_$init_node_data_fmt_00e318d8, name_$init_path_buf,
                  (int16_t *)&name_$init_buf_size_00e3189a, &path_len,
                  &NODE_$ME, &name_$init_zero_l_00e31620);

    len = path_len;                                         /* 0x00E3181A */
    if (uids_supplied >= 0) {
        len = 0x0E;                                         /* 0x00E31822 */
    } else {
        /* 0x00E31826-0x00E31854: characters 16..len (1-based) that are in
         * ['A'..'Z'] get 0x20 ADDED, i.e. the node id is lower-cased.
         * `subi.w #0x10 / bmi` skips names shorter than 16; the dbf count
         * is len-16 so the loop runs len-15 times. */
        count = (int16_t)(len - 0x10);
        if (count >= 0) {
            idx = 0x10;
            do {
                ch = (uint8_t)name_$init_path_buf[idx - 1];
                /* set test: bound 0x5F, byte (0x5F-ch)>>3, bit ch&7 */
                if (ch <= 0x5F) {
                    d = (uint16_t)(0x5F - ch);
                    if ((name_$init_set_upper_00e318cc[d >> 3] >> (ch & 7)) & 1) {
                        name_$init_path_buf[idx - 1] =
                            (char)(uint8_t)(ch + 0x20);
                    }
                }
                idx = (int16_t)(idx + 1);
                count = (int16_t)(count - 1);
            } while (count != -1);
        }
    }

    /* 0x00E31858-0x00E31872: resolve into NAME_$DATA.node_data_uid (+0x00) */
    path_len = len;
    NAME_$RESOLVE(name_$init_path_buf, &path_len, &NAME_$DATA.node_data_uid, &status);

    /* 0x00E31876-0x00E31886: ("resolve%$", buffer, (long)len) */
    name_$init_check_status(name_$init_msg_resolve_00e3189c,
                            name_$init_path_buf, (int32_t)len, &status);

    ACL_$EXIT_SUPER();                                      /* 0x00E3188A */
}
