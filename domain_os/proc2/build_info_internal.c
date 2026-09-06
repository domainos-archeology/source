/*
 * PROC2_$BUILD_INFO_INTERNAL - Build combined process info structure
 *
 * Builds a combined PROC1+PROC2 info record (0xE4 bytes) from a PROC1
 * PID and/or a PROC2 table index.  Called by PROC2_$GET_INFO and
 * PROC2_$INFO.
 *
 * Parameters:
 *   proc2_index - Index in PROC2 table (0 for no PROC2 info)
 *   proc1_pid   - PROC1 process ID (0 for no PROC1 info)
 *   info        - Buffer to receive combined info (0xE4 bytes)
 *   status_ret  - Pointer to receive status
 *
 * Original address: 0x00e4094c (852 bytes)
 *
 * The PROC2 entry is addressed through A4 = 0xEA551C + index * 0xE4
 * (0x00E40A84-0x00E40A8E), which is entry_base + 0xE4, so every negative
 * displacement (-d,A4) in this function is entry offset 0xE4 - d.  The
 * same convention is used by PROC2_$SET_ACCT_INFO, PROC2_$GET_TTY_DATA
 * and PROC2_$SET_TTY.
 */

#include "proc2/proc2_internal.h"
#include "misc/string.h"

/* Status codes */
#define status_$proc2_not_level_2_process           0x00190002
#define status_$proc2_request_is_for_current_process 0x00190004

/*
 * Combined process info record (0xE4 bytes).
 *
 * Every offset below is the (d,A2) displacement used by the original;
 * A2 is the `info` argument (0x00E4095C).
 */
typedef struct proc_info_combined_t {
    uid_t       parent_uid;     /* 0x00: entry+0x08          (0x00E40AB0) */
    uint32_t    cr_rec;         /* 0x08: entry+0x68          (0x00E40AB6) */
    proc1_$info_t proc1_info;   /* 0x0C: PROC1_$GET_INFO     (0x00E4096E) */
    uid_t       sid[4];         /* 0x24: ACL_$GET_PID_SID    (0x00E409A0) */
    uint32_t    pad_44;         /* 0x44: cleared             (0x00E40A56) */
    uid_t       proc_uid_2;     /* 0x48: pgroup UID          (0x00E40AC2) */
    uint8_t     server_flag;    /* 0x50: flags bit 9         (0x00E40BAE) */
    uint8_t     pad_51;         /* 0x51 */
    uint16_t    min_priority;   /* 0x52: PROC1_$SET_PRIORITY (0x00E4098A) */
    uint16_t    max_priority;   /* 0x54: PROC1_$SET_PRIORITY (0x00E40986) */
    uint32_t    cpu_time[4];    /* 0x56: PROC_STATS_BASE     (0x00E409C8) */
    uid_t       pgroup_uid;     /* 0x66: copy of proc_uid_2  (0x00E40AD4) */
    uint16_t    pgroup_flags;   /* 0x6E: copy of pgroup_info (0x00E40AEA) */
    uint16_t    upid;           /* 0x70: entry+0x16          (0x00E40BB2) */
    uint16_t    parent_upid;    /* 0x72: P2[entry+0x1E].upid (0x00E40BD0) */
    uint16_t    pgroup_info;    /* 0x74: pgroup upgid        (0x00E40ADC) */
    uint16_t    session_upid;   /* 0x76: P2[entry+0x26].upid (0x00E40BF6) */
    uint16_t    asid;           /* 0x78: entry+0x96          (0x00E40ABC) */
    uid_t       acct_uid;       /* 0x7A: entry+0x4C          (0x00E40C04) */
    uint16_t    acct_info_len;  /* 0x82: entry+0x54, part of the same copy */
    char        acct_info[32];  /* 0x84: entry+0x2C          (0x00E40C16) */
    uint16_t    name_len;       /* 0xA4: entry+0xBE          (0x00E40AFC) */
    char        name[32];       /* 0xA6: entry+0x9E          (0x00E40B24) */
    uid_t       tty_uid;        /* 0xC6: entry+0x60          (0x00E40AF0) */
    uint16_t    pad_ce;         /* 0xCE */
    uint32_t    usage_d0;       /* 0xD0: PROC1_$GET_ANY_CPU_USAGE arg 2 */
    uint16_t    usage_d4;       /* 0xD4: (cleared alone at 0x00E40A6E) */
    uint16_t    pad_d6;         /* 0xD6: never cleared by the null path */
    uint32_t    usage_d8;       /* 0xD8: PROC1_$GET_ANY_CPU_USAGE arg 4 */
    uint32_t    usage_dc;       /* 0xDC: PROC1_$GET_ANY_CPU_USAGE arg 3 */
    uint32_t    const_e0;       /* 0xE0: constant 0x411C     (0x00E409F6) */
} proc_info_combined_t;

#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(proc_info_combined_t, cr_rec) == 0x08, "cr_rec@0x08");
_Static_assert(__builtin_offsetof(proc_info_combined_t, proc1_info) == 0x0C, "proc1_info@0x0C");
_Static_assert(__builtin_offsetof(proc_info_combined_t, sid) == 0x24, "sid@0x24");
_Static_assert(__builtin_offsetof(proc_info_combined_t, pad_44) == 0x44, "pad_44@0x44");
_Static_assert(__builtin_offsetof(proc_info_combined_t, proc_uid_2) == 0x48, "proc_uid_2@0x48");
_Static_assert(__builtin_offsetof(proc_info_combined_t, server_flag) == 0x50, "server_flag@0x50");
_Static_assert(__builtin_offsetof(proc_info_combined_t, min_priority) == 0x52, "min_priority@0x52");
_Static_assert(__builtin_offsetof(proc_info_combined_t, max_priority) == 0x54, "max_priority@0x54");
_Static_assert(__builtin_offsetof(proc_info_combined_t, cpu_time) == 0x56, "cpu_time@0x56");
_Static_assert(__builtin_offsetof(proc_info_combined_t, pgroup_uid) == 0x66, "pgroup_uid@0x66");
_Static_assert(__builtin_offsetof(proc_info_combined_t, pgroup_flags) == 0x6E, "pgroup_flags@0x6E");
_Static_assert(__builtin_offsetof(proc_info_combined_t, upid) == 0x70, "upid@0x70");
_Static_assert(__builtin_offsetof(proc_info_combined_t, parent_upid) == 0x72, "parent_upid@0x72");
_Static_assert(__builtin_offsetof(proc_info_combined_t, pgroup_info) == 0x74, "pgroup_info@0x74");
_Static_assert(__builtin_offsetof(proc_info_combined_t, session_upid) == 0x76, "session_upid@0x76");
_Static_assert(__builtin_offsetof(proc_info_combined_t, asid) == 0x78, "asid@0x78");
_Static_assert(__builtin_offsetof(proc_info_combined_t, acct_uid) == 0x7A, "acct_uid@0x7A");
_Static_assert(__builtin_offsetof(proc_info_combined_t, acct_info_len) == 0x82, "acct_info_len@0x82");
_Static_assert(__builtin_offsetof(proc_info_combined_t, acct_info) == 0x84, "acct_info@0x84");
_Static_assert(__builtin_offsetof(proc_info_combined_t, name_len) == 0xA4, "name_len@0xA4");
_Static_assert(__builtin_offsetof(proc_info_combined_t, name) == 0xA6, "name@0xA6");
_Static_assert(__builtin_offsetof(proc_info_combined_t, tty_uid) == 0xC6, "tty_uid@0xC6");
_Static_assert(__builtin_offsetof(proc_info_combined_t, usage_d0) == 0xD0, "usage_d0@0xD0");
_Static_assert(__builtin_offsetof(proc_info_combined_t, usage_d8) == 0xD8, "usage_d8@0xD8");
_Static_assert(__builtin_offsetof(proc_info_combined_t, usage_dc) == 0xDC, "usage_dc@0xDC");
_Static_assert(__builtin_offsetof(proc_info_combined_t, const_e0) == 0xE0, "const_e0@0xE0");
_Static_assert(sizeof(proc_info_combined_t) == 0xE4, "proc_info_combined_t must be 0xE4 bytes");
#endif

/*
 * Nested helpers (0x00E421DE and 0x00E421AA).  Both take (entry, out) by
 * reference and turn the entry's process-group table index into either a
 * synthetic UID or the raw UPGID.  Renamed in Ghidra to
 * PROC2_$ENTRY_PGROUP_UID / PROC2_$ENTRY_PGROUP_UPGID.
 */
static void proc2_$entry_pgroup_uid(proc2_info_t *entry, uid_t *uid_ret);
static void proc2_$entry_pgroup_upgid(proc2_info_t *entry, uint16_t *info_ret);

void PROC2_$BUILD_INFO_INTERNAL(int16_t proc2_index, int16_t proc1_pid,
                                 void *info, status_$t *status_ret)
{
    proc_info_combined_t *out = (proc_info_combined_t *)info;
    proc2_info_t *entry;
    proc2_info_t *other;
    uint16_t flags;
    int16_t other_idx;
    int i;

    /* 0x00E40964 */
    *status_ret = status_$ok;

    /* 0x00E40966: tst.w D3w / beq */
    if (proc1_pid == 0) {
        /* --- no PROC1 half (0x00E40A0E) --- */

        /* 0x00E40A0E: six longwords -> out+0x0C..0x23 */
        memset(&out->proc1_info, 0, sizeof(out->proc1_info));

        /* 0x00E40A1A: clr.l (0x52,A2) clears BOTH priority words */
        out->min_priority = 0;
        out->max_priority = 0;

        /* 0x00E40A1E-0x00E40A52 */
        for (i = 0; i < 4; i++) {
            out->sid[i] = UID_$NIL;
        }

        /* 0x00E40A56 */
        out->pad_44 = 0;

        /* 0x00E40A5A-0x00E40A66 */
        out->cpu_time[0] = 0;
        out->cpu_time[1] = 0;
        out->cpu_time[2] = 0;
        out->cpu_time[3] = 0;

        /* 0x00E40A6A/0x00E40A6E: note 0xD6 is deliberately left alone */
        out->usage_d0 = 0;
        out->usage_d4 = 0;

        /* 0x00E40A72: three longwords -> out+0xD8..0xE3 */
        out->usage_d8 = 0;
        out->usage_dc = 0;
        out->const_e0 = 0;
    } else {
        /*
         * --- PROC1 half (0x00E4096C) ---
         *
         * Both by-reference PROC1 calls below pass (0xa,A6): the address of
         * the proc1_pid PARAMETER slot itself, so they share one variable.
         * The by-value uses go through D3, which is never reloaded.
         */
        int16_t proc1_pid_slot = proc1_pid;

        /* 0x00E40976 */
        PROC1_$GET_INFO(&proc1_pid_slot, &out->proc1_info, status_ret);

        /* 0x00E40980: tst.w (0x2,A3) -- the LOW word of the status */
        if ((*status_ret & 0xFFFF) != 0) {
            /* 0x00E409B6: bset.b #0x7,(A3) -- bit 31 of the status */
            *status_ret |= 0x80000000;
            return;
        }

        /*
         * 0x00E40986-0x00E40992: pushes are (0x54,A2), (0x52,A2), 0, pid,
         * so min_priority receives out+0x52 and max_priority out+0x54.
         */
        PROC1_$SET_PRIORITY((uint16_t)proc1_pid, 0,
                            &out->min_priority, &out->max_priority);

        /* 0x00E409A6 */
        ACL_$GET_PID_SID(proc1_pid, out->sid, status_ret);

        /* 0x00E409B0 */
        if ((*status_ret & 0xFFFF) != 0) {
            *status_ret |= 0x80000000;
            return;
        }

        /*
         * 0x00E409C0-0x00E409DA: A0 = 0xE25D20, index = pid*16, and the
         * first read is (-0x10,A0,D4w) = 0xE25D10 + pid*16, i.e. the
         * four longwords of PROC_STATS_BASE[pid].
         */
        out->cpu_time[0] = PROC_STATS_BASE[proc1_pid * 4 + 0];
        out->cpu_time[1] = PROC_STATS_BASE[proc1_pid * 4 + 1];
        out->cpu_time[2] = PROC_STATS_BASE[proc1_pid * 4 + 2];
        out->cpu_time[3] = PROC_STATS_BASE[proc1_pid * 4 + 3];

        /* 0x00E409DC-0x00E409EC: arguments pushed 0xD8, 0xDC, 0xD0, &pid */
        PROC1_$GET_ANY_CPU_USAGE((uint16_t *)&proc1_pid_slot, &out->usage_d0,
                                 &out->usage_dc, &out->usage_d8);

        /* 0x00E409F6 */
        out->const_e0 = 0x411C;

        /* 0x00E409FE */
        if (proc1_pid == (int16_t)PROC1_$CURRENT) {
            *status_ret = status_$proc2_request_is_for_current_process;
        }
    }

    /* 0x00E40A7C: tst.w D2w / beq -> done */
    if (proc2_index == 0) {
        return;
    }

    /* 0x00E40A82 */
    entry = P2_INFO_ENTRY(proc2_index);

    /* 0x00E40A92: move.w (-0xba,A4),D4w -- entry+0x2A */
    flags = entry->flags;

    /* 0x00E40A96/0x00E40A9C: btst #8 / btst #13 */
    if ((flags & 0x0100) == 0 && (flags & PROC2_FLAG_ZOMBIE) == 0) {
        /* --- neither valid nor zombie (0x00E40C28) --- */
        *status_ret = status_$proc2_not_level_2_process;

        out->parent_uid = UID_$NIL;     /* 0x00E40C34 */
        out->pgroup_uid = UID_$NIL;     /* 0x00E40C40 */
        out->proc_uid_2 = UID_$NIL;     /* 0x00E40C4E */
        out->cr_rec = 0;                /* 0x00E40C56 */
        out->pgroup_flags = 0;          /* 0x00E40C5A */
        out->server_flag = 0;           /* 0x00E40C5E */
        out->upid = 0;                  /* 0x00E40C62 */
        /* 0x00E40C66: clr.l (0x72,A2) covers parent_upid AND pgroup_info */
        out->parent_upid = 0;
        out->pgroup_info = 0;
        out->session_upid = 0;          /* 0x00E40C6A */
        out->acct_uid = UID_$NIL;       /* 0x00E40C74 */
        out->acct_info_len = 0;         /* 0x00E40C7C */
        out->name_len = 0;              /* 0x00E40C80 */
        out->asid = 0;                  /* 0x00E40C84 */
        out->tty_uid = UID_$NIL;        /* 0x00E40C8E */

        /* This path does NOT fall into the common block at 0x00E40BA4. */
        return;
    }

    /* 0x00E40AA4: btst #8 again */
    if ((flags & 0x0100) != 0) {
        /* --- valid process (0x00E40AAC) --- */

        out->parent_uid = entry->parent_uid;     /* 0x00E40AB0: entry+0x08 */
        out->cr_rec = entry->cr_rec;             /* 0x00E40AB6: entry+0x68 */
        out->asid = entry->asid;                 /* 0x00E40ABC: entry+0x96 */

        /* 0x00E40AC2: the helper writes out+0x48 ... */
        proc2_$entry_pgroup_uid(entry, &out->proc_uid_2);
        /* 0x00E40AD0: ... and it is then copied to out+0x66 */
        out->pgroup_uid = out->proc_uid_2;

        /* 0x00E40ADC: the helper writes out+0x74 ... */
        proc2_$entry_pgroup_upgid(entry, &out->pgroup_info);
        /* 0x00E40AEA: ... and out+0x6E takes a copy */
        out->pgroup_flags = out->pgroup_info;

        /*
         * 0x00E40AF0: lea (-0x84,A4) = entry+0x60 -- the TTY UID (see
         * PROC2_$GET_TTY_DATA 0x00E41BEC) -- lands in the info record at
         * 0xC6, not the accounting UID.
         */
        out->tty_uid = entry->tty_uid;

        /* 0x00E40AFC: entry+0xBE */
        if (entry->name_len == 0x21) {          /* '!' = no name */
            out->name_len = 0;
        } else if (entry->name_len == 0x22) {   /* '"' = special */
            out->name_len = 0xFFFF;
        } else {
            out->name_len = entry->name_len;
        }

        /* 0x00E40B24: 32 bytes from entry+0x9E */
        memcpy(out->name, entry->name, 32);
    } else {
        /* --- zombie (0x00E40B38) --- */

        out->parent_uid = UID_$NIL;     /* 0x00E40B3E */
        out->cr_rec = 0;                /* 0x00E40B44 */
        out->asid = 0;                  /* 0x00E40B48 */
        out->pgroup_uid = UID_$NIL;     /* 0x00E40B52 */
        out->proc_uid_2 = UID_$NIL;     /* 0x00E40B60 */
        out->pgroup_flags = 0;          /* 0x00E40B68 */
        out->pgroup_info = 0;           /* 0x00E40B6C */
        out->tty_uid = UID_$NIL;        /* 0x00E40B76 */

        /*
         * 0x00E40B7E/0x00E40B84: the zombie's exit data (entry+0xA4 and
         * entry+0xA8) is stuffed into the PROC1 info area at out+0x1C and
         * out+0x20, which is proc1_$info_t.cpu_total.
         */
        for (i = 0; i < 6; i++) {
            out->proc1_info.cpu_total[i] = entry->zombie_usage[i];
        }

        /* 0x00E40B8A: five longwords entry+0xA4 -> out+0xD0..0xE3 */
        out->usage_d0 = PROC2_ZOMBIE_USAGE(entry, 0);
        out->usage_d4 = (uint16_t)(PROC2_ZOMBIE_USAGE(entry, 1) >> 16);
        out->pad_d6   = (uint16_t)(PROC2_ZOMBIE_USAGE(entry, 1));
        out->usage_d8 = PROC2_ZOMBIE_USAGE(entry, 2);
        out->usage_dc = PROC2_ZOMBIE_USAGE(entry, 3);
        out->const_e0 = PROC2_ZOMBIE_USAGE(entry, 4);

        /* 0x00E40B9A */
        out->name_len = 0;

        /* 0x00E40B9E */
        *status_ret = status_$proc2_zombie;
    }

    /* --- common tail for the valid and zombie paths (0x00E40BA4) --- */

    /* 0x00E40BA4: btst #9 of the flags word / sne -> 0xFF or 0x00 */
    flags = entry->flags;
    out->server_flag = (flags & 0x0200) ? 0xFF : 0x00;

    /* 0x00E40BB2: entry+0x16 */
    out->upid = entry->upid;

    /* 0x00E40BB8: entry+0x1E is the parent's table index */
    other_idx = (int16_t)entry->parent_pgroup_idx;
    if (other_idx == 0) {
        out->parent_upid = 1;               /* 0x00E40BD8 */
    } else {
        other = P2_INFO_ENTRY(other_idx);
        out->parent_upid = other->upid;     /* 0x00E40BD0: other+0x16 */
    }

    /* 0x00E40BDE: entry+0x26 */
    other_idx = (int16_t)entry->debugger_idx;
    if (other_idx == 0) {
        out->session_upid = 0;              /* 0x00E40BFE */
    } else {
        other = P2_INFO_ENTRY(other_idx);
        out->session_upid = other->upid;    /* 0x00E40BF6: other+0x16 */
    }

    /*
     * 0x00E40C04: ten bytes from entry+0x4C, i.e. the accounting UID
     * written by PROC2_$SET_ACCT_INFO (0x00E41B2C) followed by the
     * accounting-info length at entry+0x54 (0x00E41B24).
     */
    out->acct_uid = entry->acct_uid;
    out->acct_info_len = entry->acct_info_len;

    /* 0x00E40C16: 32 bytes from entry+0x2C -- the accounting info string */
    memcpy(out->acct_info, entry->acct_info, 32);
}

/*
 * proc2_$entry_pgroup_uid - 0x00E421DE
 *
 *   00e421ec  tst.w (0x10,A0)              ; entry->pgroup_table_idx
 *   00e421f0  bne.b 0x00e42200
 *   00e421f2  movea.l #0xe1737c,A2         ; UID_$NIL
 *   00e421f8  move.l (A2)+,(A1)
 *   00e421fa  move.l (A2)+,(0x4,A1)
 *   00e421fe  bra.b 0x00e4221c
 *   00e42200  clr.w (A1)                   ; uid.high high half = 0
 *   00e42202  move.w (0x10,A0),D0w
 *   00e42206  movea.l #0xea551c,A2
 *   00e4220c  lsl.w #0x3,D0w               ; idx * 8
 *   00e4220e  lea (0x0,A2,D0w*0x1),A2
 *   00e42212  move.w (0x3f34,A2),(0x2,A1)  ; PGROUP_TABLE[idx].upgid
 *   00e42218  clr.l (0x4,A1)               ; uid.low = 0
 *
 * The result is a synthetic UID whose high longword is the UPGID and
 * whose low longword is zero.
 */
static void proc2_$entry_pgroup_uid(proc2_info_t *entry, uid_t *uid_ret)
{
    uint16_t idx = entry->pgroup_table_idx;

    if (idx == 0) {
        *uid_ret = UID_$NIL;
    } else {
        uid_ret->high = PGROUP_ENTRY(idx)->upgid;
        uid_ret->low = 0;
    }
}

/*
 * proc2_$entry_pgroup_upgid - 0x00E421AA
 *
 *   00e421b8  tst.w (0x10,A0)
 *   00e421bc  bne.b 0x00e421c2
 *   00e421be  clr.w (A1)
 *   00e421c2  move.w (0x10,A0),D0w
 *   00e421cc  lsl.w #0x3,D0w
 *   00e421d2  move.w (0x3f34,A2),(A1)      ; PGROUP_TABLE[idx].upgid
 */
static void proc2_$entry_pgroup_upgid(proc2_info_t *entry, uint16_t *info_ret)
{
    uint16_t idx = entry->pgroup_table_idx;

    if (idx == 0) {
        *info_ret = 0;
    } else {
        *info_ret = PGROUP_ENTRY(idx)->upgid;
    }
}
