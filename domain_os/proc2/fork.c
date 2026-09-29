/*
 * PROC2_$FORK - Fork the current process
 *
 * Creates a child process by forking the caller.  The child gets a copy of
 * the caller's address space, or shares it when the caller asked for a
 * vfork (*fork_flags == 0).
 *
 * Original address: 0x00E72BCE (1814 bytes)
 *
 * Addressing conventions used by the original, and reproduced in the
 * comments below:
 *
 *   A2 = 0xEA551C + new_idx    * 0xE4  == &new_entry    + 0xE4
 *   A3 = 0xEA551C + parent_idx * 0xE4  == &parent_entry + 0xE4
 *   A1 = 0xE7BE84                      == the PROC2 module data base
 *
 * so every (-d,A2)/(-d,A3) displacement is entry offset 0xE4 - d, and
 * (0x1E0,A1) / (0x1E2,A1) are P2_INFO_ALLOC_PTR (0xE7C064) and
 * P2_FREE_LIST_HEAD (0xE7C066).
 *
 * Flag bits: the original manipulates the 16-bit flags word at entry+0x2A
 * with BYTE operations.  (-0xBA,A2) is the HIGH byte, so bit n there is
 * bit n+8 of the word (0x08 -> 0x0800, 0x10 -> 0x1000, 0x80 -> 0x8000,
 * bit 0 -> 0x0100); (-0xB9,A2) is the LOW byte, so bit n there is bit n
 * of the word.  The same holds for cleanup_flags at entry+0x9C/0x9D.
 *
 * Stack frame locals (link.w A6,-0x68):
 *   -0x08 ec_list[1]      -0x10 wait_val        -0x14 va_adjusted
 *   -0x20 creation_time   -0x28 temp_status     -0x2C status
 *   -0x3A va_extra        -0x40 max_priority    -0x42 min_priority
 *   -0x44 va_modified     -0x46 va_active       -0x62 parent_asid*4
 *   -0x68 va_minus_one
 */

#include "proc2/proc2_internal.h"
#include "misc/misc.h"
#include "time/time.h"

/*
 * Globals used here (declared in subsystem headers):
 *   FIM_$USER_FIM_ADDR (0xE212A8), FIM_$QUIT_INH (0xE2248A)   - fim/fim.h
 *   PROC2_$EC / PROC_FORK_EC / PROC_CR_REC_EC (0xE2B978),
 *   PROC2_$UID table (0xE7BE94), proc2_system_uid (0xE7BE8C)  - proc2 headers
 */

#if defined(ARCH_M68K)
_Static_assert(sizeof(xpd_$ptrace_opts_t) == 14,
               "the 14 bytes copied at 0x00E73078 are one xpd_$ptrace_opts_t");
#endif

/* startup_context_t, STARTUP_CONTEXT_RESERVE and PROC1_SET_PRIORITY_SET/GET are
 * shared with PROC2_$CREATE and live in proc2_internal.h. */

void PROC2_$FORK(int32_t *entry_point, int32_t *user_data, int32_t *fork_flags,
                 uid_t *uid_ret, uint32_t reserved, uint16_t *upid_ret,
                 void **ec_ret, status_$t *status_ret)
{
    /*
     * `status` is the A6-0x2C local.  It is the value both exits assign to
     * *status_ret (0x00E732D6 `move.l (-0x2c,A6),(A2)`, reached from the
     * success path at 0x00E7313C/0x00E73142 as well as from the cleanup
     * tail), so it is deliberately left indeterminate here.
     *
     * ORIGINAL BUG (source-hh0j, confirmed 2026-09-06 by tracing every
     * reference to A6-0x2C in the 1814-byte function).  The complete list
     * of stores to that slot is:
     *
     *   0x00E72D02  pea (-0x2c,A6)  PROC1_$ALLOC_STACK    writes it
     *   0x00E72D46  pea (-0x2c,A6)  PROC1_$BIND           writes it
     *   0x00E72E60  pea (-0x2c,A6)  EC2_$REGISTER_EC1     writes it
     *   0x00E72EA6  pea (-0x2c,A6)  ACL_$ALLOC_ASID       writes it
     *   0x00E72EB6  pea (-0x2c,A6)  AUDIT_$INHERIT_AUDIT  writes it
     *   0x00E72F0E  pea (-0x2c,A6)  FILE_$FORK_LOCK       writes it
     *   0x00E72F4A  pea (-0x2c,A6)  MST_$FORK             writes it
     *   0x00E72F6E  pea (-0x2c,A6)  MST_$GET_VA_INFO      writes it
     *   0x00E72FA0  pea (-0x2c,A6)  MST_$GET_VA_INFO      writes it
     *   0x00E730FE  pea (-0x2c,A6)  PROC1_$RESUME         writes it
     *   0x00E73114  pea (-0x2c,A6)  CRASH_SYSTEM          reads it
     *   0x00E731B8  bset.b #0x7,(-0x2c,A6)                modifies it
     *   0x00E732D6  move.l (-0x2c,A6),(A2)                reads it
     *
     * The MST_$ALLOC_ASID failure branch is taken at 0x00E72CC6
     * (`tst.w (0x2,A1)` / `beq`), sets bit 31 of the CALLER's status at
     * 0x00E72CC8 (`bset.b #0x7,(A1)`), and then `bra.w 0x00E73240`
     * (0x00E72CCC) into the entry-teardown tail.  Every one of the stores
     * above sits at an address strictly between 0x00E72CCC and 0x00E73240,
     * and 0x00E73240..0x00E732D4 contains no reference to A6-0x2C at all
     * (only PGROUP_CLEANUP_INTERNAL, the free-list relinking, the UID
     * stores and ML_$UNLOCK).  Nothing before 0x00E72CCC touches it either:
     * the only frame stores in 0x00E72BCE..0x00E72CCC are (-0x20,A6) for
     * TIME_$CLOCK.  So on this one path the slot still holds whatever the
     * previous stack frame left there, and 0x00E732D6 hands that leftover
     * to the caller, discarding the real error MST_$ALLOC_ASID reported.
     *
     * That the author knew the difference is visible two branches earlier:
     * the "process table full" exit at 0x00E72C2E jumps to 0x00E732DA,
     * i.e. deliberately PAST the `*status_ret = status` store, so its own
     * write at 0x00E72C1C survives.  The ASID path does not.
     *
     * We cannot diff against a second Domain/OS build, so the behaviour is
     * reproduced as found rather than "corrected".
     */
    status_$t status;
    status_$t temp_status;
    clock_t creation_time;
    int16_t new_idx;
    int16_t parent_idx;
    uint16_t new_pid;
    void *stack_ptr = NULL;      /* D2 */
    uint16_t new_asid;
    proc2_info_t *new_entry;     /* A2 - 0xE4 */
    proc2_info_t *parent_entry;  /* A3 - 0xE4 */
    startup_context_t *ctx;      /* D6 */
    ec_$eventcount_t *fork_ec;
    void *registered_ec;         /* D5, after it stops holding parent_idx */
    uint16_t min_priority;       /* A6-0x42 */
    uint16_t max_priority;       /* A6-0x40 */
    uint32_t va_adjusted;        /* A6-0x14 */
    uint8_t  va_extra[14];       /* A6-0x3A: MST_$GET_VA_INFO param_5 buffer */
    int8_t   va_active;          /* A6-0x46 */
    int8_t   va_modified;        /* A6-0x44 */
    uint32_t va_minus_one;       /* A6-0x68 */
    uint16_t parent_asid_x4;     /* A6-0x62 */
    boolean file_locked;         /* D4 */
    int i;

    /* 0x00E72BDA: clr.b D4b */
    file_locked = false;

    /* 0x00E72BDC-0x00E72BF0 */
    parent_idx = (int16_t)P2_PID_TO_INDEX(PROC1_$CURRENT);

    /* 0x00E72BF8 */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E72C04 */
    TIME_$CLOCK(&creation_time);

    /* 0x00E72C12: the free-list head lives at 0xE7C066 */
    new_idx = (int16_t)P2_FREE_LIST_HEAD;
    if (new_idx == 0) {
        /* 0x00E72C18: written straight to the caller's status */
        *status_ret = status_$proc2_table_full;
        ML_$UNLOCK(PROC2_LOCK_ID);
        /* 0x00E72C2E jumps past the `*status_ret = status` epilogue. */
        return;
    }

    /* 0x00E72C32/0x00E72C42 */
    new_entry = P2_INFO_ENTRY(new_idx);
    parent_entry = P2_INFO_ENTRY(parent_idx);

    /* 0x00E72C52-0x00E72C5E: unlink from the free list, push onto the
     * allocated list */
    P2_FREE_LIST_HEAD = new_entry->next_index;
    new_entry->next_index = P2_INFO_ALLOC_PTR;
    P2_INFO_ALLOC_PTR = (uint16_t)new_idx;

    /*
     * 0x00E72C62-0x00E72C74: UNCONDITIONAL.  When next_index is 0 this
     * writes to P2_INFO_ENTRY(0) == 0xEA5438, the out-of-band entry the
     * table is biased against; the original has no null check here.
     */
    P2_INFO_ENTRY(new_entry->next_index)->pad_14 = (uint16_t)new_idx;

    /* 0x00E72C78 */
    new_entry->pad_14 = 0;

    /* 0x00E72C7C: entry+0x1E, the parent link -- not entry+0x24 */
    new_entry->parent_pgroup_idx = 0;

    /*
     * 0x00E72C80-0x00E72C9C.  All four are byte operations on the HIGH
     * byte of the flags word, so the masks are 0x0800 / 0x1000 / 0x8000.
     */
    new_entry->flags &= (uint16_t)~PROC2_FLAG_VFORK;
    if (*fork_flags == 0) {
        new_entry->flags |= PROC2_FLAG_VFORK;
    }
    new_entry->flags |= PROC2_FLAG_ORPHAN;          /* bset.b #0x4 -> 0x1000 */
    new_entry->flags &= (uint16_t)~PROC2_FLAG_INIT; /* bclr.b #0x7 -> 0x8000 */

    /* 0x00E72CA2: entry+0x6C from the parent ... */
    new_entry->cr_rec_2 = parent_entry->cr_rec_2;
    /* 0x00E72CAA: ... and entry+0x68 from the second argument */
    new_entry->cr_rec = (uint32_t)*user_data;

    /*
     * 0x00E72CAE: MST_$ALLOC_ASID is handed the CALLER's status pointer,
     * not the local.
     */
    new_asid = MST_$ALLOC_ASID(status_ret);
    new_entry->asid = new_asid;

    /* 0x00E72CC2: tst.w (0x2,A1) -- the LOW word of *status_ret */
    if ((*status_ret & 0xFFFF) != 0) {
        /* 0x00E72CC8: bset.b #0x7,(A1) == bit 31 */
        *status_ret |= 0x80000000;
        /*
         * 0x00E72CCC: bra.w 0x00E73240.  This value does NOT reach the
         * caller: the shared exit at 0x00E732D6 overwrites *status_ret
         * with the A6-0x2C local, which no instruction on this path has
         * written.  See the note on `status` above (source-hh0j) -- an
         * original bug, reproduced.
         */
        goto cleanup_entry;
    }

    /* 0x00E72CD0 */
    if ((new_entry->flags & PROC2_FLAG_VFORK) != 0) {
        /* vfork: keep the parent's ASID, park the new one as the alternate */
        new_entry->asid_alt = new_asid;                 /* 0x00E72CDA */
        new_entry->asid = parent_entry->asid;           /* 0x00E72CDE */
        /* 0x00E72CE4: entry+0xDC is stack_uid, not tty_uid */
        new_entry->stack_uid = parent_entry->stack_uid;
    } else {
        new_entry->asid_alt = 0;                        /* 0x00E72CF2 */
    }

    /* 0x00E72CFA */
    PROC2_$INIT_ENTRY_INTERNAL(new_entry);

    /* 0x00E72D0A */
    stack_ptr = PROC1_$ALLOC_STACK(0x1000, &status);

    /* 0x00E72D14: tst.w (-0x2a,A6) -- the LOW word of the local status */
    if ((status & 0xFFFF) != 0) {
        goto cleanup_locked;
    }

    /* 0x00E72D1C-0x00E72D42 */
    ctx = (startup_context_t *)((char *)stack_ptr
                                - (int32_t)(STARTUP_CONTEXT_RESERVE
                                            + FIM_$INITIAL_STACK_SIZE));
    ctx->user_data = *user_data;
    ctx->asid = new_entry->asid;
    ctx->entry_point = *entry_point;
    ctx->self_ptr = &ctx->user_data;

    /* 0x00E72D50: PROC2_$STARTUP's address is pushed as a longword value */
    new_pid = PROC1_$BIND((void *)PROC2_$STARTUP, ctx, stack_ptr, 0, &status);
    new_entry->level1_pid = new_pid;

    /* 0x00E72D64 */
    if ((status & 0xFFFF) != 0) {
        goto cleanup_locked;
    }

    /*
     * 0x00E72D6C: bset.b D4,(-0xba,A2).  D4 is still 0 here, so this sets
     * bit 0 of the HIGH byte, i.e. flags bit 8 == 0x0100.
     */
    new_entry->flags |= PROC2_FLAG_BOUND;

    /* 0x00E72D70 */
    PROC2_$PID_TO_INDEX[new_pid] = (uint16_t)new_idx;

    /*
     * 0x00E72D80-0x00E72D9E.  Six longwords: 0x70, 0x74, 0x78, 0x7C, 0x84
     * and 0x8C.  entry+0x80 (sig_mask_2) is deliberately NOT copied.
     */
    new_entry->sig_pending   = parent_entry->sig_pending;   /* 0x70 */
    new_entry->sig_blocked_1 = parent_entry->sig_blocked_1; /* 0x74 */
    new_entry->sig_blocked_2 = parent_entry->sig_blocked_2; /* 0x78 */
    new_entry->sig_mask_3    = parent_entry->sig_mask_3;    /* 0x7C */
    new_entry->sig_mask_1    = parent_entry->sig_mask_1;    /* 0x84 */
    new_entry->sig_mask_4    = parent_entry->sig_mask_4;    /* 0x8C */

    /*
     * 0x00E72DA4-0x00E72DBA: propagate flags bit 10 (0x0400).  The byte
     * operations are on the HIGH byte, so the mask is 0x0400.
     */
    new_entry->flags = (uint16_t)((new_entry->flags & ~0x0400)
                                  | (parent_entry->flags & 0x0400));

    /*
     * 0x00E72DBC-0x00E72DD0: propagate flags bit 2 (0x0004).  These are
     * byte operations on the LOW byte at entry+0x2B.
     */
    new_entry->flags = (uint16_t)((new_entry->flags & ~0x0004)
                                  | (parent_entry->flags & 0x0004));

    /*
     * 0x00E72DD2/0x00E72DD8: both words end up holding parent+0x18.  The
     * second move's source is (-0xCA,A2) -- the child's own 0x1A, just
     * written -- not the parent's.  Reproduced as-is.
     */
    new_entry->pad_18[1] = parent_entry->pad_18[0];
    new_entry->pad_18[0] = new_entry->pad_18[1];

    /* 0x00E72DDE/0x00E72DE4: push the child onto the parent's child list */
    new_entry->next_child_sibling = parent_entry->first_child_idx;
    parent_entry->first_child_idx = (uint16_t)new_idx;

    /* 0x00E72DE8: entry+0x1E records the parent's table index */
    new_entry->parent_pgroup_idx = (uint16_t)parent_idx;

    /* 0x00E72DEC/0x00E72DF2: the full 48-bit clock, high long then low word */
    new_entry->creation_time_high = creation_time.high;   /* entry+0x56 */
    new_entry->creation_time_low = creation_time.low;     /* entry+0x5A */

    /* 0x00E72DF8: bset.b #0x3 on the LOW byte -> flags bit 3 == 0x0008 */
    new_entry->flags |= PROC2_FLAG_DEBUG;

    /* 0x00E72DFE-0x00E72E0C: eight longwords, entry+0x2C (acct_info) */
    for (i = 0; i < 8; i++) {
        ((uint32_t *)new_entry->acct_info)[i] =
            ((uint32_t *)parent_entry->acct_info)[i];
    }

    /* 0x00E72E0E-0x00E72E2A */
    new_entry->acct_info_len = parent_entry->acct_info_len;  /* 0x54 */
    new_entry->acct_uid = parent_entry->acct_uid;            /* 0x4C */
    new_entry->tty_uid = parent_entry->tty_uid;              /* 0x60 */

    /*
     * 0x00E72E2C-0x00E72E3E: the eventcount pair is indexed by
     * entry+0x1C (self_index), NOT by the table index.  A4 lands on
     * PROC2_$EC + idx*0x18, and the two pea's are (-0x18,A4) and
     * (-0xC,A4), i.e. PROC2_$EC[idx-1].fork_ec / .cr_rec_ec.
     */
    fork_ec = PROC_FORK_EC(new_entry->self_index);
    EC_$INIT(fork_ec);

    /* 0x00E72E4E: the fork EC starts at -1 */
    fork_ec->value = -1;

    /* 0x00E72E54 */
    EC_$INIT(PROC_CR_REC_EC(new_entry->self_index));

    /* 0x00E72E68: the handle comes back in A0 */
    registered_ec = EC2_$REGISTER_EC1(fork_ec, &status);

    /* 0x00E72E72 */
    if ((status & 0xFFFF) != 0) {
        goto cleanup_locked;
    }

    /* 0x00E72E80 */
    ML_$UNLOCK(PROC2_LOCK_ID);

    /* 0x00E72E88-0x00E72EA2 */
    *uid_ret = new_entry->uid;
    *upid_ret = new_entry->upid;
    *ec_ret = registered_ec;

    /* 0x00E72EAE */
    ACL_$ALLOC_ASID((int16_t)new_entry->level1_pid, &status);

    /* 0x00E72EBE */
    AUDIT_$INHERIT_AUDIT((int16_t *)&new_entry->level1_pid, &status);

    /*
     * 0x00E72EC6-0x00E72EF6.  FIM_$USER_FIM_ADDR is a longword table
     * indexed by ASID (the *4 is the element size); the `move.l` sets Z
     * from the value moved, so the QUIT_INH clear runs only when the
     * inherited handler is non-NULL.
     */
    parent_asid_x4 = (uint16_t)(PROC1_$AS_ID << 2);
    {
        void *fim_addr = FIM_$DATA.user_fim_addr[parent_asid_x4 >> 2];
        FIM_$DATA.user_fim_addr[new_entry->asid] = fim_addr;
        if (fim_addr != NULL) {
            FIM_$WIRED_DATA.quit_inh[new_entry->asid] = 0;
        }
    }

    /* 0x00E72EF8 */
    if ((new_entry->flags & PROC2_FLAG_VFORK) != 0) {
        goto set_priority;
    }

    /* 0x00E72F04 */
    if (PROC1_$CURRENT != 1) {
        FILE_$FORK_LOCK(&new_entry->asid, &status);
        /* 0x00E72F1E: tst.l -- the WHOLE longword, unlike the tests above */
        if (status != status_$ok) {
            goto cleanup_locked;
        }
        /* 0x00E72F26: st D4b */
        file_locked = true;
    }

    /*
     * 0x00E72F28: tst.b (-0x47,A3) is the sign of the LOW byte of the
     * parent's cleanup_flags word, i.e. bit 7 == 0x0080.
     */
    if ((parent_entry->cleanup_flags & 0x0080) != 0) {
        /* 0x00E72F32: the address of PROC1_$AS_ID is pushed by value */
        if (MSG_$FORK(&PROC1_$AS_ID, &new_entry->asid) < 0) {
            /* 0x00E72F44: bset.b #0x7 on the LOW byte -> 0x0080 */
            new_entry->cleanup_flags |= 0x0080;
        }
    }

    /*
     * 0x00E72F4A-0x00E72F62: four parameters, 12 bytes of stack.
     *   0x00E72F4A  pea (-0x2c,A6)            -> &status
     *   0x00E72F4E  movea.l (0x10,A6),A0      -> fork_flags
     *   0x00E72F52  move.l (A0),-(SP)         -> *fork_flags, a LONGWORD
     *   0x00E72F54  move.w (-0x4a,A2),-(SP)   -> new_entry->level1_pid
     *   0x00E72F58  move.w (-0x4e,A2),-(SP)   -> new_entry->asid
     */
    MST_$FORK(new_entry->asid, new_entry->level1_pid,
              (uint32_t)*fork_flags, &status);

    /* 0x00E72F66 */
    if ((status & 0xFFFF) != 0) {
        goto cleanup_locked;
    }

    /*
     * 0x00E72F6E-0x00E72F94: eight arguments.  The four middle out-params
     * are real stack locals, not NULL.  The virtual address queried is
     * entry+0x6C (cr_rec_2) and the UID lands in entry+0x08 (parent_uid).
     */
    MST_$GET_VA_INFO(&new_entry->asid, &new_entry->cr_rec_2,
                     &new_entry->parent_uid, &va_adjusted, va_extra,
                     &va_active, &va_modified, &status);

    /* 0x00E72F98 */
    if ((status & 0xFFFF) != 0) {
        goto cleanup_locked;
    }

    /*
     * 0x00E72FA0-0x00E72FD0: the second query uses entry+0x68 (cr_rec)
     * minus one and stores the UID in entry+0xDC (stack_uid).
     */
    va_minus_one = new_entry->cr_rec - 1;
    MST_$GET_VA_INFO(&new_entry->asid, &va_minus_one,
                     &new_entry->stack_uid, &va_adjusted, va_extra,
                     &va_active, &va_modified, &status);

    /* 0x00E72FD4 */
    if ((status & 0xFFFF) != 0) {
        goto cleanup_locked;
    }

    /* 0x00E72FE6 */
    NAME_$FORK((int16_t *)&PROC1_$AS_ID, (int16_t *)&new_entry->asid);

    /*
     * 0x00E72FEE: btst #11 of the parent's cleanup_flags word (0x0800).
     * 0x00E72FF8: PCHIST_$UNIX_PROFIL_FORK takes TWO arguments, the
     * child's PROC1 pid and its ASID.
     */
    if ((parent_entry->cleanup_flags & 0x0800) != 0) {
        PCHIST_$UNIX_PROFIL_FORK((int16_t *)&new_entry->level1_pid,
                                 &new_entry->asid);
        /* 0x00E73008: bset.b #0x3 on the HIGH byte -> bit 11 == 0x0800 */
        new_entry->cleanup_flags |= 0x0800;
    }

set_priority:
    /* 0x00E7300E */
    if (PROC1_$CURRENT == 1) {
        max_priority = 14;
        min_priority = 3;
    } else {
        PROC1_$SET_PRIORITY(PROC1_$CURRENT, PROC1_SET_PRIORITY_GET,
                            &min_priority, &max_priority);
    }

    /* 0x00E73040: the parent's debugger index lives at entry+0x26 */
    if (parent_entry->debugger_idx != 0) {
        /* 0x00E73046: &parent_entry->ptrace_opts (entry+0xCE) */
        if (XPD_$INHERIT_PTRACE_OPTIONS(
                (xpd_$ptrace_opts_t *)parent_entry->ptrace_opts) < 0) {
            ML_$LOCK(PROC2_LOCK_ID);

            /* 0x00E73064: the first argument is the child's entry+0x1C */
            DEBUG_SETUP_INTERNAL((int16_t)new_entry->self_index,
                                 (int16_t)parent_entry->debugger_idx, 0);

            /* 0x00E73078-0x00E73086: 4 + 4 + 4 + 2 = 14 bytes */
            for (i = 0; i < 14; i++) {
                new_entry->ptrace_opts[i] = parent_entry->ptrace_opts[i];
            }

            ML_$UNLOCK(PROC2_LOCK_ID);
        }
    }

    /*
     * 0x00E73096-0x00E730A4.  The second argument is a Pascal boolean
     * pushed with `st -(SP)`, which lands in the HIGH byte of the word
     * slot; the C model of this parameter is 0xFF00.
     */
    PROC1_$SET_PRIORITY(new_entry->level1_pid, PROC1_SET_PRIORITY_SET,
                        &min_priority, &max_priority);

    /* 0x00E730AE-0x00E730E8 */
    {
        int32_t wait_val = EC_$READ(PROC_FORK_EC(new_entry->self_index)) + 1;
        ec_$eventcount_t *ec_list[1];

        ec_list[0] = PROC_FORK_EC(new_entry->self_index);

        /* 0x00E730EC */
        PROC1_$SET_TYPE(new_entry->level1_pid, 2);

        /* 0x00E73106 */
        PROC1_$RESUME(new_entry->level1_pid, &status);
        if ((status & 0xFFFF) != 0) {
            CRASH_SYSTEM(&status);
        }

        /* 0x00E7312E */
        EC_$WAITN(ec_list, &wait_val, 1);
    }

    /*
     * 0x00E73138: tst.b (-0xb9,A2) / bmi -- the sign of the LOW byte of
     * the flags word, i.e. bit 7 == 0x0080.  If the child did not set it,
     * the eventcount handle is retracted.
     */
    if ((new_entry->flags & 0x0080) == 0) {
        *ec_ret = NULL;
    }

    /* 0x00E732D2 */
    *status_ret = status;
    return;

cleanup_locked:
    /*
     * 0x00E73146: every failure after the lock was dropped comes here.
     * PROC1_$TST_LOCK returns a Domain boolean, so a negative value means
     * "already held".
     */
    if (PROC1_$TST_LOCK(PROC2_LOCK_ID) >= 0) {
        ML_$LOCK(PROC2_LOCK_ID);
    }

    /*
     * 0x00E73166-0x00E73182: unlink the child from the parent's child
     * list.  The parent is entry+0x1E and the field patched is the
     * parent's first_child_idx (0x20), taken from the child's
     * next_child_sibling (0x22).
     */
    if (new_entry->parent_pgroup_idx != 0) {
        P2_INFO_ENTRY(new_entry->parent_pgroup_idx)->first_child_idx =
            new_entry->next_child_sibling;
    }

    /* 0x00E73184 */
    if ((new_entry->flags & PROC2_FLAG_BOUND) != 0) {
        PROC1_$UNBIND(new_entry->level1_pid, &temp_status);
    } else if (stack_ptr != NULL) {
        PROC1_$FREE_STACK(stack_ptr);
    }

    /*
     * 0x00E731B0: cmpi.w #0x19,(-0x2a,A6) compares the LOW word of the
     * status with 0x19 -- not the module number in the high word.
     */
    if ((status & 0xFFFF) != 0x19) {
        /* 0x00E731B8: bset.b #0x7,(-0x2c,A6) == bit 31 */
        status |= 0x80000000;
    }

    /* 0x00E731BE */
    if (file_locked < 0) {
        FILE_$PRIV_UNLOCK_ALL(&new_entry->asid);
    }

    /* 0x00E731CE */
    if ((new_entry->flags & PROC2_FLAG_VFORK) != 0) {
        /* 0x00E731D8: release the alternate ASID and restore the
         * parent's own UID into the per-ASID table (parent+0x00). */
        MST_$FREE_ASID(new_entry->asid_alt, &temp_status);
        PROC2_$UID[new_entry->asid] = parent_entry->uid;
    } else {
        /* 0x00E73204 */
        MST_$FREE_ASID(new_entry->asid, &temp_status);
        PROC2_$UID[new_entry->asid] = proc2_system_uid;
    }

    /* 0x00E7322E */
    if (new_entry->cleanup_flags != 0) {
        PROC2_$CLEANUP_HANDLERS_INTERNAL(new_entry);
    }

cleanup_entry:
    /* 0x00E73240 */
    PGROUP_CLEANUP_INTERNAL(new_entry, 2);

    /* 0x00E73252-0x00E7327C: unlink from the allocated list */
    if (new_entry->pad_14 == 0) {
        P2_INFO_ALLOC_PTR = new_entry->next_index;
    } else {
        P2_INFO_ENTRY(new_entry->pad_14)->next_index = new_entry->next_index;
    }

    /*
     * 0x00E7327E-0x00E73294: UNCONDITIONAL, exactly like the insertion at
     * 0x00E72C62.
     */
    P2_INFO_ENTRY(new_entry->next_index)->pad_14 = new_entry->pad_14;

    /* 0x00E73296-0x00E732A4: push the entry back onto the free list */
    new_entry->next_index = P2_FREE_LIST_HEAD;
    P2_FREE_LIST_HEAD = (uint16_t)new_idx;

    /* 0x00E732A6 */
    new_entry->parent_uid = UID_$NIL;

    /* 0x00E732B4: bclr.b #0x0 on the HIGH byte -> flags bit 8 == 0x0100 */
    new_entry->flags &= (uint16_t)~PROC2_FLAG_BOUND;

    /* 0x00E732BA: 0xE7BE84 + 8 == proc2_system_uid (0xE7BE8C) */
    new_entry->uid = proc2_system_uid;

    /* 0x00E732CC */
    ML_$UNLOCK(PROC2_LOCK_ID);

    /* 0x00E732D2 */
    *status_ret = status;
}
