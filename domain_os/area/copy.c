/*
 * AREA_$COPY - Copy an area (copy-on-write)
 *
 * Original address: 0x00E0901A .. 0x00E0939A (898 bytes)
 */

#include "area/area_internal.h"
#include "misc/crash_system.h"

/*
 * find_seg_table - the overflow-table search AREA_$COPY emits INLINE, twice
 *
 * 0x00E091C6-0x00E091F0 (source area) and 0x00E09230-0x00E0925A
 * (destination area) are the same five instructions: index
 * AREA_$GLOBALS.seg_table_list[] with the entry's owner_asid scaled by four
 * (`lsl.w #0x2,D0w` / `lea (0x0,A5,D0w*0x1),A1` / `movea.l (0x68,A1),A2`),
 * then walk the `next` chain (+0x04) until both the area id (+0x00, a word)
 * and the table index (+0x02, a BYTE zero-extended to a word) match.
 *
 * This is deliberately NOT a call to area_$lookup_seg_table (0x00E09D2E):
 * that routine takes ML lock 0x12 and allocates a fresh table when none is
 * found, which is not what these two loops do.
 */
static area_$seg_table_t *find_seg_table(int16_t asid, int16_t area_id,
                                         uint16_t table_idx)
{
    area_$seg_table_t *tbl = AREA_$GLOBALS.seg_table_list[asid];

    /* 0x00E091EC `cmpa.w #0x0,A2` - the chain terminator */
    while (tbl != NULL) {
        if (tbl->area_id == area_id &&
            (uint16_t)tbl->table_index == table_idx) {
            break;
        }
        tbl = tbl->next;
    }

    return tbl;
}

/*
 * AREA_$COPY - Copy an area (copy-on-write)
 *
 * Creates a copy of an area with copy-on-write semantics: a fresh area is
 * created in `new_asid` and every allocated segment of the source is handed
 * to AST_$COPY_AREA, which shares the physical pages until one side writes.
 *
 * This is what process fork uses to duplicate an address space.
 *
 * Parameters (the prologue at 0x00E09028-0x00E09034 reads them):
 *   gen          A6+0x08 word - source area generation
 *   area_id      A6+0x0A word - source area id
 *   new_asid     A6+0x0C word - owner ASID of the copy (D6)
 *   param_4      A6+0x0E word - handed straight to AST_$COPY_AREA
 *                (0x00E09320) and used for nothing else
 *   stack_limit  A6+0x10 long - the top of the stack region (D2); segments
 *                between AS_$STACK_LOW and this address are NOT copied
 *   status_ret   A6+0x14 long - output status (A4)
 *
 * Returns: the handle of the new area (generation << 16 | area id).
 *
 * Original address: 0x00E0901A
 */
uint32_t AREA_$COPY(int16_t gen, uint16_t area_id, int16_t new_asid,
                    int16_t param_4, uint32_t stack_limit,
                    status_$t *status_ret)
{
    area_$entry_t *src_entry;
    area_$entry_t *dst_entry;
    uint32_t new_handle;
    uint16_t new_area_id;
    int8_t is_reversed;
    uint32_t src_virt_size;
    uint16_t stack_low_page;
    uint16_t stack_high_page;
    uint16_t bitmap_bytes;
    uint16_t byte_idx;
    uint16_t loop_count;
    int16_t seg_counter;
    int16_t seg_direction;
    uint16_t seg_page;
    area_$seg_slot_t *src_map;
    area_$seg_slot_t *dst_map;
    area_$seg_slot_t *src_slot;
    area_$seg_slot_t *dst_slot;
    status_$t delete_status;

    /*
     * A6-0x2C, the frame slot the epilogue at 0x00E0938E returns.  It is
     * written exactly once, at 0x00E090D4, AFTER area_$internal_create has
     * succeeded; the three early exits (0x00E0906C bad id / not active,
     * 0x00E0908A not owner, 0x00E090C2 create failed) all fall into that
     * same epilogue and hand the caller whatever the slot happened to hold.
     * C cannot spell "uninitialised" without undefined behaviour, so the
     * slot is zeroed here - callers must look at *status_ret, exactly as
     * they must in the image.
     */
    uint32_t result = 0;

    /* 0x00E09034-0x00E0903E: 0 < area_id <= AREA_$N_AREAS (unsigned) */
    if (area_id == 0 || area_id > (uint16_t)AREA_$N_AREAS) {
        *status_ret = status_$area_not_active;
        return result;
    }

    /* 0x00E09040-0x00E09056: base + id*0x30 - 0x30 */
    src_entry = AREA_ID_TO_ENTRY(area_id);

    /*
     * 0x00E0905A-0x00E0906A: `lsr.b #0x1,D0b` tests AREA_FLAG_ACTIVE (bit 0
     * of the flags word), then the generation must match.
     */
    if ((src_entry->flags & AREA_FLAG_ACTIVE) == 0 ||
        src_entry->generation != gen) {
        *status_ret = status_$area_not_active;
        return result;
    }

    /* 0x00E09076-0x00E09090 */
    if (src_entry->remote_uid == 0 &&
        PROC1_$AS_ID != 0 &&
        PROC1_$AS_ID != src_entry->owner_asid) {
        *status_ret = status_$area_not_owner;
        return result;
    }

    /* 0x00E09094-0x00E0909C: `btst.l #0x1,D0` / `sne D5b` */
    is_reversed = (src_entry->flags & AREA_FLAG_REVERSED) ? (int8_t)-1 : 0;

    /* 0x00E0909E-0x00E090B8 */
    new_handle = area_$internal_create(src_entry->virt_size,
                                       src_entry->commit_size,
                                       0,               /* remote_uid */
                                       new_asid,
                                       1,               /* alloc_remote */
                                       is_reversed,
                                       status_ret);

    /* 0x00E090C0: the CALLER's status cell is the one tested */
    if (*status_ret != status_$ok) {
        return result;
    }

    /* 0x00E090C6-0x00E090D2: both limits are page-of-32K numbers */
    stack_low_page = (uint16_t)(AS_$STACK_LOW >> 15);
    stack_high_page = (uint16_t)(stack_limit >> 15);

    /* 0x00E090D4: the return slot is filled in here and nowhere else */
    result = new_handle;

    /* 0x00E090D8-0x00E090EE */
    new_area_id = (uint16_t)new_handle;
    dst_entry = AREA_ID_TO_ENTRY(new_area_id);

    /*
     * 0x00E090F6-0x00E090FA: a zero-sized source skips the whole copy but
     * still falls into the common tail at 0x00E0937A, which clears
     * AREA_FLAG_IN_TRANS on the source (never set on this path) and advances
     * AREA_$IN_TRANS_EC.  There is no early return here.
     */
    src_virt_size = src_entry->virt_size;
    if (src_virt_size == 0) {
        goto finish;
    }

    /*
     * 0x00E090FE-0x00E09114: bitmap_bytes = ((((virt_size >> 10) + 0x1F)
     * >> 5) truncated to a word, + 7) >> 3.  The truncation is the
     * `clr.l D4` / `move.w D0w,D4w` pair at 0x00E09102/0x00E09110.
     */
    {
        uint32_t segs = ((src_virt_size >> 10) + 0x1F) >> 5;
        bitmap_bytes = (uint16_t)(((uint32_t)(uint16_t)segs + 7) >> 3);
    }

    /* 0x00E09116-0x00E0911C */
    dst_entry->flags = src_entry->flags;
    dst_entry->remote_uid = src_entry->remote_uid;

    /* 0x00E09122-0x00E09156: claim the source's in-transition bit */
    ML_$LOCK(ML_LOCK_AREA);
    while ((src_entry->flags & AREA_FLAG_IN_TRANS) != 0) {
        area_$wait_in_trans();
    }
    src_entry->flags |= AREA_FLAG_IN_TRANS;
    ML_$UNLOCK(ML_LOCK_AREA);

    /* 0x00E09158-0x00E09160 */
    dst_entry->first_bste = new_asid;
    dst_entry->first_seg_index = src_entry->first_seg_index;

    /* 0x00E09166-0x00E0917E */
    seg_counter = 0;
    seg_page = (uint16_t)src_entry->first_seg_index;
    seg_direction = (is_reversed < 0) ? -1 : 1;

    /*
     * 0x00E09180-0x00E09184: `move.w D4w,D0w` / `subq.w #0x1,D0w` / `bmi`.
     * A zero byte count skips straight to the tail.
     */
    if ((int16_t)(bitmap_bytes - 1) < 0) {
        goto finish;
    }
    loop_count = (uint16_t)(bitmap_bytes - 1);

    /*
     * 0x00E0918C-0x00E09198: byte_idx and the two four-byte cursors.  The
     * image keeps the cursors in the frame at A6-0x50 / A6-0x54 and bumps
     * both by four per outer pass (0x00E0936A/0x00E0936E); indexing the slot
     * arrays by byte_idx computes exactly the same addresses.
     */
    byte_idx = 0;
    src_map = (area_$seg_slot_t *)&src_entry->seg_bitmap[0];
    dst_map = (area_$seg_slot_t *)&dst_entry->seg_bitmap[0];

    do {
        int bit;

        /* 0x00E0919C: the first two cells live in the entry itself */
        if (byte_idx < 2) {
            /* 0x00E091A8/0x00E091B4: `lea (0x18,A1),A3` */
            src_slot = &src_map[byte_idx];
            dst_slot = &dst_map[byte_idx];
        } else {
            area_$seg_table_t *tbl;
            uint16_t table_idx;
            int16_t table_offset;

            /* 0x00E091BC-0x00E091C4: `lsr.w #0x8,D4w` */
            table_idx = (uint16_t)(byte_idx >> 8);

            /* 0x00E091C6-0x00E091F0 */
            tbl = find_seg_table(src_entry->owner_asid, (int16_t)area_id,
                                 table_idx);
            if (tbl == NULL) {
                /* 0x00E091F8: pea (-0x137e,PC) -> 0x00E07E7C */
                CRASH_SYSTEM(&Area_Internal_Error);
            }

            /* 0x00E09204-0x00E0921C: (byte_idx - 2) mod 0x100, then * 4 */
            table_offset = (int16_t)(M$OIS$WLW((int32_t)byte_idx - 2, 0x100) << 2);

            /* 0x00E0921E-0x00E09228 */
            src_slot = (area_$seg_slot_t *)((char *)tbl->bitmap_ptr
                                            + table_offset);

            /* 0x00E0922C-0x00E0925A */
            tbl = find_seg_table(dst_entry->owner_asid, (int16_t)new_area_id,
                                 table_idx);
            if (tbl == NULL) {
                /* 0x00E09262: the same status cell */
                CRASH_SYSTEM(&Area_Internal_Error);
            }

            /* 0x00E0926E-0x00E09272 */
            dst_slot = (area_$seg_slot_t *)((char *)tbl->bitmap_ptr
                                            + table_offset);
        }

        /* 0x00E0927A: `moveq #0x7,D5` + the dbf at 0x00E09362 = 8 passes */
        for (bit = 0; bit < 8; bit++) {
            /*
             * 0x00E0927E-0x00E09288: the bits byte is re-read every pass
             * (area_$get_aste writes the slot).
             */
            if ((src_slot->bits & (1u << bit)) != 0 &&
                /*
                 * 0x00E0928C-0x00E09292: word compares.  A segment inside
                 * [stack_low_page, stack_high_page) is skipped.
                 */
                (seg_page < stack_low_page || seg_page >= stack_high_page)) {
                struct aste_t *src_aste;
                struct aste_t *dst_aste;

                /* 0x00E09296: ML lock 0x12, the AST lock */
                ML_$LOCK(ML_LOCK_AST);

                /* 0x00E092A4-0x00E092BE: the CALLER's status cell again */
                src_aste = area_$get_aste((int16_t)area_id, src_slot,
                                          seg_counter, 0, (int8_t)-1,
                                          status_ret);
                if (*status_ret != status_$ok) {
                    /* 0x00E092C4 */
                    CRASH_SYSTEM(&Area_Internal_Error);
                }

                /* 0x00E092D0-0x00E092EA */
                dst_aste = area_$get_aste((int16_t)new_area_id, dst_slot,
                                          seg_counter, 0, (int8_t)-1,
                                          status_ret);
                if (*status_ret != status_$ok) {
                    /* 0x00E092F0 */
                    CRASH_SYSTEM(&Area_Internal_Error);
                }

                /* 0x00E092FC */
                ML_$UNLOCK(ML_LOCK_AST);

                /*
                 * 0x00E0930A-0x00E0932E.  The sixth argument is the virtual
                 * address of the segment, seg_page << 15, passed BY VALUE
                 * (0x00E0930E-0x00E09316 `clr.l D0` / `move.w D6w,D0w` /
                 * `lsl.l #0x8` / `lsl.l #0x7`); AST_$COPY_AREA walks it as a
                 * byte buffer.  ARCH_VA_TO_PTR is the identity cast on m68k.
                 * The status cell is once more the caller's.
                 */
                AST_$COPY_AREA(area_id, param_4, src_aste, dst_aste,
                               (uint16_t)seg_counter,
                               (char *)ARCH_VA_TO_PTR((uint32_t)seg_page << 15),
                               status_ret);

                /* 0x00E09332/0x00E09336: both ASTEs drop a reference */
                src_aste->wire_count--;
                dst_aste->wire_count--;

                /* 0x00E0933A */
                if (*status_ret != status_$ok) {
                    /*
                     * 0x00E0933E-0x00E09352.  The second argument is the
                     * destination entry's own +0x2A word (`move.w (0x2a,A0)`
                     * with A0 = A6-0x24 = dst_entry), NOT new_area_id, and
                     * the status goes to a scratch local at A6-0x04 so the
                     * caller keeps the AST_$COPY_AREA failure.
                     */
                    area_$internal_delete(dst_entry, dst_entry->reserved_2a,
                                          &delete_status, (int8_t)-1);
                    goto finish;
                }
            }

            /* 0x00E09358-0x00E09360 */
            seg_counter++;
            seg_page = (uint16_t)(seg_page + seg_direction);
        }

        /* 0x00E09366-0x00E09376 */
        byte_idx++;
    } while (loop_count-- != 0);

finish:
    /* 0x00E0937A-0x00E0938C */
    src_entry->flags &= (uint16_t)~AREA_FLAG_IN_TRANS;
    EC_$ADVANCE(&AREA_$IN_TRANS_EC);

    /* 0x00E0938E */
    return result;
}
