/*
 * FILE_$EXPORT_LK - hand one of this process's locks to another process
 *
 * Original address: 0x00E74110, 306 bytes.
 *
 * Copies the caller's lock-object-table (LOT) reference into a free slot of
 * the target process's per-process lock table and bumps the LOT entry's
 * reference count, so both processes now hold the same lock.
 *
 * Frame (A6+):
 *   0x08 file_uid    (long)  the file the lock must be on
 *   0x0C lock_index  (long) -> A3, POINTER to the caller's slot number
 *   0x10 target_proc (long)  UID of the process to export to
 *   0x14 index_out   (long)  the slot number assigned in the target
 *   0x18 status_ret  (long) -> A2
 *
 * `lea (0xe8605c).l,A5` at 0x00E74118 sets a module base that this routine
 * never uses; every table it touches is addressed absolutely.
 */

#include "file/file_internal.h"
#include "proc2/proc2.h"

/*
 * Slot numbers run 1..0x96 here - `cmpi.l #0x96,D0` + `bls` at 0x00E74152
 * accepts 0x96 itself, unlike FILE_$CHECK_PROT's `bcc`.
 */
#define FILE_EXPORT_LK_MAX_SLOT     0x96

/* 150 iterations: `move.w #0x95,D1w` + `dbf` at 0x00E741CC / 0x00E74228. */
#define FILE_EXPORT_LK_SLOT_COUNT   150

/*
 * 0x00E74242: the operand of the `pea (0x112,PC)` at 0x00E7412E - a Pascal
 * constant handed to PROC2_$FIND_ASID by reference.  The raw byte there is
 * 0xFF, i.e. Domain TRUE, which is the arm PROC2_$FIND_ASID tests with
 * `tst.b (A0)` / `bpl` at 0x00E4075A: TRUE makes it return the process's UPID
 * (proc2 record +0x4C when bit 11 of +0xBA is set) rather than the plain ASID
 * at +0x4E.
 */
static const boolean file_$export_lk_want_upid_00e74242 = true;

void FILE_$EXPORT_LK(uid_t *file_uid, uint32_t *lock_index,
                     uid_t *target_proc, int32_t *index_out,
                     status_$t *status_ret)
{
    uint16_t  current_asid;         /* D2w, before it is reused */
    uint16_t  target_asid;          /* D3w */
    uint32_t  slot_num;             /* D0 */
    uint16_t  entry_index;          /* D2w, after the table read */
    file_lock_entry_detail_t *entry;/* A3, after it is reused */
    int16_t   slot;                 /* D4w */
    int16_t   remaining;            /* D1w, the dbf counter */

    current_asid = PROC1_$AS_ID;                        /* 0x00E74126 */

    /* 0x00E7412C-0x00E74144 */
    target_asid = PROC2_$FIND_ASID(target_proc,
                                   (int8_t *)&file_$export_lk_want_upid_00e74242,
                                   status_ret);
    if (*status_ret != status_$ok) {
        return;
    }

    /* 0x00E74148-0x00E7415A */
    slot_num = *lock_index;
    if (slot_num == 0 || slot_num > FILE_EXPORT_LK_MAX_SLOT) {
        *status_ret = file_$invalid_arg;                /* 0x000F0014 */
        return;
    }

    /* 0x00E7415E-0x00E74178: the caller's own slot must name a LOT entry. */
    entry_index = FILE_$PROC_LOT_SLOT(current_asid, slot_num);
    if (entry_index == 0) {
        goto not_locked;
    }

    /*
     * 0x00E7417A-0x00E741AC.  A3 = 0xE935CC + entry_index*0x1C addresses the
     * END of the entry, so the original reads the reference count at -4 and
     * the UID at -0x10; FILE_$LOT_ENTRY(n) is the same entry addressed from
     * its start.
     */
    entry = FILE_$LOT_ENTRY(entry_index);
    if ((uint16_t)entry->refcount == 0) {               /* 0x00E74194 */
        goto not_locked;
    }
    if (entry->uid_high != file_uid->high ||
        entry->uid_low  != file_uid->low) {
        goto not_locked;
    }

    /* 0x00E741B8: assume the target's table is full until a slot is found. */
    *status_ret = file_$local_lock_table_full;          /* 0x000F0009 */

    ML_$LOCK(FILE_LOT_ML_LOCK_ID);                      /* 0x00E741C4 */

    slot = 1;                                           /* 0x00E741D0 */
    for (remaining = FILE_EXPORT_LK_SLOT_COUNT - 1; remaining >= 0; remaining--) {
        if (FILE_$PROC_LOT_SLOT(target_asid, slot) == 0) {

            /* 0x00E741EE-0x00E741FC */
            FILE_$PROC_LOT_SLOT(target_asid, slot) = entry_index;

            /* 0x00E741FE-0x00E74214: keep the target's high-water mark. */
            if (slot > (int16_t)FILE_$PROC_LOT_COUNT(target_asid)) {
                FILE_$PROC_LOT_COUNT(target_asid) = (uint16_t)slot;
            }

            entry->refcount++;                          /* 0x00E74216 */
            *status_ret = status_$ok;                   /* 0x00E7421A */
            /* `move.l D0,(A1)` - D0 is the sign-extended slot number. */
            *index_out = (int32_t)slot;                 /* 0x00E74220 */
            break;
        }
        slot++;                                         /* 0x00E74224 */
    }

    ML_$UNLOCK(FILE_LOT_ML_LOCK_ID);                    /* 0x00E74232 */
    return;

not_locked:                                             /* 0x00E741AE */
    *status_ret = file_$object_not_locked_by_this_process;   /* 0x000F0005 */
}
