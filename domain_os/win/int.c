/*
 * win/int.c - WIN_$INT (0x00E19BFA, 238 bytes)
 *
 * The Winchester interrupt handler.  Idles the unit's mode register, then -
 * if a request chain is in progress - classifies the completion with
 * WIN_$CHECK_DISK_STATUS and either starts the transfer a seek was for,
 * or (after DMA_$CHECK) steps to the next request in the chain and seeks
 * for it.  The waiting WIN_$DO_IO is woken (unit eventcount advanced) when
 * the chain is exhausted, when anything failed, or when there was no chain
 * at all.  Returns Domain true (`st D0b`).
 *
 * Frame (link.w A6,-0x14; A5 A2 D4 D3 D2 saved), A5 = 0xE2B89C:
 *   D2          unit (the DCTE's controller number)
 *   D3          unit * 12
 *   D4          done: Domain boolean
 *   A2          the unit's register block
 *
 * 0x00E19C36-0x00E19C38: `move SR,D1w` / `move #0x2500,SR` - the priority
 * is raised to 5 and NEVER restored here (D1 is not used again); the
 * caller's rte does that.
 */

#include "win/win_internal.h"

int8_t WIN_$INT(dcte_t *dcte)
{
    uint8_t *win_data = WIN_DATA_BASE;
    uint16_t unit;                      /* D2 */
    int8_t done;                        /* D4 */
    volatile uint8_t *regs;             /* A2 */
    win_$request_t *cur;                /* A0 */
    disk_$volume_t *vol;                /* A0 */
    status_$t st;                       /* D0 */

    /* 0x00E19C08-0x00E19C26 */
    unit = dcte->cnum;
    regs = WIN_UNIT_REGS(unit);
    regs[WIN_REG_MODE] = 0;

    /* 0x00E19C2C-0x00E19C30: no chain in progress - just wake the waiter. */
    if (WIN_CUR_REQ_VA == 0) {
        goto advance;
    }

    /* 0x00E19C34-0x00E19C46 */
    done = 0;
    SET_SR(0x2500);
    *(status_$t *)(win_data + WIN_STATUS_OFFSET) = WIN_$CHECK_DISK_STATUS(unit);

    if ((int8_t)win_data[WIN_FLAG_OFFSET] < 0) {
        /* 0x00E19C4A-0x00E19C64: a seek completed.  Clear the flag (with
         * D4 = 0); if it went well, note the cylinder the request asked for
         * and start its transfer. */
        win_data[WIN_FLAG_OFFSET] = (uint8_t)done;
        if (*(status_$t *)(win_data + WIN_STATUS_OFFSET) != status_$ok) {
            goto finish;
        }
        cur = (win_$request_t *)ARCH_VA_TO_PTR(WIN_CUR_REQ_VA);
        *(uint16_t *)(win_data + WIN_CUR_CYL_OFFSET) = cur->cylinder;
        goto transfer;
    }

    /* 0x00E19C66-0x00E19C82: a transfer completed.  DMA_$CHECK's status
     * stands in for a zero completion status. */
    st = DMA_$CHECK(3);
    if (*(status_$t *)(win_data + WIN_STATUS_OFFSET) == status_$ok) {
        *(status_$t *)(win_data + WIN_STATUS_OFFSET) = st;
    }
    if (*(status_$t *)(win_data + WIN_STATUS_OFFSET) != status_$ok) {
        goto finish;
    }

    /* 0x00E19C84-0x00E19C94: on to the next request; none left = done. */
    cur = (win_$request_t *)ARCH_VA_TO_PTR(WIN_CUR_REQ_VA);
    WIN_CUR_REQ_VA = cur->next;
    if (WIN_CUR_REQ_VA == 0) {
        done = -1;
        goto finish;
    }

    /* 0x00E19C96-0x00E19CB2: SEEK(unit, vol->dev_unit, next, done (0)).
     * Zero = already on the cylinder: transfer now.  Positive = an error
     * to post.  Negative = the seek was started (the interrupt will come
     * back here with the flag set): nothing more to do. */
    vol = (disk_$volume_t *)ARCH_VA_TO_PTR(WIN_DEV_INFO_VA);
    st = SEEK(unit, vol->dev_unit, ARCH_VA_TO_PTR(WIN_CUR_REQ_VA),
              (uint8_t)done);
    if (st != status_$ok) {
        if (st > 0) {
            *(status_$t *)(win_data + WIN_STATUS_OFFSET) = st;
        }
        goto finish;
    }

transfer:
    /* 0x00E19CB4-0x00E19CC2: the result is posted unconditionally. */
    st = read_or_write_disk_record(unit);
    *(status_$t *)(win_data + WIN_STATUS_OFFSET) = st;

finish:
    /* 0x00E19CC6-0x00E19CD0 */
    if (*(status_$t *)(win_data + WIN_STATUS_OFFSET) != status_$ok) {
        done = -1;
    }
    if (done >= 0) {
        return -1;
    }

advance:
    /* 0x00E19CD2-0x00E19CDC */
    EC_$ADVANCE_WITHOUT_DISPATCH(WIN_UNIT_EC(unit));
    return -1;
}
