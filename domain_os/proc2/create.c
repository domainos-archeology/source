/*
 * PROC2_$CREATE - Create a new process
 *
 * Re-emitted from the image (0x00E726EC..0x00E72BCC, 1250 bytes).
 *
 * Takes a slot from the free list, gives it an ASID, initialises it,
 * maps the initial area, allocates a PROC1 stack, binds a PROC1 process
 * whose entry is PROC2_$STARTUP, links the slot under the caller,
 * inherits the debugger, initialises the slot's eventcount pair and
 * registers the fork eventcount, then finishes with ACL/audit/name
 * initialisation and the priority/type setup.  Reached only through the
 * SVC table entry at 0x00E7BE36.
 *
 * Frame (link.w A6,-0x5C):
 *   (0x8,A6)  parent_uid   -> copied to A6-0x8 (uid_t)
 *   (0xC,A6)  code_desc    -> D7 = *ptr   (stored at entry+0x6C)
 *   (0x10,A6) map_param    -> A6-0x38 = *ptr
 *   (0x14,A6) entry_point  -> D5 = *ptr   (startup context +0x08)
 *   (0x18,A6) user_data    -> D3 = *ptr   (entry+0x68, context +0x04)
 *   (0x1C,A6) reserved1    never read
 *   (0x20,A6) reserved2    never read
 *   (0x24,A6) flags        -> D4 = *(byte)
 *   (0x28,A6) uid_ret
 *   (0x2C,A6) ec_ret
 *   (0x30,A6) status_ret   (also handed straight to MST_$ALLOC_ASID)
 *   A6-0x10   clock_t from TIME_$CLOCK
 *   A6-0x14   scratch status for PROC1_$UNBIND / MST_$FREE_ASID
 *   A6-0x18   status (never initialised; see the ORIGINAL BUG note)
 *   A6-0x40   max priority, A6-0x42 min priority
 *   D2 new index, D6 stack pointer (0 until allocated), D7 later = context
 *
 * Entries are addressed as A_n = 0xEA551C + idx*0xE4 = entry + 0xE4;
 * A3 = the new entry, A2 = the caller's entry (later reused).
 *
 * Original address: 0x00e726ec
 */

#include "proc2/proc2_internal.h"
#include "time/time.h"

void PROC2_$CREATE(uid_t *parent_uid, uint32_t *code_desc, uint32_t *map_param,
                   int32_t *entry_point, int32_t *user_data,
                   uint32_t reserved1, uint32_t reserved2,
                   uint8_t *flags, uid_t *uid_ret, void **ec_ret,
                   status_$t *status_ret)
{
    uid_t local_parent_uid;          /* A6-0x8 */
    uint32_t local_code_desc;        /* D7 */
    uint32_t local_map_param;        /* A6-0x38 */
    int32_t local_entry_point;       /* D5 */
    int32_t local_user_data;         /* D3 */
    int8_t local_flags;              /* D4 */
    /*
     * A6-0x18.  Written only by the by-reference callees on the paths
     * that reach them; the "process table full" exit skips the final
     * store, but the MST_$ALLOC_ASID failure exit (0x00E727D6 -> 0x00E72B24)
     * does NOT, so that exit returns whatever the slot held.  ORIGINAL BUG,
     * the same shape as PROC2_$FORK's (see proc2/fork.c); left
     * indeterminate on purpose.
     */
    status_$t status;
    status_$t temp_status;           /* A6-0x14 */
    clock_t creation_time;           /* A6-0x10 */
    int16_t new_idx;                 /* D2 */
    int16_t current_idx;
    uint16_t new_pid;
    uint8_t *stack_ptr = NULL;       /* D6 (clr.l D6 at 0x00E7271E) */
    uint16_t new_asid;
    proc2_info_t *new_entry;         /* A3 */
    proc2_info_t *current_entry;     /* A2 */
    startup_context_t *ctx;          /* D7 (reused) */
    uint16_t min_priority;           /* A6-0x42 */
    uint16_t max_priority;           /* A6-0x40 */

    (void)reserved1;   /* (0x1C,A6): never read */
    (void)reserved2;   /* (0x20,A6): never read */

    /* 0x00E726F4-0x00E72722 */
    local_parent_uid.high = parent_uid->high;
    local_parent_uid.low = parent_uid->low;
    local_code_desc = *code_desc;
    local_map_param = *map_param;
    local_entry_point = *entry_point;
    local_user_data = *user_data;
    local_flags = (int8_t)*flags;

    /* 0x00E72720/0x00E72724-0x00E7272E */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E72730-0x00E7273A */
    TIME_$CLOCK(&creation_time);

    /* 0x00E7273C-0x00E72746: D2 = P2_FREE_LIST_HEAD (0xE7BE84+0x1E2) */
    new_idx = (int16_t)P2_FREE_LIST_HEAD;
    if (new_idx == 0) {
        /* 0x00E72748-0x00E7275E: exits at 0x00E72BC4, past the status store */
        *status_ret = status_$proc2_table_full;
        ML_$UNLOCK(PROC2_LOCK_ID);
        return;
    }

    /* 0x00E72762-0x00E7276E: A3 = new entry (muls) */
    new_entry = P2_INFO_ENTRY(new_idx);

    /* 0x00E72772-0x00E7278C: A2 = the caller's entry (mulu) */
    current_idx = (int16_t)P2_PID_TO_INDEX(PROC1_$CURRENT);
    current_entry = P2_INFO_ENTRY(current_idx);

    /*
     * 0x00E72790-0x00E7279C: pop the free list, push onto the allocated
     * list (P2_INFO_ALLOC_PTR at 0xE7BE84+0x1E0).
     */
    P2_FREE_LIST_HEAD = new_entry->next_index;
    new_entry->next_index = P2_INFO_ALLOC_PTR;
    P2_INFO_ALLOC_PTR = (uint16_t)new_idx;

    /*
     * 0x00E727A0-0x00E727AC: the old allocated-list head's back link
     * (+0x14) is written UNCONDITIONALLY -- there is no zero test, so an
     * empty allocated list writes entry(0)+0x14, the word just below the
     * table.
     */
    P2_INFO_ENTRY(new_entry->next_index)->pad_14 = (uint16_t)new_idx;

    /* 0x00E727B0/0x00E727B4: entry+0x14 = 0, entry+0x1E = 0 */
    new_entry->pad_14 = 0;
    new_entry->parent_pgroup_idx = 0;

    /*
     * 0x00E727B8-0x00E727C4: MST_$ALLOC_ASID is given the CALLER's
     * status_ret (0x30,A6), not the local; entry+0x96 = the result.
     */
    new_asid = MST_$ALLOC_ASID(status_ret);
    new_entry->asid = new_asid;

    /* 0x00E727C8-0x00E727D6: tst.w (0x2,A1) / bset.b #0x7,(A1) */
    if ((*status_ret & 0xFFFF) != 0) {
        *status_ret |= (status_$t)0x80000000u;
        goto cleanup_entry;                         /* bra.w 0x00E72B24 */
    }

    /* 0x00E727DA-0x00E727E4: FIM_$FP_INIT(asid) with a result slot */
    FIM_$FP_INIT((int16_t)new_asid);

    /*
     * 0x00E727E6-0x00E727F2: andi.b #0x7f / or.b on the HIGH byte of the
     * flags word at +0x2A: flags = (flags & 0x7FFF) | ((flags_arg & 0x80) << 8).
     */
    new_entry->flags = (uint16_t)((new_entry->flags & 0x7FFF) |
                                  (((uint16_t)local_flags & 0x80) << 8));

    /* 0x00E727F6-0x00E727FE: PROC2_$INIT_ENTRY_INTERNAL(entry base) */
    PROC2_$INIT_ENTRY_INTERNAL(new_entry);

    /* 0x00E72800-0x00E72808: entry+0x08 = the parent UID argument */
    new_entry->parent_uid.high = local_parent_uid.high;
    new_entry->parent_uid.low = local_parent_uid.low;

    /*
     * 0x00E7280C: move.l D7,(-0x78,A3)  entry+0x6C = *code_desc
     * 0x00E72810: move.l D3,(-0x7c,A3)  entry+0x68 = *user_data
     */
    new_entry->cr_rec_2 = local_code_desc;
    new_entry->cr_rec = (uint32_t)local_user_data;

    /*
     * 0x00E72814-0x00E72834, pushes right to left with a result slot:
     *   pea (-0x18,A6)           arg 7  &status
     *   move.l #0x70000,-(SP)    arg 5/6  area_kind 7, touch FALSE
     *   move.l (-0x38,A6),-(SP)  arg 4  map_param
     *   pea (-0x8,A6)            arg 3  &local_parent_uid
     *   move.w (-0x4e,A3),-(SP)  arg 2  entry->asid
     *   move.l D7,-(SP)          arg 1  code_desc
     */
    MST_$MAP_INITIAL_AREA(local_code_desc, new_entry->asid, &local_parent_uid,
                          local_map_param, 7, 0, &status);

    /* 0x00E72838: tst.w (-0x16,A6) */
    if ((status & 0xFFFF) != 0) {
        goto cleanup_asid;                          /* bne.w 0x00E72A6E */
    }

    /* 0x00E72840-0x00E7284A: entry+0xDC (stack_uid) = UID_$NIL */
    new_entry->stack_uid.high = UID_$NIL.high;
    new_entry->stack_uid.low = UID_$NIL.low;

    /* 0x00E7284E-0x00E72860: D6 = PROC1_$ALLOC_STACK(0x1000, &status) */
    stack_ptr = (uint8_t *)PROC1_$ALLOC_STACK(0x1000, &status);

    /* 0x00E72862 */
    if ((status & 0xFFFF) != 0) {
        goto cleanup_asid;
    }

    /*
     * 0x00E7286A-0x00E7288C: D7 = D6 - (0x10 + FIM_$INITIAL_STACK_SIZE);
     * the 14-byte record sits STARTUP_CONTEXT_RESERVE (0x10) below the
     * reserve.
     *   0x00E7287A  ctx+0x04 = D3 (user_data)
     *   0x00E7287E  ctx+0x0C = entry->asid (word)
     *   0x00E72884  ctx+0x08 = D5 (entry_point)
     *   0x00E7288C  ctx+0x00 = ctx + 4
     */
    ctx = (startup_context_t *)(stack_ptr - FIM_$INITIAL_STACK_SIZE
                                - STARTUP_CONTEXT_RESERVE);
    ctx->user_data = local_user_data;
    ctx->asid = new_entry->asid;
    ctx->entry_point = local_entry_point;
    ctx->self_ptr = &ctx->user_data;

    /*
     * 0x00E7288E-0x00E728AA: PROC1_$BIND(PROC2_$STARTUP, ctx, stack, 0,
     * &status) with a result slot; entry+0x9A = the pid.
     */
    new_pid = PROC1_$BIND((void *)PROC2_$STARTUP, ctx, stack_ptr, 0, &status);
    new_entry->level1_pid = new_pid;

    /* 0x00E728AE */
    if ((status & 0xFFFF) != 0) {
        goto cleanup_asid;
    }

    /* 0x00E728B6: bset.b #0x0,(-0xba,A3) -- HIGH byte bit 0 = 0x0100 */
    new_entry->flags |= PROC2_FLAG_BOUND;

    /* 0x00E728BC-0x00E728C8: PROC2_$PID_TO_INDEX[pid] = new index */
    PROC2_$PID_TO_INDEX[new_pid] = (uint16_t)new_idx;

    /*
     * 0x00E728CC-0x00E728D6: four longwords from entry+0x70 cleared:
     * 0x70, 0x74, 0x78, 0x7C.  0x00E728D8: clr.l (-0x60,A3) = entry+0x84.
     * (entry+0x80 and entry+0x88..0x8F are left alone.)
     */
    new_entry->sig_pending = 0;
    new_entry->sig_blocked_1 = 0;
    new_entry->sig_blocked_2 = 0;
    new_entry->sig_mask_3 = 0;
    new_entry->sig_mask_1 = 0;

    /* 0x00E728DC: andi.w #0xe3fb,(-0xba,A3) */
    new_entry->flags &= 0xE3FB;

    /* 0x00E728E2: clr.w (-0xcc,A3) -- entry+0x18 */
    new_entry->pad_18[0] = 0;

    /* 0x00E728E6: move.w (-0xcc,A2),(-0xca,A3) -- new+0x1A = current+0x18 */
    new_entry->pad_18[1] = current_entry->pad_18[0];

    /* 0x00E728EC: bclr.b #0x3,(-0xb9,A3) -- LOW byte bit 3 = 0x0008 */
    new_entry->flags &= (uint16_t)~PROC2_FLAG_DEBUG;

    /* 0x00E728F2-0x00E728FA: entry+0x4C (acct_uid) = the parent UID arg */
    new_entry->acct_uid.high = local_parent_uid.high;
    new_entry->acct_uid.low = local_parent_uid.low;

    /* 0x00E728FE: clr.w (-0x90,A3) -- entry+0x54 */
    new_entry->acct_info_len = 0;

    /*
     * 0x00E72902: move.l (-0x10,A6),(-0x8e,A3) -- entry+0x56 = clock high
     * 0x00E72908: move.w (-0xc,A6),(-0x8a,A3)  -- entry+0x5A = clock low
     */
    new_entry->creation_time_high = creation_time.high;
    new_entry->creation_time_low = creation_time.low;

    /* 0x00E7290E-0x00E72916: entry+0x60 (tty_uid) = current+0x60 */
    new_entry->tty_uid.high = current_entry->tty_uid.high;
    new_entry->tty_uid.low = current_entry->tty_uid.low;

    /* 0x00E7291A: tst.b D4b / bpl -- Domain boolean in the flags byte */
    if (local_flags < 0) {
        /* 0x00E7291E/0x00E72922: entry+0x1E = 0, entry+0x22 = 0 */
        new_entry->parent_pgroup_idx = 0;
        new_entry->next_child_sibling = 0;
    } else {
        /*
         * 0x00E72928: new+0x1E = current+0x1C (the caller's own index)
         * 0x00E7292E: new+0x22 = current+0x20 (old first child)
         * 0x00E72934: current+0x20 = new index
         */
        new_entry->parent_pgroup_idx = current_entry->self_index;
        new_entry->next_child_sibling = current_entry->first_child_idx;
        current_entry->first_child_idx = (uint16_t)new_idx;
    }

    /* 0x00E72938: tst.w (-0xbe,A2) -- current+0x26, the caller's debugger */
    if (current_entry->debugger_idx != 0) {
        /* 0x00E7293E-0x00E7294C: XPD_$INHERIT_PTRACE_OPTIONS(&current+0xCE) */
        if (XPD_$INHERIT_PTRACE_OPTIONS(
                (xpd_$ptrace_opts_t *)current_entry->ptrace_opts) < 0) {
            /*
             * 0x00E7294E-0x00E72960: DEBUG_SETUP_INTERNAL(new+0x1C,
             * current+0x26, FALSE) with a result slot.
             */
            DEBUG_SETUP_INTERNAL((int16_t)new_entry->self_index,
                                 (int16_t)current_entry->debugger_idx, 0);

            /* 0x00E72962-0x00E72970: 4+4+4+2 = 14 bytes current+0xCE -> new+0xCE */
            for (int i = 0; i < 14; i++) {
                new_entry->ptrace_opts[i] = current_entry->ptrace_opts[i];
            }
        }
    }

    /*
     * 0x00E72972-0x00E7299E: A2 = 0xE2B978 + (new+0x1C)*0x18; EC_$INIT on
     * (-0x18,A2) and (-0xC,A2) -- the slot's fork and cr_rec eventcounts.
     * (A2 no longer points at the caller's entry from here on.)
     */
    EC_$INIT(PROC_FORK_EC(new_entry->self_index));
    EC_$INIT(PROC_CR_REC_EC(new_entry->self_index));

    /* 0x00E729A0-0x00E729B4: *ec_ret = EC2_$REGISTER_EC1(fork_ec, &status) */
    *ec_ret = EC2_$REGISTER_EC1(PROC_FORK_EC(new_entry->self_index), &status);

    /* 0x00E729B6 */
    if ((status & 0xFFFF) != 0) {
        goto cleanup_asid;
    }

    /* 0x00E729BE-0x00E729CA */
    ML_$UNLOCK(PROC2_LOCK_ID);

    /* 0x00E729CC-0x00E729D6: *uid_ret = entry->uid */
    uid_ret->high = new_entry->uid.high;
    uid_ret->low = new_entry->uid.low;

    /* 0x00E729DA-0x00E729EA: ACL_$ALLOC_ASID(entry->level1_pid, &status) */
    ACL_$ALLOC_ASID((int16_t)new_entry->level1_pid, &status);

    /* 0x00E729EC-0x00E729FA: AUDIT_$INHERIT_AUDIT(&entry->level1_pid, &status) */
    AUDIT_$INHERIT_AUDIT((int16_t *)&new_entry->level1_pid, &status);

    /* 0x00E729FC-0x00E72A0A: NAME_$INIT_ASID(&entry->asid, &status) */
    NAME_$INIT_ASID((int16_t *)&new_entry->asid, &status);

    /* 0x00E72A0C: only this last status is tested */
    if ((status & 0xFFFF) != 0) {
        goto cleanup_asid;
    }

    /* 0x00E72A12: cmpi.w #0x1,PROC1_$CURRENT */
    if (PROC1_$CURRENT == 1) {
        max_priority = 0xE;                         /* 0x00E72A1C: A6-0x40 */
        min_priority = 3;                           /* 0x00E72A22: A6-0x42 */
    } else {
        /* 0x00E72A2A-0x00E72A40: query the caller's range (clr.w = GET) */
        PROC1_$SET_PRIORITY(PROC1_$CURRENT, PROC1_SET_PRIORITY_GET,
                            &min_priority, &max_priority);
    }

    /* 0x00E72A44-0x00E72A58: `st` = SET on the new process */
    PROC1_$SET_PRIORITY(new_entry->level1_pid, PROC1_SET_PRIORITY_SET,
                        &min_priority, &max_priority);

    /* 0x00E72A5C-0x00E72A64: PROC1_$SET_TYPE(entry->level1_pid, 2) */
    PROC1_$SET_TYPE(new_entry->level1_pid, 2);

    /* 0x00E72A6A: bra.w 0x00E72BBC -- the status store, no unlock */
    *status_ret = status;
    return;

cleanup_asid:
    /*
     * 0x00E72A6E-0x00E72A8C: every failure after the ASID was allocated
     * lands here, lock held or not.  PROC1_$TST_LOCK's boolean is tested
     * with tst.b/bmi: re-take the lock only when it is NOT held.
     */
    if (PROC1_$TST_LOCK(PROC2_LOCK_ID) >= 0) {
        ML_$LOCK(PROC2_LOCK_ID);
    }

    /* 0x00E72A8E: movea.l A3,A2 -- A2 now addresses the new entry */

    /*
     * 0x00E72A90-0x00E72AA8: if entry+0x1E (parent) != 0,
     * parent+0x20 (first child) = entry+0x22 (our next sibling).
     */
    if (new_entry->parent_pgroup_idx != 0) {
        proc2_info_t *parent = P2_INFO_ENTRY((int16_t)new_entry->parent_pgroup_idx);
        parent->first_child_idx = new_entry->next_child_sibling;
    }

    /* 0x00E72AAE-0x00E72AB6: btst.l #0x8 on the flags word */
    if ((new_entry->flags & PROC2_FLAG_BOUND) != 0) {
        /* 0x00E72AB8-0x00E72AC8: PROC1_$UNBIND(entry->level1_pid, &temp) */
        PROC1_$UNBIND(new_entry->level1_pid, &temp_status);
    } else if (stack_ptr != NULL) {
        /* 0x00E72ACC-0x00E72AD8: tst.l D6 / PROC1_$FREE_STACK(D6) */
        PROC1_$FREE_STACK(stack_ptr);
    }

    /*
     * 0x00E72ADA: cmpi.w #0x19,(-0x16,A6) / beq; 0x00E72AE2: bset.b #0x7,
     * (-0x18,A6).  A6-0x16 is the LOW word of the status, so the test is
     * "status code == 0x19", not "module == PROC2"; reproduced as found.
     */
    if ((status & 0xFFFF) != 0x19) {
        status |= (status_$t)0x80000000u;
    }

    /* 0x00E72AE8-0x00E72AF8: MST_$FREE_ASID(entry->asid, &temp) */
    MST_$FREE_ASID(new_entry->asid, &temp_status);

    /*
     * 0x00E72AFA-0x00E72B0E: PROC2_$UID[entry->asid] = proc2_system_uid
     * (A0 = 0xE7BE84, source (0x8,A0) = 0xE7BE8C, slot (0x10,A0,asid*8)).
     */
    PROC2_$UID[new_entry->asid].high = proc2_system_uid.high;
    PROC2_$UID[new_entry->asid].low = proc2_system_uid.low;

    /* 0x00E72B12-0x00E72B22: entry+0x9C (cleanup_flags) */
    if (new_entry->cleanup_flags != 0) {
        PROC2_$CLEANUP_HANDLERS_INTERNAL(new_entry);
    }

cleanup_entry:
    /* 0x00E72B24-0x00E72B36: PGROUP_CLEANUP_INTERNAL(entry, 2), result slot */
    PGROUP_CLEANUP_INTERNAL(new_entry, 2);

    /*
     * 0x00E72B38-0x00E72B5E: unlink from the allocated list.
     *   +0x14 == 0  -> P2_INFO_ALLOC_PTR = +0x12
     *   else        -> P2[+0x14]->next_index = +0x12
     */
    if (new_entry->pad_14 == 0) {
        P2_INFO_ALLOC_PTR = new_entry->next_index;
    } else {
        proc2_info_t *prev = P2_INFO_ENTRY((int16_t)new_entry->pad_14);
        prev->next_index = new_entry->next_index;
    }

    /*
     * 0x00E72B64-0x00E72B76: P2[+0x12]->pad_14 = +0x14, UNCONDITIONALLY
     * (no zero test; a zero next writes entry(0)+0x14).
     */
    P2_INFO_ENTRY(new_entry->next_index)->pad_14 = new_entry->pad_14;

    /* 0x00E72B7C-0x00E72B88: push back onto the free list */
    new_entry->next_index = P2_FREE_LIST_HEAD;
    P2_FREE_LIST_HEAD = (uint16_t)new_idx;

    /* 0x00E72B8C-0x00E72B96: entry+0x08 (parent_uid) = UID_$NIL */
    new_entry->parent_uid.high = UID_$NIL.high;
    new_entry->parent_uid.low = UID_$NIL.low;

    /* 0x00E72B9A: bclr.b #0x0,(-0xba,A2) -- HIGH byte bit 0 = 0x0100 */
    new_entry->flags &= (uint16_t)~PROC2_FLAG_BOUND;

    /* 0x00E72BA0-0x00E72BAC: entry+0x00 (uid) = proc2_system_uid (0xE7BE8C) */
    new_entry->uid.high = proc2_system_uid.high;
    new_entry->uid.low = proc2_system_uid.low;

    /* 0x00E72BB0-0x00E72BB6 */
    ML_$UNLOCK(PROC2_LOCK_ID);

    /* 0x00E72BBC-0x00E72BC0: *status_ret = A6-0x18 */
    *status_ret = status;
}
