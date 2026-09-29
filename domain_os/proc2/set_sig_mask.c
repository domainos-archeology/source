/*
 * PROC2_$SET_SIG_MASK - Update the calling process's signal state
 *
 * Re-emitted from the image (0x00E3F7DE..0x00E3FA0E, 560 bytes).
 *
 * Frame (link.w A6,-0x4C; A5 = 0xE7BE84):
 *   (0x8,A6)  delta ptr      -> D3 = *ptr (word added to entry+0x18)
 *   (0xC,A6)  clear_mask     8 longwords copied to A6-0x40..-0x21
 *   (0x10,A6) set_mask       8 longwords copied to A6-0x20..-0x1
 *   (0x14,A6) result         -> A3 (two longwords out)
 *   A6-0x46   caller's table index (looked up BEFORE the lock)
 *   A6-0x44   previous-sibling index for DETACH_FROM_PARENT
 *
 * Entry fields through A2 = entry + 0xE4: (-0x74) +0x70 sig_pending,
 * (-0x70) +0x74 sig_blocked_1, (-0x6C) +0x78 sig_blocked_2, (-0x68) +0x7C
 * sig_mask_3, (-0x64) +0x80 sig_mask_2, (-0x63) its byte 1 (bits 23..16),
 * (-0x60) +0x84 sig_mask_1, (-0x58) +0x8C sig_mask_4, (-0xBA/-0xB9) the
 * flags word's high/low byte, (-0xCC) +0x18, (-0xCA) +0x1A, (-0xC4) +0x20
 * first child, (-0xC2) +0x22 next sibling, (-0xC8) +0x1C self_index.
 *
 * Only reference: the SVC table entry at 0x00E7BA3E.
 *
 * Original address: 0x00e3f7de
 */

#include "proc2/proc2_internal.h"

void PROC2_$SET_SIG_MASK(int16_t *priority_delta, uint32_t *clear_mask,
                         uint32_t *set_mask, uint32_t *result)
{
    uint32_t clr[8];             /* A6-0x40 */
    uint32_t set[8];             /* A6-0x20 */
    int16_t index;               /* A6-0x46 */
    int16_t delta;               /* D3 */
    int16_t prev;                /* A6-0x44 */
    int16_t child_idx;           /* D4 / D2 */
    int16_t next;
    proc2_info_t *entry;         /* A2 */
    proc2_info_t *child;         /* A0 */
    int i;

    /* 0x00E3F7EC-0x00E3F80E: two dbf loops of 8 longwords */
    for (i = 0; i < 8; i++) {
        clr[i] = clear_mask[i];
    }
    for (i = 0; i < 8; i++) {
        set[i] = set_mask[i];
    }

    /* 0x00E3F812-0x00E3F82A: delta and the index, before locking */
    delta = *priority_delta;
    index = (int16_t)PROC2_$DATA.pid_to_index[PROC1_$CURRENT];

    /* 0x00E3F830-0x00E3F83C */
    ML_$LOCK(PROC2_LOCK_ID);

    /* 0x00E3F83E-0x00E3F84C */
    entry = P2_INFO_ENTRY(index);

    /* 0x00E3F850-0x00E3F8B0: field := (field & ~clr[n]) | set[n] */
    entry->sig_blocked_1 = (entry->sig_blocked_1 & ~clr[0]) | set[0];   /* +0x74 */
    entry->sig_blocked_2 = (entry->sig_blocked_2 & ~clr[1]) | set[1];   /* +0x78 */
    entry->sig_pending   = (entry->sig_pending   & ~clr[2]) | set[2];   /* +0x70 */
    entry->sig_mask_1    = (entry->sig_mask_1    & ~clr[3]) | set[3];   /* +0x84 */
    entry->sig_mask_2    =  entry->sig_mask_2    & ~clr[4];             /* +0x80: no set */
    entry->sig_mask_3    = (entry->sig_mask_3    & ~clr[5]) | set[5];   /* +0x7C */

    /*
     * 0x00E3F8B4-0x00E3F8C6: byte 0 of clr[7] / set[7] (A6-0x24 / A6-0x4),
     * tested with tst.b/bpl, drive bit 2 of the flags HIGH byte (0x0400).
     */
    if ((clr[7] & 0x80000000u) != 0) {
        entry->flags &= (uint16_t)~0x0400;                   /* bclr.b #2,(-0xba) */
    }
    if ((set[7] & 0x80000000u) != 0) {
        entry->flags |= 0x0400;                              /* bset.b #2,(-0xba) */
    }

    /* 0x00E3F8CC-0x00E3F8D2: a non-zero clr[6] installs set[6] at +0x8C */
    if (clr[6] != 0) {
        entry->sig_mask_4 = set[6];
    }

    /* 0x00E3F8D8-0x00E3F8EA: byte 1 of clr[7] / set[7] (A6-0x23 / A6-0x3)
     * drive bit 2 of the flags LOW byte (0x0004) */
    if ((clr[7] & 0x00800000u) != 0) {
        entry->flags &= (uint16_t)~0x0004;                   /* bclr.b #2,(-0xb9) */
    }
    if ((set[7] & 0x00800000u) != 0) {
        entry->flags |= 0x0004;                              /* bset.b #2,(-0xb9) */
    }

    /* 0x00E3F8F0: tst.w D3w / beq */
    if (delta != 0) {
        /* 0x00E3F8F4: D3 += entry+0x18 */
        delta = (int16_t)(delta + (int16_t)entry->pad_18[0]);
        /* 0x00E3F8F8-0x00E3F900: only a decrease to a positive value walks
         * the children (bge / ble, signed) */
        if (delta < (int16_t)entry->pad_18[0] && delta > 0) {
            child_idx = (int16_t)entry->first_child_idx;     /* 0x00E3F902 */
            prev = 0;                                        /* 0x00E3F906 */
            while (child_idx != 0) {
                child = P2_INFO_ENTRY(child_idx);            /* 0x00E3F90C-0x00E3F918 */
                /* 0x00E3F91C: cmp.w (-0xca,A0),D3w / bge skip */
                if (delta < (int16_t)child->pad_18[1]) {
                    next = (int16_t)child->next_child_sibling;   /* 0x00E3F922 */
                    PROC2_$DETACH_FROM_PARENT(child_idx, prev);  /* 0x00E3F92C */
                    child_idx = next;
                } else {
                    prev = child_idx;                        /* 0x00E3F936 */
                    child_idx = (int16_t)child->next_child_sibling;
                }
            }
        }
        /* 0x00E3F942: entry+0x18 = D3 */
        entry->pad_18[0] = (uint16_t)delta;
    }

    /* 0x00E3F946-0x00E3F962: bit 17 of +0x80 clear, and set in +0x70 or +0x74 */
    if ((entry->sig_mask_2 & 0x00020000u) == 0 &&
        ((entry->sig_pending & 0x00020000u) != 0 ||
         (entry->sig_blocked_1 & 0x00020000u) != 0)) {
        child_idx = (int16_t)entry->first_child_idx;         /* 0x00E3F964 */
        prev = 0;
        while (child_idx != 0) {
            child = P2_INFO_ENTRY(child_idx);                /* 0x00E3F970-0x00E3F97C */
            /* 0x00E3F980-0x00E3F992: a zombie whose +0x1A equals our +0x18 */
            if ((child->flags & PROC2_FLAG_ZOMBIE) != 0 &&
                child->pad_18[1] == entry->pad_18[0]) {
                /* 0x00E3F994: bit 17 of +0x74 */
                if ((entry->sig_blocked_1 & 0x00020000u) != 0) {
                    next = (int16_t)child->next_child_sibling;       /* 0x00E3F99E */
                    PROC2_$DETACH_FROM_PARENT(child_idx, prev);      /* 0x00E3F9A8 */
                    child_idx = next;
                    continue;
                }
                /* 0x00E3F9B2: bset.b #1,(-0x63,A2) -> +0x80 bit 17; stop */
                entry->sig_mask_2 |= 0x00020000u;
                break;
            }
            prev = child_idx;                                /* 0x00E3F9BA */
            child_idx = (int16_t)child->next_child_sibling;
        }
    }

    /* 0x00E3F9C6-0x00E3F9DC: (+0x80 & ~+0x78) != 0 -> deliver(self_index) */
    if ((entry->sig_mask_2 & ~entry->sig_blocked_2) != 0) {
        PROC2_$DELIVER_PENDING_INTERNAL((int16_t)entry->self_index);
    }

    /* 0x00E3F9DE-0x00E3F9EA */
    ML_$UNLOCK(PROC2_LOCK_ID);

    /* 0x00E3F9EC-0x00E3FA02: result[0] = +0x78; result[1] = flags bit 10 */
    result[0] = entry->sig_blocked_2;
    result[1] = ((entry->flags & 0x0400) != 0) ? 1u : 0u;
}
