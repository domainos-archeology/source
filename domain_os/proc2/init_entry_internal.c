/*
 * PROC2_$INIT_ENTRY_INTERNAL - Initialise a freshly allocated process entry
 *
 * Called by PROC2_$CREATE (0x00E727FA) and PROC2_$FORK (0x00E72CFA) once the
 * new entry has been taken off the free list and its ASID assigned.  It:
 *
 *   1. generates the process UID and publishes it in PROC2_UID[asid];
 *   2. resets the FIM per-PID quit state;
 *   3. allocates a UPID that collides with no live process's UPID, no live
 *      process's session id and no live process's process group;
 *   4. clears the child / debug list links;
 *   5. inherits the session id and the process group from the creator (or
 *      makes the entry a group leader if its flags say so);
 *   6. propagates the creator's flag bit 0x0200, clears 0x6070, marks the
 *      entry unnamed and resets the XPD ptrace options.
 *
 * NOTE (bead source-e8c8): this routine never touches entry+0x1C
 * (proc2_info_t.self_index).  That word is slot identity -- PROC2_$INIT
 * stamps each slot with its own 1-based table index at 0x00E304CA and gives
 * entry 1 the value 1 at 0x00E30502 -- so it must survive entry reuse.
 *
 * Parameters:
 *   entry - the process table entry to initialise
 *
 * Original address: 0x00e732e4
 */

#include "proc2/proc2_internal.h"

void PROC2_$INIT_ENTRY_INTERNAL(proc2_info_t *entry)
{
    proc2_info_t *creator;
    uint16_t upid;
    int16_t pgroup_idx;
    int16_t scan;
    int8_t taken;
    status_$t status;
    xpd_$ptrace_opts_t opts;
    int i;

    /* 0x00E732F0: UID_$GEN(&entry->uid) */
    UID_$GEN(&entry->uid);

    /*
     * 0x00E732FA-0x00E7330C: publish the new UID in the per-ASID table.
     * D0 = entry->asid << 3 indexes 0xE7BE84 + 0x10 = PROC2_UID (0xE7BE94),
     * and the two longwords are copied high then low.
     */
    PROC2_UID[entry->asid].high = entry->uid.high;
    PROC2_UID[entry->asid].low = entry->uid.low;

    /* 0x00E73310: the argument is &entry->asid (entry+0x96), not the value */
    FIM_$INIT_PID((int16_t *)&entry->asid);

    /* 0x00E7331C */
    entry->cleanup_flags = 0;

    /*
     * 0x00E73330-0x00E7338E: pick a UPID nobody is using.
     *
     * The allocator is a rolling counter: the candidate is the counter's
     * current value and the counter then advances (wrapping from 30000 back
     * to 0x41).  The candidate is rejected if any entry on the allocated
     * list already uses it as a UPID (+0x16) or as a session id (+0x5C), or
     * if it names an existing process group whose index some entry carries
     * (+0x10).
     */
    for (;;) {
        /* 0x00E73330 */
        upid = PROC2_$NEXT_UPID;

        /* 0x00E73334-0x00E73344 */
        if (PROC2_$NEXT_UPID == P2_UPID_WRAP_AT) {
            PROC2_$NEXT_UPID = P2_UPID_WRAP_TO;
        } else {
            PROC2_$NEXT_UPID = PROC2_$NEXT_UPID + 1;
        }

        /* 0x00E7334A: clr.b D3b */
        taken = 0;

        /* 0x00E7334E */
        pgroup_idx = PGROUP_FIND_BY_UPGID(upid);

        /* 0x00E73356-0x00E7335A: walk the allocated list */
        scan = (int16_t)P2_INFO_ALLOC_PTR;
        while (scan != 0) {
            proc2_info_t *e = P2_INFO_ENTRY(scan);

            if (e->upid == upid                          /* 0x00E7336C */
                || e->session_id == upid                 /* 0x00E73372 */
                || (pgroup_idx != 0                      /* 0x00E73378 */
                    && e->pgroup_table_idx == (uint16_t)pgroup_idx)) {
                /* 0x00E73382: st D3b, then 0x00E73384 retries */
                taken = -1;
                break;
            }

            /* 0x00E73386 */
            scan = (int16_t)e->next_index;
        }

        if (taken < 0) {
            continue;
        }

        /*
         * 0x00E7338C  tst.b D3b
         * 0x00E7338E  bmi.b 0x00E73330
         * Dead as written: every path that reaches here left D3 clear (it is
         * cleared at 0x00E7334A and the only st is followed by a branch back
         * to the top).  Preserved for fidelity.
         */
        if (taken < 0) {
            continue;
        }
        break;
    }

    /* 0x00E73390 */
    entry->upid = upid;

    /* 0x00E73394: clr.l -- both halves of the debug list head */
    entry->first_debug_target_idx = 0;
    entry->debugger_idx = 0;

    /* 0x00E73398 */
    entry->next_debug_target_idx = 0;

    /* 0x00E7339C: clr.l -- both halves of the child list head */
    entry->first_child_idx = 0;
    entry->next_child_sibling = 0;

    /*
     * 0x00E733A0-0x00E733BA: the creator is the current process,
     * P2_INFO_ENTRY(P2_PID_TO_INDEX[PROC1_$CURRENT]).
     */
    creator = P2_INFO_ENTRY(P2_PID_TO_INDEX(PROC1_$CURRENT));

    /* 0x00E733BE */
    entry->pgroup_table_idx = 0;

    /* 0x00E733C2 */
    entry->session_id = creator->session_id;

    /*
     * 0x00E733C8  tst.w (0x2a,A3) / bpl -- a signed word test of the flags,
     * i.e. bit 15 (PROC2_FLAG_INIT).
     */
    if ((int16_t)entry->flags < 0) {
        /* 0x00E733CE-0x00E733DA: become a group leader under its own UPID.
         * The status this writes is never read by this routine. */
        PGROUP_SET_INTERNAL(entry, entry->upid, &status);
        (void)status;
    } else {
        /* 0x00E733E6-0x00E733EC: args are (creator, entry) */
        PROC2_$PGROUP_INHERIT_INTERNAL(creator, entry);
    }

    /*
     * 0x00E733F4-0x00E73408: copy the creator's flag bit 0x0200 into the new
     * entry.  The andi.b / or.b operate on the HIGH byte of the flags word at
     * entry+0x2A, so the bit written is bit 1 of that byte == 0x0200 of the
     * word; spelled out here so it holds on a little-endian host.
     */
    entry->flags &= (uint16_t)~0x0200u;
    if ((creator->flags & 0x0200u) != 0) {
        entry->flags |= 0x0200u;
    }

    /* 0x00E7340C: 0x21 marks the entry as having no name */
    entry->name_len = 0x21;

    /* 0x00E73412: andi.w #-0x6071 -- clears 0x6070, keeps everything else */
    entry->flags &= 0x9F8Fu;

    /* 0x00E73418 */
    entry->pad_94 = 0;

    /* 0x00E7341C: clr.l at entry+0x80 */
    entry->sig_mask_2 = 0;

    /*
     * 0x00E73420-0x00E73448: the 14-byte XPD ptrace option record is copied
     * to a stack local, reset, and copied back.
     */
    for (i = 0; i < (int)sizeof(entry->ptrace_opts); i++) {
        ((uint8_t *)&opts)[i] = entry->ptrace_opts[i];
    }

    /* 0x00E73434 */
    XPD_$RESET_PTRACE_OPTS(&opts);

    for (i = 0; i < (int)sizeof(entry->ptrace_opts); i++) {
        entry->ptrace_opts[i] = ((uint8_t *)&opts)[i];
    }
}
