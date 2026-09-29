/*
 * NETBUF_$GET_DAT_COND and NETBUF_$GET_DAT - Get a data buffer page
 *
 * NETBUF_$GET_DAT_COND: non-blocking; pops the head of the data-page free
 *   list under the netbuf spin lock, or reports an empty pool.
 * NETBUF_$GET_DAT: blocking; retries the conditional get, and when the pool
 *   is empty either waits NETBUF_$DELAY_TIME (a type-7 network process) or
 *   wires a fresh page with WP_$CALLOC (any other process).
 *
 * Data pages are tracked by page number (PPN); the free list is threaded
 * through the MMAPE word at entry offset 0x06 (see netbuf_internal.h).  The
 * buffer address handed back is ppn << 10.
 *
 * Original addresses:
 *   NETBUF_$GET_DAT_COND: 0x00E0EF28 (124 bytes)
 *   NETBUF_$GET_DAT:      0x00E0EFA4 (162 bytes)
 * A5 = 0xE245A8 (the netbuf globals) in both.
 * Re-emitted from the disassembly 0x00E0EF28-0x00E0EFA2 and
 * 0x00E0EFA4-0x00E0F044.
 */

#include "netbuf/netbuf_internal.h"

/*
 * NETBUF_$GET_DAT_COND - Conditionally get a data buffer
 *
 * Parameters:
 *   addr_out - Output: the buffer address, ppn << 10, or 0 when the pool
 *              is empty (argument 1, (0x8,A6))
 *
 * Returns:
 *   Domain boolean in D0b: 0xFF (`st`) on success, 0 (`clr.b`) when empty.
 */
int8_t NETBUF_$GET_DAT_COND(uint32_t *addr_out)
{
    ml_$spin_token_t token;     /* (-0x6,A6) */
    uint32_t         ppn;       /* D2        */

    /* 0x00E0EF3A-0x00E0EF46: ML_$SPIN_LOCK(&spin_lock (0x308,A5)). */
    token = ML_$SPIN_LOCK(&NETBUF_$SPIN_LOCK);

    /* 0x00E0EF4A-0x00E0EF4E: `tst.l (0x320,A5) / beq` - pool empty? */
    if (NETBUF_$DAT_CNT == 0) {
        /* 0x00E0EF88-0x00E0EF98: unlock, false, *addr_out = 0. */
        ML_$SPIN_UNLOCK(&NETBUF_$SPIN_LOCK, token);
        *addr_out = 0;
        return 0;
    }

    /* 0x00E0EF50: ppn = dat_top (0x324,A5). */
    ppn = NETBUF_$DAT_TOP;

    /* 0x00E0EF54-0x00E0EF68: dat_top = zero-extended MMAPE word at
     * 0xEB4800 + ppn*16 - 0x1FFA (= MMAPE base + 6). */
    NETBUF_$DAT_TOP = (uint32_t)NETBUF_DAT_NEXT(ppn);

    /* 0x00E0EF6C: dat_cnt-- */
    NETBUF_$DAT_CNT--;

    /* 0x00E0EF70-0x00E0EF78: ML_$SPIN_UNLOCK(&spin_lock, token). */
    ML_$SPIN_UNLOCK(&NETBUF_$SPIN_LOCK, token);

    /* 0x00E0EF7E-0x00E0EF84: `lsl.l #8 / lsl.l #2` = ppn << 10; true. */
    *addr_out = ppn << 10;
    return (int8_t)-1;
}

/*
 * NETBUF_$GET_DAT - Get a data buffer (blocking)
 *
 * Parameters:
 *   addr_out - Output: the buffer address, ppn << 10 (argument 1, (0x8,A6))
 */
void NETBUF_$GET_DAT(uint32_t *addr_out)
{
    status_$t status;   /* (-0x8,A6) */
    uint32_t  ppn;      /* (-0x4,A6), then D2 */

    for (;;) {
        /* 0x00E0EFB8-0x00E0EFC2: NETBUF_$GET_DAT_COND(addr_out); a true
         * (negative) result is done. */
        if (NETBUF_$GET_DAT_COND(addr_out) < 0) {
            return;
        }

        /* 0x00E0EFC4-0x00E0EFDC: PROC1_$DATA.type[PROC1_$CURRENT_PCB->mypid]
         * (`cmpi.w #0x7,(-0x2,A0,D0w*0x1)` with A0 = 0xE2612C, D0w = pid*2,
         * i.e. the word at 0xE2612A + pid*2). */
        if (PROC1_$DATA.type[PROC1_$CURRENT_PCB->mypid] != NETBUF_NETWORK_PROC_TYPE) {
            break;
        }

        /* 0x00E0EFDE-0x00E0EFF0: TIME_$WAIT(&NETBUF_$DELAY_TYPE (the zero
         * word at 0x00E0EEB2, `pea (-0x136,PC)`), &delay_time (0x300,A5),
         * &status). */
        TIME_$WAIT(&NETBUF_$DELAY_TYPE, &NETBUF_$DELAY_TIME, &status);

        /* 0x00E0EFF4-0x00E0F004: a bad status crashes the system. */
        if (status != status_$ok) {
            CRASH_SYSTEM(&status);
        }

        /* 0x00E0F006-0x00E0F00A: dat_delays++ (0x314,A5); try again. */
        NETBUF_$DAT_DELAYS++;
    }

    /* 0x00E0F00C-0x00E0F01C: WP_$CALLOC(&ppn, &status); ppn into D2. */
    WP_$CALLOC(&ppn, &status);

    /* 0x00E0F020-0x00E0F02A */
    if (status != status_$ok) {
        CRASH_SYSTEM(&status);
    }

    /* 0x00E0F030: dat_allocs++ (0x30c,A5) */
    NETBUF_$DAT_ALLOCS++;

    /* 0x00E0F034-0x00E0F03A: *addr_out = ppn << 10 */
    *addr_out = ppn << 10;
}
