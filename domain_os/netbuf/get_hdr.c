/*
 * NETBUF_$GET_HDR_COND and NETBUF_$GET_HDR - Get a header buffer
 *
 * NETBUF_$GET_HDR_COND: non-blocking; pops the head of the 1KB header
 *   buffer free list under the netbuf spin lock, or reports an empty pool.
 * NETBUF_$GET_HDR: blocking; retries the conditional get, and when the pool
 *   is empty either waits NETBUF_$DELAY_TIME (a type-7 network process) or
 *   wires a fresh page with WP_$CALLOC, maps it with NETBUF_$GETVA and
 *   initialises its trailer (any other process).
 *
 * Header buffers are addressed by target virtual address; the free-list link
 * is the longword at 0x3E4 and the physical address the longword at 0x3FC
 * (see netbuf_internal.h).
 *
 * Original addresses:
 *   NETBUF_$GET_HDR_COND: 0x00E0ED6C (106 bytes)
 *   NETBUF_$GET_HDR:      0x00E0EDD6 (220 bytes)
 * A5 = 0xE245A8 (the netbuf globals) in both.
 * Re-emitted from the disassembly 0x00E0ED6C-0x00E0EDD4 and
 * 0x00E0EDD6-0x00E0EEB0.
 */

#include "netbuf/netbuf_internal.h"

/*
 * NETBUF_$GET_HDR_COND - Conditionally get a header buffer
 *
 * Parameters:
 *   phys_out - Output: the buffer's physical address (argument 1, (0x8,A6));
 *              NOT written when the pool is empty
 *   va_out   - Output: the buffer's virtual address, 0 when the pool is
 *              empty (argument 2, (0xC,A6))
 *
 * Returns:
 *   Domain boolean in D0b: 0xFF (`st`) on success, 0 (`clr.b`) when empty.
 */
int8_t NETBUF_$GET_HDR_COND(uint32_t *phys_out, uint32_t *va_out)
{
    ml_$spin_token_t token;     /* (-0x2,A6) */
    uint32_t         va;        /* A1 / A2   */

    /* 0x00E0ED7A-0x00E0ED86: ML_$SPIN_LOCK(&spin_lock (0x308,A5)). */
    token = ML_$SPIN_LOCK(&NETBUF_$SPIN_LOCK);

    /* 0x00E0ED8A-0x00E0ED98: *va_out = hdr_top (0x328,A5), re-read and
     * tested (`cmpa.w #0x0,A1`). */
    *va_out = NETBUF_$HDR_TOP;
    va = *va_out;
    if (va == 0) {
        /* 0x00E0EDBC-0x00E0EDCA: unlock, false. */
        ML_$SPIN_UNLOCK(&NETBUF_$SPIN_LOCK, token);
        return 0;
    }

    /* 0x00E0ED9A-0x00E0ED9C: hdr_top = link at (0x3e4,buffer). */
    NETBUF_$HDR_TOP = NETBUF_HDR_NEXT(va);

    /* 0x00E0EDA2-0x00E0EDAA: ML_$SPIN_UNLOCK(&spin_lock, token). */
    ML_$SPIN_UNLOCK(&NETBUF_$SPIN_LOCK, token);

    /* 0x00E0EDB0-0x00E0EDB8: *phys_out = (0x3fc,buffer); true. */
    *phys_out = NETBUF_HDR_PHYS(va);
    return (int8_t)-1;
}

/*
 * NETBUF_$GET_HDR - Get a header buffer (blocking)
 *
 * Parameters:
 *   phys_out - Output: the buffer's physical address (argument 1, (0x8,A6))
 *   va_out   - Output: the buffer's virtual address (argument 2, (0xC,A6))
 */
void NETBUF_$GET_HDR(uint32_t *phys_out, uint32_t *va_out)
{
    status_$t status;   /* (-0x8,A6) */
    uint32_t  ppn;      /* (-0x4,A6) */
    uint32_t  va;       /* A1        */
    int16_t   count;    /* D0w: dbf counter */
    uint32_t  off;      /* D1: 4, 8, 0xC, 0x10 */

    for (;;) {
        /* 0x00E0EDE8-0x00E0EDF6: NETBUF_$GET_HDR_COND(phys_out, va_out); a
         * true (negative) result is done. */
        if (NETBUF_$GET_HDR_COND(phys_out, va_out) < 0) {
            return;
        }

        /* 0x00E0EDFA-0x00E0EE12: PROC1_$TYPE[PROC1_$CURRENT_PCB->mypid]
         * (`cmpi.w #0x7,(-0x2,A0,D0w*0x1)` with A0 = 0xE2612C, D0w = pid*2,
         * i.e. the word at 0xE2612A + pid*2). */
        if (PROC1_$TYPE[PROC1_$CURRENT_PCB->mypid] != NETBUF_NETWORK_PROC_TYPE) {
            break;
        }

        /* 0x00E0EE14-0x00E0EE26: TIME_$WAIT(&NETBUF_$DELAY_TYPE (the zero
         * word at 0x00E0EEB2, `pea (0x94,PC)`), &delay_time (0x300,A5),
         * &status). */
        TIME_$WAIT(&NETBUF_$DELAY_TYPE, &NETBUF_$DELAY_TIME, &status);

        /* 0x00E0EE2A-0x00E0EE3A: a bad status crashes the system. */
        if (status != status_$ok) {
            CRASH_SYSTEM(&status);
        }

        /* 0x00E0EE3C-0x00E0EE40: hdr_delays++ (0x318,A5); try again. */
        NETBUF_$HDR_DELAYS++;
    }

    /* 0x00E0EE42-0x00E0EE50: WP_$CALLOC(&ppn, &status). */
    WP_$CALLOC(&ppn, &status);

    /* 0x00E0EE52-0x00E0EE5A: *phys_out = ppn << 10, stored BEFORE the
     * status is looked at. */
    *phys_out = ppn << 10;

    /* 0x00E0EE5C-0x00E0EE70: only with a good status is the page mapped:
     * NETBUF_$GETVA(ppn << 10, va_out, &status). */
    if (status == status_$ok) {
        NETBUF_$GETVA(ppn << 10, va_out, &status);
    }

    /* 0x00E0EE74-0x00E0EE84: either failure lands here. */
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    /* 0x00E0EE86: hdr_allocs++ (0x310,A5) */
    NETBUF_$HDR_ALLOCS++;

    /* 0x00E0EE8A-0x00E0EEA0: `moveq #3,D0 / moveq #4,D1`, then per pass
     * `movea.l (0xc,A6),A0 / movea.l (A0),A1 / lea (0,A1,D1),A3 /
     * clr.l (0x3e8,A3) / addq.l #4,D1 / dbf` - four longwords at
     * 0x3EC, 0x3F0, 0x3F4, 0x3F8, with *va_out re-read every pass. */
    count = 3;
    off   = 4;
    do {
        va = *va_out;
        NETBUF_HDR_FIELD(va, NETBUF_HDR_DATA_OFF + off) = 0;
        off += 4;
        count--;
    } while (count >= 0);

    /* 0x00E0EEA4: (0x3fc,A1) = *phys_out, A1 still the last re-read VA. */
    NETBUF_HDR_PHYS(va) = *phys_out;
}
