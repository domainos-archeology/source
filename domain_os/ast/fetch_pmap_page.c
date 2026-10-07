/*
 * AST_$FETCH_PMAP_PAGE - Read one page of a remote object into a buffer
 *
 * Allocates a physical page, hands its data buffer to the NETBUF pool,
 * asks the diskless partner for the page with NETWORK_$READ_AHEAD, and on
 * success maps the page at the wired AST_$ZERO_BUFF address, copies its
 * 1KB (256 longwords) into `output_buf`, unmaps and frees it.  On failure
 * the data buffer is taken back from NETBUF and freed by page number.
 *
 * Parameters (frame at 0x00E041A8, `link.w A6,-0x94`):
 *   uid_info   (0x8,A6)   the 32-byte page request record READ_AHEAD takes
 *   output_buf (0xC,A6)   256 longwords out
 *   flags      (0x10,A6)  word, READ_AHEAD's page_size argument
 *   status     (0x12,A6)  BY VALUE: the slot itself is READ_AHEAD's status
 *                         (`pea (0x12,A6)` 0x00E041F4, `tst.l (0x12,A6)`
 *                         0x00E04222), so nothing reaches the caller
 * Locals: (-0x80,A6) 32-longword ppn array (only [0] is used),
 *         (-0x84,A6) NETBUF_$GET_DAT's address out,
 *         (-0x8C,A6) ONE clock_t cell whose address is passed three times.
 *
 * Original address: 0x00E041A8 (264 bytes), A5 = 0xE1DC80 (unused here).
 */

#include "ast/ast_internal.h"
#include "mmu/mmu.h"
#include "mmap/mmap.h"
#include "netbuf/netbuf.h"
#include "network/network.h"
#include "area/area.h"

void AST_$FETCH_PMAP_PAGE(void *uid_info, uint32_t *output_buf,
                          uint16_t flags, status_$t status)
{
    uint32_t ppn_array[32];     /* (-0x80,A6) */
    uint32_t buf_addr;          /* (-0x84,A6) */
    clock_t clock_scratch;      /* (-0x8C,A6) */
    uint32_t *src;              /* A1 */
    uint32_t *dst;              /* A3 */
    int16_t i;                  /* D0w */

    /* 0x00E041B6..0x00E041E0: allocate one page under the PMAP lock;
     * `move.l #0x10001` pushes count = 1 and min_count = 1 */
    ML_$LOCK(PMAP_LOCK_ID);
    ast_$allocate_pages(1, 1, ppn_array);
    ML_$UNLOCK(PMAP_LOCK_ID);

    /* 0x00E041E2..0x00E041F2: its data buffer address is ppn << 10 */
    NETBUF_$RTN_DAT(ppn_array[0] << 10);

    /*
     * 0x00E041F4..0x00E0421E: 36 bytes of arguments, no result slot.
     * `pea (-0x8c,A6)` then `move.l (SP),-(SP)` twice passes the same
     * cell as dtm, clock and acl_info; `clr.l -(SP)` is both the
     * no_read_ahead and flags bytes; count is 1.
     */
    (void)NETWORK_$READ_AHEAD(&AREA_$PARTNER, uid_info, ppn_array, flags, 1,
                              0, 0, &clock_scratch, &clock_scratch,
                              &clock_scratch, &status);

    /* 0x00E04222 */
    if (status == status_$ok) {
        /* 0x00E04228..0x00E0424C: map the page at 0xFF8C00 with flags 0x16 */
        ML_$LOCK(PMAP_LOCK_ID);
        MMU_$INSTALL(ppn_array[0], ARCH_PTR_TO_VA(AST_$ZERO_BUFF), 0, 0x16);

        /* 0x00E04250..0x00E0425E: move.w #0xff / dbf = 256 longwords */
        src = AST_$ZERO_BUFF;
        dst = output_buf;
        for (i = 0xFF; i >= 0; i--) {
            *dst++ = *src++;
        }

        /* 0x00E04262..0x00E04286 */
        MMU_$REMOVE(ppn_array[0]);
        MMAP_$FREE(ppn_array[0]);
        ML_$UNLOCK(PMAP_LOCK_ID);
    } else {
        /* 0x00E04288..0x00E042A0: take the buffer back, free it by ppn */
        NETBUF_$GET_DAT(&buf_addr);
        MMAP_$FREE(buf_addr >> 10);
    }
}
