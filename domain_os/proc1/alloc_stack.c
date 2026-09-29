/*
 * PROC1_$ALLOC_STACK - Allocate a process stack
 * Original address: 0x00e1501a (256 bytes)
 *
 * Re-emitted from the disassembly.  A5 = 0x00E254E8 (the PROC1_ data block,
 * `lea (0xe254e8).l,A5' at 0x00E15022); the three cells it touches are
 * STACK_LOW_WATER (0xC40,A5), STACK_HIGH_WATER (0xC3C,A5) and
 * STACK_FREE_LIST (0xC38,A5) - see the module map in proc1/proc1.h.
 *
 * Frame: (0x8,A6) size (word), (0xA,A6) status (pointer).
 * Locals: (-0x8,A6) result, (-0x10,A6) the page WP_$CALLOC returns,
 * (-0x1C,A6) the zero-extended size used by the large-stack subtraction.
 * Registers: D2 = rounded size, D3 = "small stack" boolean (scs), D4 =
 * scratch / the new high-water value, A2 = status.
 *
 * 0x00E1501A  link.w A6,-0x1c / movem.l D2-D4/A2/A5,-(SP) / lea A5
 * 0x00E15028  D2 = size; A2 = status; *status = 0
 * 0x00E15032  ML_$LOCK(0xB)                    (result slot + word, addq #4)
 * 0x00E15040  D2 = (D2 + 0x3FF) & ~0x3FF
 * 0x00E15048  D3 = (D2 < 0x1000)               `scs D3b'
 * 0x00E1504E  tst.b D3 / bpl 0x00E1506C        small stack when D3 < 0
 * 0x00E15052  D4 = LOW + D2 + 0x400; result = D4
 * 0x00E15064  cmp.l HIGH,D4 / bls 0x00E150AE   D4 <= HIGH: go map pages
 * 0x00E1506A  bra 0x00E150C4                   else: no stack space
 * 0x00E1506C  tst.l FREE_LIST / beq 0x00E1508C
 * 0x00E15072  cmpi.w #0x1000,D2 / bne 0x00E1508C
 * 0x00E15078  result = FREE_LIST + 4; FREE_LIST = *FREE_LIST; bra 0x00E15100
 * 0x00E1508C  (-0x1C,A6) = D2 (zero-extended)
 * 0x00E15094  D4 = HIGH - size - 0x400
 * 0x00E150A2  cmp.l LOW,D4 / bcs 0x00E150C4    D4 < LOW: no stack space
 * 0x00E150A8  result = HIGH
 * 0x00E150AE  tst.w D2 / beq 0x00E150F0        page loop
 * 0x00E150B2  WP_$CALLOC(&page, status)        (addq #8)
 * 0x00E150C0  tst.l (A2) / beq 0x00E150CC
 * 0x00E150C4  *status = 0x000A0009; bra 0x00E15100
 * 0x00E150CC  MMU_$INSTALL(page, result - D2, 0x16)   (lea (0xc,SP),SP)
 * 0x00E150EA  D2 -= 0x400; bne 0x00E150B2
 * 0x00E150F0  tst.b D3 / bpl 0x00E150FC
 * 0x00E150F4  LOW = result; bra 0x00E15100
 * 0x00E150FC  HIGH = D4
 * 0x00E15100  ML_$UNLOCK(0xB)                  (result slot + word, no addq:
 *                                               the unlk discards them)
 * 0x00E1510C  D0 = result / movem.l / unlk / rts
 *
 * Quirks reproduced:
 *   - WP_$CALLOC's own failure status is overwritten with
 *     status_$no_stack_space_is_available at 0x00E150C4;
 *   - the result cell (-0x8,A6) is only written at 0x00E15060, 0x00E1507E
 *     and 0x00E150A8, so the large-stack "no space" exit taken at
 *     0x00E150A6 returns whatever the frame held (the other failure paths
 *     have already written it);
 *   - the pages already mapped by a partially completed loop are not
 *     unmapped when WP_$CALLOC fails.
 *
 * Parameters:
 *   size       - requested stack size in bytes (rounded up to 1KB)
 *   status_ret - status return
 *
 * Returns:
 *   the top of the allocated stack (the value PROC1_$FREE_STACK takes back)
 */

#include "proc1/proc1_internal.h"
#include "ml/ml.h"
#include "mmu/mmu.h"
#include "wp/wp.h"

/* 0x00E15040 / 0x00E15048 / 0x00E1505A */
#define PROC1_STACK_PAGE      0x400u    /* one page: `addi.l #0x400' */
#define PROC1_STACK_ROUND     0x3FFu    /* `addi.w #0x3ff' / `andi.w #-0x400' */
#define PROC1_STACK_LARGE     0x1000u   /* `cmpi.w #0x1000,D2w' */

/* 0x00E150CC: `pea (0x16).w' - the MMU_$INSTALL flags for a stack page */
#define PROC1_STACK_MMU_FLAGS 0x16u

void *PROC1_$ALLOC_STACK(uint16_t size, status_$t *status_ret)
{
    uint16_t rounded;           /* D2 */
    int8_t   small_stack;       /* D3 */
    uint32_t d4;                /* D4 */
    /*
     * (-0x8,A6): a virtual address.  The image never initialises the cell,
     * so the exit at 0x00E150A6 returns stale frame contents; a defined
     * zero stands in for that here.
     */
    uint32_t result = 0;
    uint32_t page;              /* (-0x10,A6) */
    uint32_t *link;

    /* 0x00E15030 */
    *status_ret = status_$ok;

    /* 0x00E15032: ML_$LOCK(PROC1_CREATE_LOCK_ID) */
    ML_$LOCK(PROC1_CREATE_LOCK_ID);

    /* 0x00E15040..0x00E15044 */
    rounded = (uint16_t)((size + PROC1_STACK_ROUND) & ~PROC1_STACK_ROUND);

    /* 0x00E15048: scs -> 0xFF when rounded < 0x1000 (unsigned) */
    small_stack = (rounded < PROC1_STACK_LARGE) ? -1 : 0;

    /* 0x00E1504E: tst.b D3 / bpl */
    if (small_stack < 0) {
        /* 0x00E15052..0x00E15060: grow the low region upward */
        d4 = PROC1_$DATA.stack_low_water + (uint32_t)rounded + PROC1_STACK_PAGE;
        result = d4;

        /* 0x00E15064: cmp.l (0xc3c,A5),D4 / bls -> map pages */
        if (d4 <= PROC1_$DATA.stack_high_water) {
            goto map_pages;
        }
        /* 0x00E1506A */
        goto no_space;
    }

    /* 0x00E1506C..0x00E15076: a free 4KB stack can be reused as-is */
    if (PROC1_$DATA.stack_free_list != 0 && rounded == PROC1_STACK_LARGE) {
        /* 0x00E15078..0x00E1508A */
        d4 = PROC1_$DATA.stack_free_list + 4;
        result = d4;
        link = (uint32_t *)ARCH_VA_TO_PTR(PROC1_$DATA.stack_free_list);
        PROC1_$DATA.stack_free_list = *link;
        goto unlock;
    }

    /* 0x00E1508C..0x00E150A0: grow the high region downward */
    d4 = PROC1_$DATA.stack_high_water - (uint32_t)rounded - PROC1_STACK_PAGE;

    /* 0x00E150A2: cmp.l (0xc40,A5),D4 / bcs -> no space */
    if (d4 < PROC1_$DATA.stack_low_water) {
        goto no_space;
    }

    /* 0x00E150A8 */
    result = PROC1_$DATA.stack_high_water;

map_pages:
    /* 0x00E150AE: tst.w D2 / beq 0x00E150F0 */
    while (rounded != 0) {
        /* 0x00E150B2: WP_$CALLOC(&page, status_ret) */
        WP_$CALLOC(&page, status_ret);

        /* 0x00E150C0: tst.l (A2) */
        if (*status_ret != status_$ok) {
            goto no_space;
        }

        /* 0x00E150CC..0x00E150E6: MMU_$INSTALL(page, result - rounded, 0x16) */
        MMU_$INSTALL(page, result - (uint32_t)rounded, PROC1_STACK_MMU_FLAGS);

        /* 0x00E150EA: subi.w #0x400,D2w / bne */
        rounded = (uint16_t)(rounded - PROC1_STACK_PAGE);
    }

    /* 0x00E150F0: tst.b D3 / bpl */
    if (small_stack < 0) {
        /* 0x00E150F4 */
        PROC1_$DATA.stack_low_water = result;
    } else {
        /* 0x00E150FC */
        PROC1_$DATA.stack_high_water = d4;
    }
    goto unlock;

no_space:
    /* 0x00E150C4 */
    *status_ret = status_$no_stack_space_is_available;

unlock:
    /* 0x00E15100: ML_$UNLOCK(PROC1_CREATE_LOCK_ID) */
    ML_$UNLOCK(PROC1_CREATE_LOCK_ID);

    /* 0x00E1510C: D0 = (-0x8,A6) */
    return ARCH_VA_TO_PTR(result);
}
