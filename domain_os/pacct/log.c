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

#include "pacct_internal.h"

/*
 * Extended SID structure from ACL_$GET_RE_ALL_SIDS
 */
typedef struct all_sids_t {
    uid_t user_sid;     /* 0x00: User SID */
    uid_t group_sid;    /* 0x08: Group SID */
    uid_t org_sid;      /* 0x10: Org SID */
    uid_t login_sid;    /* 0x18: Login SID */
    /* Additional data follows */
} all_sids_t;

typedef struct prot_info_t {
    uid_t prot_uid;     /* 0x00: Protection UID */
    uid_t unused;       /* 0x08: Unused */
} prot_info_t;

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
    all_sids_t sids;
    prot_info_t prot_info;
    int32_t prot_result[3];
    uint8_t attr_buf[32];
    uint8_t file_info[80];
    uint16_t devno;
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
    ACL_$GET_RE_ALL_SIDS(&sids, &prot_info.prot_uid, prot_result, NULL, &status);

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
        int16_t attr_size = 0x7A;
        int16_t attr_flags = 0;
        FILE_$GET_ATTR_INFO(tty_uid, &attr_flags, &attr_size,
                            (uint32_t *)attr_buf, file_info, &status);
        if (status == status_$ok) {
            /* Device number at offset 0x32 in file_info */
            record.ac_devno = *(uint16_t *)(file_info + 0x32);
        } else {
            record.ac_devno = 0xFFFFFFFF;   /* No TTY */
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
            MST_$UNMAP_PRIVI(1, &UID_$NIL, DAT_00e81804, DAT_00e81800, 0, &status);
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
