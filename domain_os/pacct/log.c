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
 * A6-0x110, A6-0xE8, A6-0xC0 and A6-0xB0 (0x00E5AADE-0x00E5AAF2), and copies
 * the FIRST (A6-0x110, 0x00E5AB30) and the THIRD (A6-0xC0, 0x00E5AB42) into
 * the record.  pacct_sid_block_t / pacct_prot_block_t are in pacct/pacct.h.
 */

/*
 * Constant cells in the code region, passed by reference to
 * FILE_$GET_ATTR_INFO at 0x00E5AC36 (`pea (-0x380,PC)` -> 0xE5A8B8) and
 * 0x00E5AC3A (`pea (0xf8,PC)` -> 0xE5AD34).  The image holds 0x007A - the
 * compact record's size - and 0x0004, whose second byte sets bit 2, i.e.
 * "do not run the FILE_$DELETE_INT probe".
 */
static int16_t  pacct_$attr_info_size = FILE_ATTR_INFO_SIZE;   /* 0xE5A8B8 */
static uint16_t pacct_$attr_info_req  = 0x0004;                /* 0xE5AD34 */

/* Unix epoch offset - difference between Domain/OS epoch and Unix epoch */
#define UNIX_EPOCH_OFFSET   0x12CEA600

void PACCT_$LOG(boolean *fork_flag, boolean *su_flag, int16_t *exit_status,
                clock_t *start_clock, const uint32_t *proc_times,
                const uint32_t *io_write_count, const uint32_t *io_read_count,
                uid_t *tty_uid, const uid_t *proc_uid,
                const char *comm_ptr, const int16_t *comm_len)
{
    status_$t status;
    clock_t current_clock;                  /* A6-0x19C */
    pacct_sid_block_t sids;                 /* A6-0x110 */
    pacct_sid_block_t cur_sids;             /* A6-0xE8  */
    pacct_prot_block_t prot_info;           /* A6-0xC0  */
    pacct_prot_block_t subsys_info;         /* A6-0xB0  */
    file_$obj_loc_t tty_loc;                /* A6-0x20 */
    uint8_t file_info[FILE_ATTR_INFO_SIZE]; /* A6-0xA0, the 0x7A compact record */
    pacct_record_t record;                  /* A6-0x190 */
    int16_t len;
    int16_t i;
    void *map_result;

    /* Check if accounting is enabled (0x00E5AABE-0x00E5AACE compares the
     * eight bytes at 0x00E817EC against UID_$NIL at 0x00E1737C). */
    if (pacct_owner.high == UID_$NIL.high &&
        pacct_owner.low == UID_$NIL.low) {
        return;
    }

    /* Get current clock; the elapsed time is computed in this same slot. */
    TIME_$CLOCK(&current_clock);

    /*
     * 0x00E5AADE-0x00E5AAF2.  All four output slots are real frame storage:
     * the callee copies through every one of them unconditionally.  Only the
     * first and the third are read back.
     */
    ACL_$GET_RE_ALL_SIDS(&sids, &cur_sids, &prot_info, &subsys_info, &status);

    /*
     * Build accounting record.  Note that ac_pad_03 and ac_pad_74 are never
     * written, so the record copied to the file carries 13 uninitialised
     * stack bytes.  That is the original behaviour; do not "fix" it.
     */

    /* Flags: bit 0 = arg1's boolean, bit 1 = arg2's boolean
     * (0x00E5AAFC `clr.w`, then 0x00E5AB08 / 0x00E5AB1A). */
    record.ac_flags = 0;
    if (*fork_flag < 0) {
        record.ac_flags |= 0x0001;
    }
    if (*su_flag < 0) {
        record.ac_flags |= 0x0002;
    }

    /* Exit status: the LOW byte of the status word
     * (0x00E5AB2A `move.b (0x1,A0),(-0x18e,A6)`). */
    record.ac_stat = (uint8_t)(*exit_status & 0x00FF);

    /* 36-byte SID block (0x00E5AB30) and 12-byte protection block
     * (0x00E5AB42, sourced from the THIRD argument, A6-0xC0). */
    record.ac_sids = sids;
    record.ac_prot = prot_info;

    /* 0x00E5AB50 `clr.l (-0x15c,A6)` clears the device number first. */
    record.ac_devno = 0;

    /* 0x00E5AB54 `clr.l (-0x14e,A6)`. */
    record.ac_zero_42 = 0;

    /*
     * I/O counters, from PROC1_$STATS[PROC1_$CURRENT]: arg6 is the
     * pages-written cell (+0x08) and arg7 the pages-read cell (+0x0C).
     * 0x00E5AB58-0x00E5AB6C.
     */
    record.ac_io_write = pacct_$compress(*io_write_count);
    record.ac_io_read  = pacct_$compress(*io_read_count);

    /*
     * 0x00E5AB70-0x00E5AB86: D0 = (*arg6 + *arg7) << 2, then
     * (D0 << 4) - D0, i.e. 60 * (*arg6 + *arg7).
     */
    record.ac_mem = pacct_$compress((*io_write_count + *io_read_count) * 60u);

    /* CPU times out of the process record (0x00E5AB8A / 0x00E5AB9C read
     * proc_times[+0x08] and proc_times[+0x0C]). */
    record.ac_utime = pacct_$compress(proc_times[2]);
    record.ac_stime = pacct_$compress(proc_times[3]);

    /* Start time - convert clock to Unix seconds (0x00E5ABAA-0x00E5ABBA). */
    record.ac_btime = CAL_$CLOCK_TO_SEC(start_clock) + UNIX_EPOCH_OFFSET;

    /*
     * Elapsed time.  SUB48's first argument is the destination
     * (0x00E172E4 `movea.l (0x4,SP),A0`), and PACCT_$LOG pushes
     * `move.l D2,-(SP)` (start_clock) before `pea (-0x19c,A6)`, so the
     * subtraction runs in the current_clock slot: current_clock -= *start_clock.
     */
    SUB48(&current_clock, start_clock);
    record.ac_etime = pacct_$clock_to_comp(&current_clock);

    /* Process UID (0x00E5ABDE / 0x00E5ABE2, two longwords). */
    record.ac_proc_uid = *proc_uid;

    /* Command name - copy up to 32 chars, zero-fill the rest
     * (0x00E5ABE6-0x00E5AC2A). */
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

    /* Get device number from the TTY object's attributes. */
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
            record.ac_devno = 0xFFFFFFFFu;
        } else {
            /* 0x00E5AC58 `move.w (-0x6e,A6),D1w` - compact record +0x32,
             * zero-extended into the longword field. */
            record.ac_devno = *(const uint16_t *)(const void *)(file_info + 0x32);
        }
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
