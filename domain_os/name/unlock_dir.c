/*
 * NAME_$UNLOCK_DIR - Release the directory lock taken by NAME_$LOCK_DIR
 *
 * Unmaps the directory (unless it is one of the four cached mappings, or was
 * never mapped), releases the FILE_$PRIV_LOCK lock, and forgets the
 * per-process lock state.
 *
 * Original address: 0x00E54734
 * Size: 288 bytes
 *
 * Module context: runs with A5 = 0xE7FD24; the per-process tables it reads
 * are the ones NAME_$LOCK_DIR wrote (see name/name_data.c).
 *
 * Parameters:
 *   status_ret - Output: status code
 */

#include "name/name_internal.h"
#include "file/file.h"

void NAME_$UNLOCK_DIR(status_$t *status_ret)
{
    uid_t     local_uid;        /* (-0x10,A6) */
    status_$t unlock_status;    /* (-0x20,A6) */
    uint32_t  dtv_out[3];       /* (-0x1c,A6): FILE_$PRIV_UNLOCK output, 12 bytes */
    uint32_t  handle;           /* D2 */
    int16_t   asid;

    /* 0xE5473C: copy the locked directory's UID out of the per-process table. */
    local_uid = NAME_$LOCK_UID[PROC1_$CURRENT];

    /* 0xE54758 */
    handle = NAME_$LOCK_HANDLE[PROC1_$CURRENT];

    /*
     * 0xE54768: `tst.l (-0x10,A6)` looks at the HIGH longword of the UID only.
     * A zero high half means this process holds no directory lock.
     */
    if (local_uid.high == 0) {
        *status_ret = status_$ok;   /* 0xE5476E */
        return;                     /* 0xE54770 -> 0xE5484A */
    }

    /* 0xE54774: D0 = PROC1_$AS_ID * 16 indexes the per-ASID mapped-info blocks. */
    asid = (int16_t)PROC1_$AS_ID;

    if ((NAME_$DATA.wdir_mapped_info[asid].active < 0 &&
         NAME_$DATA.wdir_mapped_info[asid].first_base == handle) ||     /* 0xE54782 */
        (NAME_$DATA.ndir_mapped_info[asid].active < 0 &&
         NAME_$DATA.ndir_mapped_info[asid].first_base == handle) ||     /* 0xE54794 */
        (NAME_$NODE_MAPPED_INFO.active < 0 &&
         NAME_$NODE_MAPPED_INFO.first_base == handle) ||                /* 0xE547A0 */
        (NAME_$COM_MAPPED_INFO.active < 0 &&
         NAME_$COM_MAPPED_INFO.first_base == handle) ||                 /* 0xE547B0 */
        handle == 0) {                                                  /* 0xE547C0 */
        /* 0xE547C4: a cached (or absent) mapping - leave it mapped. */
        *status_ret = status_$ok;
    } else {
        /* 0xE547C8 */
        MST_$UNMAP_PRIVI(3, &local_uid, handle, 0x10000,
                         PROC1_$AS_ID, status_ret);
    }

    /*
     * 0xE547EA-0xE5481E, pushed right to left:
     *   pea (-0x20,A6)              status_ret = &unlock_status
     *   pea (-0x1c,A6)              dtv_out
     *   clr.l / clr.l               rem_node, rem_key
     *   clr.l                       by_key = false, key = 0
     *   move.w PROC1_$AS_ID         asid
     *   move.w (0x13e,A0)           lock_mode = NAME_$LOCK_MODE[cur]
     *   move.l (0x3c,A5,D0w*4)      lock_slot = NAME_$LOCK_SLOT[cur], a full
     *                               longword
     *   pea (-0x10,A6)              file_uid = &local_uid
     */
    (void)FILE_$PRIV_UNLOCK(&local_uid,
                            (int32_t)NAME_$LOCK_SLOT[PROC1_$CURRENT],
                            (uint16_t)NAME_$LOCK_MODE[PROC1_$CURRENT],
                            (uint16_t)PROC1_$AS_ID,
                            0,          /* by_key = false */
                            0,          /* key            */
                            0,          /* rem_key        */
                            0,          /* rem_node       */
                            dtv_out, &unlock_status);

    /*
     * 0xE54828: `clr.l (0x2b8,A0)` clears only the HIGH longword of the
     * per-process UID - that is the half tested on entry, so this is what
     * marks the process as holding no directory lock.
     */
    NAME_$LOCK_UID[PROC1_$CURRENT].high = 0;

    /* 0xE54838: `tst.w (0x2,A2)` - the LOW word of the status longword. */
    if ((*status_ret & 0xFFFFu) == 0) {
        *status_ret = unlock_status;    /* 0xE5483E */
    }

    /* 0xE54842 */
    if (*status_ret != status_$ok) {
        *status_ret |= 0x80000000u;     /* 0xE54846: bset.b #7,(A2) */
    }
}
