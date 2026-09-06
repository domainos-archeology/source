/*
 * NAME_$LOCK_DIR - Acquire a directory lock and map the directory
 *
 * Takes the FILE_$PRIV_LOCK lock on a directory (retrying while it is busy),
 * optionally checks ACL rights on it, enters ACL super mode, and hands back
 * the address at which the directory is mapped.  Directories that are already
 * kept mapped (node, /com, and the per-address-space working and naming
 * directories) reuse their cached mapping; anything else is mapped with
 * MST_$MAPS.
 *
 * Original address: 0x00E54854
 * Size: 722 bytes
 *
 * Module context: this routine runs with A5 = 0xE7FD24, the NAME/DIR "OLD"
 * module data base.  The four per-process tables it touches
 * (NAME_$LOCK_SLOT/_MODE/_HANDLE/_UID) are described in name/name_data.c.
 *
 * Parameters (five - see name/name.h): every caller pushes lock_mode and
 * acl_rights together with a single `move.l #imm,-(SP)`, but the callee reads
 * them as two separate words at (0x10,A6) and (0x12,A6), so the HIGH half of
 * that immediate is lock_mode and the LOW half is acl_rights.
 */

#include "name/name_internal.h"
#include "file/file_internal.h"

/* Status codes */
#define file_$object_in_use              0x000F0006
#define status_$naming_directory_locked  0x000E0016

/*
 * Constant cells in the code region passed by reference (`pea (d,PC)`).
 *
 * The original shares these literals with the rest of the NAME/DIR module;
 * they are read-only Pascal const/VAR arguments, so a file-static copy is
 * behaviourally identical.
 */

/* 0xE5472E, pushed by `pea (-0x214,PC)` at 0xE54940: TIME_$WAIT delay type.
 * 0 selects a relative delay.  (Ghidra: NAME_$CONST_ZERO_W; this cell used to
 * be mislabelled ACL_TYPE_FILE.) */
static uint16_t name_$lock_dir_delay_type = 0;

/* 0xE54730, pushed by `pea (-0x186,PC)` at 0xE548B4: FILE_$PRIV_LOCK's
 * acl_ctx argument.  The cell holds a NULL pointer and its ADDRESS is what is
 * passed, so the parameter is a `void **`.  Ghidra: NAME_$CONST_ZERO_L. */
static void *name_$lock_dir_acl_ctx = NULL;

/* 0xE54B28, pushed by `pea (0x1a4,PC)` at 0xE54982: ACL_$RIGHTS' second
 * (acl-data) argument, a zero longword.  Ghidra: NAME_$CONST_ZERO_L2. */
static uint32_t name_$lock_dir_acl_data = 0;

/* 0xE54B26, pushed by `pea (0x1b2,PC)` at 0xE54972: ACL_$RIGHTS' object-type
 * word.  1 == directory.  Ghidra: ACL_TYPE_DIR. */
static int16_t name_$lock_dir_acl_obj_type = 1;

void NAME_$LOCK_DIR(uid_t *dir_uid, uint32_t *handle_ret,
                    int16_t lock_mode, int16_t acl_rights,
                    status_$t *status_ret)
{
    uid_t     local_uid;        /* (-0x10,A6) */
    status_$t unlock_status;    /* (-0x20,A6): TIME_$WAIT / NAME_$UNLOCK_DIR status */
    clock_t   retry_delay;      /* (-0x1c,A6): 6-byte clock value {high, low} */
    uint16_t  lock_result;      /* (-0x26,A6): FILE_$PRIV_LOCK result_out */
    uint32_t  map_out;          /* (-0x24,A6): MST_$MAPS out buffer */
    uint32_t  required_rights;  /* (-0x2c,A6): acl_rights zero-extended to a long */
    int16_t   retry_count;      /* D4 */
    uint32_t  start_clockh;     /* D5 */

    /* 0xE5486C: copy the caller's UID into the frame (two longwords). */
    local_uid = *dir_uid;

    retry_count = 0;                    /* 0xE54874: clr.w D4w */
    start_clockh = TIME_$CLOCKH;        /* 0xE54876 */

    /* 0xE5488C: lock-acquisition retry loop. */
    for (;;) {
        /* 0xE54894 / 0xE548A0 */
        NAME_$LOCK_MODE[PROC1_$CURRENT] = lock_mode;
        NAME_$LOCK_HANDLE[PROC1_$CURRENT] = 0;

        /*
         * 0xE548A4-0xE548D6.  The `subq.l #2,SP` result slot the original
         * reserves is popped without ever being read.
         *
         * `move.l #0x880000,-(SP)` at 0xE548BE fills two separate word
         * parameters: flags = 0x0088 at (0x14,A6) and key = 0x0000 at
         * (0x16,A6).
         */
        FILE_$PRIV_LOCK(&local_uid,
                        (int16_t)PROC1_$AS_ID,
                        0,                          /* side */
                        (uint16_t)lock_mode,
                        false,                      /* local_only */
                        0x0088,                     /* flags */
                        0x0000,                     /* key */
                        0, 0, 0,                    /* rem_key/rem_node/rem_extra */
                        &name_$lock_dir_acl_ctx,
                        1,                          /* rem_wait */
                        &NAME_$LOCK_SLOT[PROC1_$CURRENT],
                        &lock_result,               /* rights_out */
                        status_ret);

        /* 0xE548DA */
        if (*status_ret == status_$ok) {
            /* 0xE548DE: remember which directory this process has locked. */
            NAME_$LOCK_UID[PROC1_$CURRENT] = local_uid;
            break;
        }

        /* 0xE548F4: any error other than "object in use" is returned as-is. */
        if (*status_ret != file_$object_in_use) {
            break;
        }

        /*
         * 0xE548FC: server processes (PROC1_$TYPE == 9) never wait.  The
         * original indexes with `(-0x2,A0,D6w*1)` off 0xE2612C, i.e. the
         * label sits on element 1 of a 1-based array whose base is 0xE2612A;
         * PROC1_$TYPE in proc1.h is declared at 0xE2612A, so the C index is
         * PROC1_$CURRENT with no adjustment.
         *
         * 0xE5490E: otherwise bump the retry count (only reached when the
         * type test failed) and give up after 0x78 retries or 0x78 clock
         * ticks.
         */
        if (PROC1_$TYPE[PROC1_$CURRENT] == 9 ||
            ++retry_count > 0x78 ||
            (int32_t)(TIME_$CLOCKH - start_clockh) > 0x78) {
            *status_ret = status_$naming_directory_locked;   /* 0xE54926 */
            break;
        }

        /* 0xE5492E: sleep for 0x4000 ticks and try again. */
        retry_delay.high = 0;
        retry_delay.low = 0x4000;
        TIME_$WAIT(&name_$lock_dir_delay_type, &retry_delay, &unlock_status);
    }

    /* 0xE54952 */
    if (*status_ret != status_$ok) {
        if (*status_ret != status_$naming_directory_locked) {
            /* 0xE5495E: bset.b #7,(A3) - bit 31 of the status longword. */
            *status_ret |= 0x80000000u;
        }
        ACL_$ENTER_SUPER();     /* 0xE54962 */
        return;                 /* 0xE54968 -> 0xE54B1C */
    }

    /* 0xE5496C */
    if (acl_rights != 0) {
        required_rights = (uint32_t)(uint16_t)acl_rights;   /* 0xE54976: zero-extended */
        ACL_$RIGHTS(&local_uid, &name_$lock_dir_acl_data, &required_rights,
                    &name_$lock_dir_acl_obj_type, status_ret);  /* 0xE5498A */
        ACL_$ENTER_SUPER();                                     /* 0xE54994 */
        if (*status_ret != status_$ok) {                        /* 0xE5499A */
            NAME_CONVERT_ACL_STATUS(status_ret);                /* 0xE549A0 */
            goto unlock_and_return;                             /* 0xE549A8 -> 0xE54B14 */
        }
    } else {
        ACL_$ENTER_SUPER();     /* 0xE549AC */
    }

    /* 0xE549B2: is this the node directory, and is its cached mapping live? */
    if (local_uid.high == NAME_$NODE_UID.high &&
        local_uid.low == NAME_$NODE_UID.low &&
        NAME_$NODE_MAPPED_INFO.active < 0 &&
        NAME_$NODE_MAPPED_INFO.reserved_02 == 0 &&
        NAME_$NODE_MAPPED_INFO.entry_count == 1) {
        *handle_ret = NAME_$NODE_MAPPED_INFO.first_base;         /* 0xE549E4 */
        goto have_handle;
    }

    /* 0xE549EE: the /com directory. */
    if (local_uid.high == NAME_$COM_UID.high &&
        local_uid.low == NAME_$COM_UID.low &&
        NAME_$COM_MAPPED_INFO.active < 0 &&
        NAME_$COM_MAPPED_INFO.reserved_02 == 0 &&
        NAME_$COM_MAPPED_INFO.entry_count == 1) {
        *handle_ret = NAME_$COM_MAPPED_INFO.first_base;          /* 0xE54A20 */
        goto have_handle;
    }

    {
        /* 0xE54A2A: D1 = PROC1_$AS_ID * 8 indexes the per-ASID UID arrays. */
        int16_t uid_asid = (int16_t)PROC1_$AS_ID;
        name_$mapped_info_t *cached = NULL;

        /* 0xE54A42: working directory UID for this address space. */
        if (NAME_$DATA.wdir_uid[uid_asid].high == local_uid.high &&
            NAME_$DATA.wdir_uid[uid_asid].low == local_uid.low) {
            /* 0xE54A4A: PROC1_$AS_ID is re-read here; D0 = asid * 16. */
            int16_t info_asid = (int16_t)PROC1_$AS_ID;
            if (NAME_$DATA.wdir_mapped_info[info_asid].active < 0 &&
                NAME_$DATA.wdir_mapped_info[info_asid].reserved_02 == 0 &&
                NAME_$DATA.wdir_mapped_info[info_asid].entry_count == 1) {
                cached = &NAME_$DATA.wdir_mapped_info[info_asid];   /* 0xE54A6A */
            }
        }

        /* 0xE54A6C: naming directory UID (reached when WDIR did not match or
         * its cached mapping was not reusable). */
        if (cached == NULL &&
            NAME_$DATA.ndir_uid[uid_asid].high == local_uid.high &&
            NAME_$DATA.ndir_uid[uid_asid].low == local_uid.low) {
            /* 0xE54A84: PROC1_$AS_ID re-read again. */
            int16_t info_asid = (int16_t)PROC1_$AS_ID;
            if (NAME_$DATA.ndir_mapped_info[info_asid].active < 0 &&
                NAME_$DATA.ndir_mapped_info[info_asid].reserved_02 == 0 &&
                NAME_$DATA.ndir_mapped_info[info_asid].entry_count == 1) {
                cached = &NAME_$DATA.ndir_mapped_info[info_asid];
            }
        }

        if (cached != NULL) {
            *handle_ret = cached->first_base;      /* 0xE54AA6 */
            goto have_handle;
        }

        /*
         * 0xE54AB0: not a cached directory - map it.  The two `st -(SP)`
         * pushes are Pascal boolean/byte arguments (0xFF == true) that the
         * callee reads with move.b from the high byte of the word slot
         * (MST_$MAPS reads (0xA,A6) and (0x1E,A6) as bytes).
         */
        map_out = 0;
        *handle_ret = NAME_$PTR_TO_HANDLE(
            MST_$MAPS((int16_t)PROC1_$AS_ID,    /* asid */
                      0xFF,                     /* boolean true, byte arg */
                      &local_uid,
                      0,
                      0x10000,
                      0x16,
                      0,
                      (int8_t)0xFF,             /* boolean true, byte arg */
                      &map_out,
                      status_ret));             /* result comes back in A0 (0xE54AE0) */

        /* 0xE54AE2: `tst.w (0x2,A3)` tests the LOW word of the status. */
        if ((*status_ret & 0xFFFFu) != 0) {
            *status_ret |= 0x80000000u;         /* 0xE54AE8: bset.b #7,(A3) */
            goto unlock_and_return;             /* 0xE54AEC -> 0xE54B14 */
        }
    }

have_handle:
    /* 0xE54AEE */
    NAME_$LOCK_HANDLE[PROC1_$CURRENT] = *handle_ret;

    /* 0xE54B06: the first word of a directory is its type; it must be 1. */
    if (*(int16_t *)NAME_$HANDLE_TO_PTR(*handle_ret) == 1) {
        return;                                 /* 0xE54B0C -> 0xE54B1C */
    }
    *status_ret = status_$naming_bad_directory; /* 0xE54B0E */

unlock_and_return:
    /* 0xE54B14 */
    NAME_$UNLOCK_DIR(&unlock_status);
}
