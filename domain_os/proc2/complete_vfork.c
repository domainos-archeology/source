/*
 * PROC2_$COMPLETE_VFORK - Complete a vfork in the child
 *
 * Re-emitted from the image (0x00E73638..0x00E7385E, 552 bytes).
 *
 * The vforked child has been running in its parent's address space.
 * This routine gives it the alternate ASID PROC2_$FORK reserved, moves
 * the two UID-table slots around, copies the user FIM handler, maps the
 * initial area and the stack, wakes the parent and enters the new
 * program through FIM_$PROC2_STARTUP.  Reached only through the SVC
 * table entry at 0x00E7BE0A.
 *
 * Frame (link.w A6,-0x48):
 *   (0x8,A6)  proc_uid     -> copied to A6-0x18 (uid_t)
 *   (0xC,A6)  code_desc    -> *ptr to A6-0x3C
 *   (0x10,A6) map_param    -> *ptr to A6-0x38
 *   (0x14,A6) entry_point  -> A2, D4 = *A2
 *   (0x18,A6) user_data    -> A3, D3 = *A3
 *   (0x1C,A6) reserved1    never read
 *   (0x20,A6) reserved2    never read
 *   (0x24,A6) status_ret   written only on the "wasn't vforked" exit
 *   A6-0x20   status for MST_$MAP_INITIAL_AREA / NAME_$INIT_ASID
 *   A6-0x10   { user_data, entry_point } startup context
 *
 * The current entry is A3 = 0xEA551C + idx*0xE4 = entry + 0xE4:
 * (-0xBA,A3) = +0x2A flags, (-0x4E,A3) = +0x96 asid, (-0x4C,A3) = +0x98
 * asid_alt, (-0x7C,A3) = +0x68 cr_rec, (-0x78,A3) = +0x6C cr_rec_2,
 * (-0xDC,A3) = +0x08 parent_uid, (-0xCC,A3) = +0x18, (-0xC6,A3) = +0x1E,
 * (-0xC8,A3) = +0x1C self_index, (-0x8,A3) = +0xDC stack_uid.
 *
 * Original address: 0x00e73638
 */

#include "proc2/proc2_internal.h"

/*
 * Constant cells passed by reference to MST_$MAP_AREA_AT (cell = pea
 * address + 2 + displacement, bytes read from the image):
 *   0x00E735F4  00 00 40 00  `pea (-0x20e,PC)` at 0x00E73800 -> argument 3,
 *               the longword the callee reads with move.l (0x00E43C28);
 *               shared with PROC2_$SET_VALID (proc2_$map_area_size_00e735f4)
 *   0x00E73860  ff           `pea (0x62,PC)` at 0x00E737FC -> argument 4,
 *               the boolean byte the callee reads with move.b/tst.b (A4)
 *               (0x00E43C22 / 0x00E43C4E); the following 00 4e 56 bytes
 *               are the next function's prologue
 */
static const int8_t proc2_$map_area_true_00e73860 = (int8_t)0xFF;

void PROC2_$COMPLETE_VFORK(uid_t *proc_uid, uint32_t *code_desc, uint32_t *map_param,
                           int32_t *entry_point, int32_t *user_data,
                           uint32_t reserved1, uint32_t reserved2,
                           status_$t *status_ret)
{
    uid_t local_uid;                 /* A6-0x18 */
    uint32_t local_code_desc;        /* A6-0x3C */
    uint32_t local_map_param;        /* A6-0x38 */
    int32_t local_entry_point;       /* D4 */
    int32_t local_user_data;         /* D3 */
    status_$t status;                /* A6-0x20 */
    int16_t current_idx;
    proc2_info_t *current_entry;     /* A3 (biased) */
    proc2_info_t *parent_entry;
    uint16_t old_asid;               /* D2 */
    void *user_fim_addr;
    cr_rec_t *cr_rec;                /* A2 */
    struct {
        int32_t user_data;           /* A6-0x10 */
        int32_t entry_point;         /* A6-0xC */
    } startup_context;

    (void)reserved1;   /* (0x1C,A6): never read */
    (void)reserved2;   /* (0x20,A6): never read */

    /* 0x00E73640-0x00E73668 */
    local_uid.high = proc_uid->high;
    local_uid.low = proc_uid->low;
    local_code_desc = *code_desc;
    local_map_param = *map_param;
    local_entry_point = *entry_point;
    local_user_data = *user_data;

    /* 0x00E73666/0x00E7366A-0x00E73674 */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E73676-0x00E73692 */
    current_idx = (int16_t)P2_PID_TO_INDEX(PROC1_$CURRENT);
    current_entry = P2_INFO_ENTRY(current_idx);

    /* 0x00E73696-0x00E7369E: btst.l #0xb on the flags word = 0x0800 */
    if ((current_entry->flags & PROC2_FLAG_ALT_ASID) == 0) {
        /* 0x00E736A0-0x00E736B6 */
        *status_ret = status_$proc2_process_wasnt_vforked;
        ML_$UNLOCK(PROC2_LOCK_ID);
        return;                                     /* 0x00E736B6: bra exit */
    }

    /*
     * 0x00E736BA-0x00E736C4: D2 = asid; asid = asid_alt; asid_alt = 0.
     * From here on the "new" ASID is read back from entry+0x96.
     */
    old_asid = current_entry->asid;
    current_entry->asid = current_entry->asid_alt;
    current_entry->asid_alt = 0;

    /* 0x00E736C8: move.l D3,(-0x7c,A3) -- entry+0x68 (cr_rec) = user_data */
    current_entry->cr_rec = (uint32_t)local_user_data;

    /* 0x00E736CC-0x00E736D4: entry+0x08 = the UID argument */
    current_entry->parent_uid.high = local_uid.high;
    current_entry->parent_uid.low = local_uid.low;

    /*
     * 0x00E736D8: bclr.b #0x3,(-0xba,A3) -- bit 3 of the HIGH byte of the
     * flags word at +0x2A, i.e. flags &= ~0x0800 (the ALT_ASID bit).
     */
    current_entry->flags &= (uint16_t)~PROC2_FLAG_ALT_ASID;

    /* 0x00E736DE: clr.w (-0xcc,A3) -- entry+0x18 */
    current_entry->pad_18[0] = 0;

    /*
     * 0x00E736E2-0x00E736F6: PROC2_$UID[new asid] = entry->uid
     * (A4 = 0xE7BE84, slot at (0x10,A4,asid*8) = 0xE7BE94 + asid*8).
     */
    PROC2_$UID[current_entry->asid].high = current_entry->uid.high;
    PROC2_$UID[current_entry->asid].low = current_entry->uid.low;

    /*
     * 0x00E736FA-0x00E73712: PROC2_$UID[old asid] = parent->uid, the
     * parent being P2[entry+0x1E] (mulu.w, then -0xE4 to reach its base).
     */
    parent_entry = P2_INFO_ENTRY((int16_t)current_entry->parent_pgroup_idx);
    PROC2_$UID[old_asid].high = parent_entry->uid.high;
    PROC2_$UID[old_asid].low = parent_entry->uid.low;

    /* 0x00E73716-0x00E73722: FIM_$FP_INIT(entry->asid) with a result slot */
    FIM_$FP_INIT((int16_t)current_entry->asid);

    /*
     * 0x00E73724-0x00E73742: A6-0x46 = old*4, D0 = new*4;
     * FIM_$USER_FIM_ADDR[new] = FIM_$USER_FIM_ADDR[old]; the move.l sets
     * the flags, so `beq` skips the quit-inhibit clear when it is zero.
     * 0x00E73744-0x00E7374E: FIM_$QUIT_INH[new asid] = 0 (byte array).
     */
    user_fim_addr = FIM_$USER_FIM_ADDR[old_asid];
    FIM_$USER_FIM_ADDR[current_entry->asid] = user_fim_addr;
    if (user_fim_addr != NULL) {
        FIM_$QUIT_INH[current_entry->asid] = 0;
    }

    /*
     * 0x00E73752-0x00E73774, pushes right to left with a result slot:
     *   pea (-0x20,A6)          arg 7  &status
     *   st -(SP)                arg 6  touch = TRUE
     *   move.w #0x7,-(SP)       arg 5  area_kind = 7
     *   move.l (-0x38,A6),-(SP) arg 4  map_param
     *   pea (-0x18,A6)          arg 3  &local_uid
     *   move.w (-0x4e,A3),-(SP) arg 2  entry->asid
     *   move.l (-0x3c,A6),-(SP) arg 1  code_desc
     */
    MST_$MAP_INITIAL_AREA(local_code_desc, current_entry->asid, &local_uid,
                          local_map_param, 7, (boolean)0xFF, &status);

    /* 0x00E73778: tst.w (-0x1e,A6) -- low word of status */
    if ((status & 0xFFFF) != 0) {
        goto error_cleanup;                         /* 0x00E7377C */
    }

    /* 0x00E73780-0x00E7378A: entry+0xDC (stack_uid) = UID_$NIL */
    current_entry->stack_uid.high = UID_$NIL.high;
    current_entry->stack_uid.low = UID_$NIL.low;

    /* 0x00E7378E-0x00E7379C: NAME_$INIT_ASID(&entry->asid, &status) */
    NAME_$INIT_ASID((int16_t *)&current_entry->asid, &status);

    /* 0x00E7379E: tst.w (-0x1e,A6) */
    if ((status & 0xFFFF) != 0) {
        goto error_cleanup;                         /* 0x00E737A2 */
    }

    /*
     * 0x00E737A6-0x00E737C2: EC_$ADVANCE(&PROC2_$EC[entry+0x1C - 1].fork_ec)
     * (D1 = idx*8, D2 = D1*2, D1 += D2 -> idx*24; pea (-0x18,A1,D1)).
     */
    EC_$ADVANCE(PROC_FORK_EC(current_entry->self_index));

    /* 0x00E737C4-0x00E737D0: PROC1_$SET_ASID(entry->asid) with result slot */
    PROC1_$SET_ASID(current_entry->asid);

    /* 0x00E737D2-0x00E737DE */
    ML_$UNLOCK(PROC2_LOCK_ID);

    /*
     * 0x00E737E0-0x00E737EC: A2 = entry+0x6C used as a pointer to the
     * creation record; fill its stack descriptor from AS_$INFO.
     */
    cr_rec = (cr_rec_t *)ARCH_VA_TO_PTR(current_entry->cr_rec_2);
    cr_rec->addr_lo = AS_$STACK_FILE_LOW;           /* 0x00E737E4: 0xE2B92C */
    cr_rec->size = AS_$INIT_STACK_FILE_SIZE;        /* 0x00E737EC: 0xE2B960 */

    /*
     * 0x00E737F4-0x00E73812, pushes right to left:
     *   pea (0x94,A2)           arg 6  &cr_rec->status
     *   pea (-0x8,A3)           arg 5  &entry->stack_uid
     *   pea 0x00E73860          arg 4  &TRUE
     *   pea 0x00E735F4          arg 3  &0x4000
     *   pea (0xb4,A2)           arg 2  &cr_rec->size
     *   pea (0xb0,A2)           arg 1  &cr_rec->addr_lo
     */
    MST_$MAP_AREA_AT(&cr_rec->addr_lo, &cr_rec->size,
                     (void *)&proc2_$map_area_size_00e735f4,
                     (void *)&proc2_$map_area_true_00e73860,
                     &current_entry->stack_uid, &cr_rec->status);

    /* 0x00E73816-0x00E7381E: cr_rec+0xA8 = entry->stack_uid */
    cr_rec->stack_uid.high = current_entry->stack_uid.high;
    cr_rec->stack_uid.low = current_entry->stack_uid.low;

    /* 0x00E73822: tst.l (0x94,A2) / beq -- a plain jsr: if PROC2_$DELETE
     * ever returned, execution would continue below */
    if (cr_rec->status != status_$ok) {
        PROC2_$DELETE();                            /* 0x00E73828 */
    }

    /* 0x00E7382E-0x00E73840: FIM_$PROC2_STARTUP(&{user_data, entry_point}) */
    startup_context.user_data = local_user_data;
    startup_context.entry_point = local_entry_point;
    FIM_$PROC2_STARTUP(&startup_context);

    /*
     * There is no branch after the startup call: the code falls straight
     * into the error tail at 0x00E73842.
     */
error_cleanup:
    /* 0x00E73842-0x00E73850 */
    ML_$UNLOCK(PROC2_LOCK_ID);
    PROC2_$DELETE();
    /* 0x00E73856: epilogue */
}
