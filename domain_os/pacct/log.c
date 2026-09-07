/*
 * PACCT_$LOG - Log a process accounting record
 *
 * Writes a 128-byte accounting record for a terminated process.
 * This function is called from process termination code.
 *
 * The accounting record includes:
 *   - Process flags (forked, used superuser)
 *   - Exit status
 *   - User/Group/Org/Login SIDs
 *   - Protection UID
 *   - TTY device number
 *   - Process start time (Unix epoch)
 *   - I/O counts
 *   - Elapsed time
 *   - Process UID
 *   - CPU times (user/system)
 *   - Memory usage
 *   - Command name
 *
 * Buffer management:
 *   - Uses a 32KB memory-mapped buffer
 *   - When buffer space < 128 bytes, maps next 32KB region
 *   - Extends file as needed
 *
 * Original address: 0x00E5AA9C
 * Size: 664 bytes
 */

#include "pacct/pacct_internal.h"

/*
 * ACL_$GET_RE_ALL_SIDS (0x00E48792) writes fixed-size blocks through all four
 * of its output arguments: nine longwords into each of the first two
 * (0x00E487C2 / 0x00E487D2) and three longwords into each of the next two
 * (0x00E487F8 / 0x00E48806).  PACCT_$LOG gives it four distinct frame slots:
 * A6-0x110, A6-0xE8, A6-0xC0 and A6-0xB0 (0x00E5AADE-0x00E5AAF2).
 */
#define PACCT_SID_BLOCK_LONGS       9   /* 36 bytes */
#define PACCT_PROT_BLOCK_LONGS      3   /* 12 bytes */

typedef struct all_sids_t {
    uid_t    user_sid;      /* 0x00: User SID */
    uid_t    group_sid;     /* 0x08: Group SID */
    uid_t    org_sid;       /* 0x10: Org SID */
    uid_t    login_sid;     /* 0x18: Login SID */
    uint32_t extra;         /* 0x20: the ninth longword the callee writes */
} all_sids_t;

_Static_assert(sizeof(all_sids_t) == PACCT_SID_BLOCK_LONGS * 4,
               "all_sids_t is the nine longwords ACL_$GET_RE_ALL_SIDS writes");

typedef struct prot_info_t {
    uid_t    prot_uid;      /* 0x00: Protection UID */
    uint32_t extra;         /* 0x08: the third longword the callee writes */
} prot_info_t;

_Static_assert(sizeof(prot_info_t) == PACCT_PROT_BLOCK_LONGS * 4,
               "prot_info_t is the three longwords ACL_$GET_RE_ALL_SIDS writes");

/*
 * Constant cells in the code region, passed by reference to
 * FILE_$GET_ATTR_INFO at 0x00E5AC36 (`pea (-0x380,PC)` -> 0xE5A8B8) and
 * 0x00E5AC3A (`pea (0xf8,PC)` -> 0xE5AD34).  The image holds 0x007A - the
 * compact record's size - and 0x0004, whose second byte sets bit 2, i.e.
 * "do not run the FILE_$DELETE_INT probe".
 */
static int16_t pacct_$attr_info_size = FILE_ATTR_INFO_SIZE;   /* 0xE5A8B8 */
static uint16_t      pacct_$attr_info_req  = 0x0004;                /* 0xE5AD34 */

/*
 * TODO: pacct_record_t's field offsets in pacct/pacct.h do not match the
 * stores this routine makes.  With the record at A6-0x190 the image writes
 * the SID block at +0x04 (36 bytes), the protection block at +0x28 (12
 * bytes), the device number at +0x34 (0x00E5AC5C), the start time at +0x38,
 * user/system time at +0x3C/+0x3E, the elapsed time at +0x40, a cleared
 * longword at +0x42, the I/O counts at +0x46/+0x48, the process UID at +0x4A
 * (0x00E5ABDE), the 32-byte command name at +0x52 (0x00E5AC04) and the memory
 * figure at +0x72 (0x00E5AB86).  Bead source-q5k1.  The header currently
 * claims 0x2C, 0x30,
 * 0x34/0x36, 0x38, 0x3A, 0x46/0x48/0x4A and 0x68.  Tracked as a bead; this
 * pass keeps the existing field names so the fix stays one change.
 */

/* Unix epoch offset - difference between Domain/OS epoch and Unix epoch */
#define UNIX_EPOCH_OFFSET   0x12CEA600

void PACCT_$LOG(uint8_t *fork_flag, uint8_t *su_flag, int16_t *exit_status,
                clock_t *start_clock, void *proc_times,
                int32_t *user_time, int32_t *sys_time,
                uid_t *tty_uid, uid_t *proc_uid,
                char *comm_ptr, int16_t *comm_len)
{
    status_$t status;
    clock_t current_clock;
    clock_t elapsed;
    all_sids_t sids;                        /* A6-0x110 */
    prot_info_t prot_info;                  /* A6-0xE8 */
    int32_t prot_result[PACCT_PROT_BLOCK_LONGS];        /* A6-0xC0 */
    int32_t subsys_ids[PACCT_PROT_BLOCK_LONGS];         /* A6-0xB0 */
    file_$obj_loc_t tty_loc;                /* A6-0x20 */
    uint8_t file_info[FILE_ATTR_INFO_SIZE]; /* A6-0xA0, the 0x7A compact record */
    pacct_record_t record;
    int16_t len;
    int16_t i;
    void *map_result;

    /* Check if accounting is enabled */
    if (pacct_owner.high == UID_$NIL.high &&
        pacct_owner.low == UID_$NIL.low) {
        return;
    }

    /* Get current clock for elapsed time calculation */
    TIME_$CLOCK(&current_clock);

    /* Get all SIDs for current process */
    /*
     * 0x00E5AADE-0x00E5AAF2.  The fourth argument is a real 12-byte frame
     * slot at A6-0xB0, not NULL: the callee copies three longwords through it
     * unconditionally (`movea.l (0x14,A6),A2` + three `move.l (A1)+,(A2)+`
     * at 0x00E48802-0x00E4880A).  Nothing in PACCT_$LOG reads it back.
     */
    ACL_$GET_RE_ALL_SIDS(&sids, &prot_info, prot_result, subsys_ids, &status);

    /*
     * Build accounting record
     */

    /* Flags: bit 0 = forked, bit 1 = used superuser */
    record.ac_flags = 0;
    if (*fork_flag & 0x80) {
        record.ac_flags |= 0x01;
    }
    if (*su_flag & 0x80) {
        record.ac_flags |= 0x02;
    }

    /* Exit status - use byte at offset 1 */
    record.ac_stat = *((uint8_t *)exit_status + 1);
    record.ac_pad1 = 0;

    /* Copy SIDs */
    record.ac_uid = sids.user_sid;
    record.ac_gid = sids.group_sid;
    record.ac_org = sids.org_sid;
    record.ac_login = sids.login_sid;
    record.ac_prot_uid = prot_info.prot_uid;

    /* Get device number from TTY UID */
    record.ac_devno = 0;
    {
        /*
         * 0x00E5AC2A-0x00E5AC5C.  Both the size word and the request word are
         * constant cells in the code region; the object-location record the
         * callee rewrites is the frame slot at A6-0x20 and is never read back.
         */
        FILE_$GET_ATTR_INFO(tty_uid, &pacct_$attr_info_req,
                            &pacct_$attr_info_size,
                            &tty_loc, file_info, &status);
        if (status != status_$ok) {
            /* 0x00E5AC52 `moveq #-0x1,D1` - no TTY. */
            record.ac_devno = 0xFFFFFFFF;
        } else {
            /* 0x00E5AC58 `move.w (-0x6e,A6),D1w` - compact record +0x32,
             * zero-extended into the longword field. */
            record.ac_devno = *(const uint16_t *)(const void *)(file_info + 0x32);
        }
    }

    /* I/O counts from proc_times (offsets 0x08 and 0x0C) */
    record.ac_io_read = pacct_$compress(*((uint32_t *)proc_times + 2));
    record.ac_io_write = pacct_$compress(*((uint32_t *)proc_times + 3));

    /* CPU times */
    record.ac_utime = pacct_$compress(*user_time);
    record.ac_stime = pacct_$compress(*sys_time);

    /* Total CPU time in 60ths of a second */
    record.ac_mem = pacct_$compress((*user_time + *sys_time) * 60);

    /* Start time - convert clock to Unix seconds */
    record.ac_btime = CAL_$CLOCK_TO_SEC(start_clock) + UNIX_EPOCH_OFFSET;

    /* Elapsed time - subtract start from current */
    elapsed = current_clock;
    SUB48(&elapsed, start_clock);
    record.ac_elapsed = pacct_$clock_to_comp(&elapsed);

    /* Process UID */
    record.ac_proc_uid = *proc_uid;

    /* Command name - copy up to 32 chars, pad with zeros */
    len = *comm_len;
    if (len > 32) {
        len = 32;
    }
    for (i = 0; i < len; i++) {
        record.ac_comm[i] = comm_ptr[i];
    }
    for (; i < 32; i++) {
        record.ac_comm[i] = 0;
    }

    /* Enter superuser mode for mapping */
    ACL_$ENTER_SUPER();

    /* Check if we need to map a new buffer region */
    if (DAT_00e817f8 < PACCT_RECORD_SIZE) {
        /* Unmap existing buffer if any */
        if (DAT_00e81804 != NULL) {
            MST_$UNMAP_PRIVI(1, &UID_$NIL, ARCH_PTR_TO_VA(DAT_00e81804), DAT_00e81800, 0, &status);
            DAT_00e81804 = NULL;
            DAT_00e81800 = 0;
            DAT_00e817f8 = 0;
        }

        /* Map new 32KB region at current file position */
        map_result = MST_$MAPS(0, 0xFF,     /* flags */
                               &pacct_owner, DAT_00e81808,
                               PACCT_BUFFER_SIZE, 0x16,
                               0, 0xFF, &DAT_00e81800, &status);

        if (status != status_$ok) {
            DAT_00e81804 = NULL;
            DAT_00e817f8 = 0;
            goto exit_super;
        }

        /* Set up buffer pointers */
        DAT_00e817f8 = DAT_00e81800;
        DAT_00e817fc = map_result;
        DAT_00e81804 = map_result;
    }

    /* Copy record to buffer (32 longwords = 128 bytes) */
    {
        uint32_t *src = (uint32_t *)&record;
        uint32_t *dst = DAT_00e817fc;
        for (i = 0; i < 32; i++) {
            *dst++ = *src++;
        }
    }

    /* Update buffer pointers */
    DAT_00e817f8 -= PACCT_RECORD_SIZE;
    DAT_00e817fc += 32;             /* 32 longwords = 128 bytes */
    DAT_00e81808 += PACCT_RECORD_SIZE;

    /* Update file length */
    FILE_$SET_LEN(&pacct_owner, &DAT_00e81808, &status);

exit_super:
    ACL_$EXIT_SUPER();
}
