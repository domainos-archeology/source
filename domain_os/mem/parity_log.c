/*
 * MEM_$PARITY_LOG - Log a memory parity error
 *
 * Original address: 0x00E0ADB0
 * Size: 182 bytes (the whole `I E0ADB0 MEM_ size = B8` code segment)
 *
 * Called from 0x00E0B0E0 (PARITY_$CHECK's logging path).
 */

#include "mem/mem_internal.h"

/*
 * MEM_$PARITY_LOG
 *
 * MEM_$MEM_REC keeps two tallies:
 *
 * 1. MEM_$BOARD_ERRORS[1..2] - a total per memory board, board 1 for
 *    physical addresses below 3MB and board 2 for the rest.
 * 2. MEM_$PAGE_ERRORS[0..3]  - a count for each of four remembered page
 *    regions, identified by bits 21..16 of the physical address.
 *
 * The routine scans the four records in order.  A record whose count is zero
 * ends the scan (the table is filled front to back).  A record whose stored
 * address has the same page id is incremented and the routine returns.
 * Otherwise the record with the lowest count is overwritten with this
 * address and a count of 1.
 *
 * The original indexes the page-error table 1-based off A5 itself: for index
 * `i` in 1..4 it forms 18*i in a data register and addresses (0x0,A5,D0) for
 * the address field and (0x4,A5,D0) for the count.  Since the table starts at
 * A5+0x12, 18*i is MEM_$PAGE_ERRORS[i-1]; that is how the [index - 1]
 * subscripts below arise.  Only the compiler's Pascal 1-based bias differs -
 * every address is the same.
 *
 * Parameters:
 *   phys_addr - Physical address where the parity error occurred
 */
void MEM_$PARITY_LOG(uint32_t phys_addr)
{
    int16_t board;              /* D0 then D1: board number, 1 or 2 */
    int16_t index;              /* D1 then D0: 1-based record index */
    int16_t iter;               /* D0 then D3: dbf loop counter */
    int16_t min_index;          /* D1: index of the lowest-count record */
    int16_t min_count;          /* D2: that record's count, held as a word */
    uint16_t page_id;           /* D2: bits 21..16 of phys_addr */
    mem_$page_error_t *rec;     /* A0: record cursor, stride 0x12 */

    /*
     * 00e0adbe  cmpi.l #0x300000,(0x8,A6)
     * 00e0adc6  bcc.b 0x00e0adcc / moveq #0x1,D0 / moveq #0x2,D0
     */
    if (phys_addr < MEM_BOARD_BOUNDARY) {
        board = MEM_BOARD_LOW;
    } else {
        board = MEM_BOARD_HIGH;
    }

    /* 00e0add4  addq.w #0x1,(0x8,A5,D1*0x1)   with D1 = 2*board */
    MEM_$BOARD_ERRORS[board] += 1;

    /* 00e0addc  moveq #0x3f,D2 / 00e0ade2 and.b (0x9,A6),D2b */
    page_id = MEM_PAGE_ID(phys_addr);

    /*
     * 00e0adde  lea (0x12,A5),A0        ; A0 = &MEM_$PAGE_ERRORS[0]
     * 00e0ade8  ... dbf D0w with D0 = 3 ; four iterations
     */
    index = 1;
    rec = &MEM_$PAGE_ERRORS[0];
    for (iter = 3; iter >= 0; iter--) {
        /* 00e0ade8  tst.w (0x4,A0) / beq.b 0x00e0ae16 */
        if (rec->count == 0) {
            break;
        }

        /* 00e0adee  moveq #0x3f,D3 / and.b (0x1,A0),D3b / cmp.w D2w,D3w */
        if (MEM_PAGE_ID(rec->phys_addr) == page_id) {
            /* 00e0ae06  addq.w #0x1,(0x4,A5,D0*0x1)   with D0 = 18*index */
            MEM_$PAGE_ERRORS[index - 1].count += 1;
            return;
        }

        /* 00e0ae0c  addq.w #0x1,D1w / lea (0x12,A0),A0 */
        index++;
        rec++;
    }

    /*
     * 00e0ae16  moveq #0x1,D1           ; min_index = 1
     * 00e0ae18  move.w (0x16,A5),D2w    ; min_count = MEM_$PAGE_ERRORS[0].count
     * 00e0ae1e  lea (0x24,A5),A0        ; A0 = &MEM_$PAGE_ERRORS[1]
     * 00e0ae22  move.w D3w,D0w          ; index = 2
     * 00e0ae24  ... dbf D3w with D3 = 2 ; three iterations
     */
    min_index = 1;
    min_count = (int16_t)MEM_$PAGE_ERRORS[0].count;
    index = 2;
    rec = &MEM_$PAGE_ERRORS[1];
    for (iter = 2; iter >= 0; iter--) {
        /*
         * 00e0ae24  clr.l D4 / move.w (0x4,A0),D4w   ; candidate, zero-extended
         * 00e0ae26  move.w D2w,D5w / ext.l D5        ; running min, sign-extended
         * 00e0ae2e  cmp.l D5,D4 / bge.b 0x00e0ae38
         *
         * The asymmetry is the original's: the candidate count is treated as
         * unsigned and the running minimum as signed, both widened to 32 bits
         * before the comparison.  Preserved verbatim.
         */
        if ((int32_t)(uint32_t)rec->count < (int32_t)min_count) {
            min_count = (int16_t)rec->count;
            min_index = index;
        }

        /* 00e0ae38  addq.w #0x1,D0w / lea (0x12,A0),A0 */
        index++;
        rec++;
    }

    /*
     * 00e0ae50  move.w #0x1,(0x4,A5,D0*0x1)      ; count first
     * 00e0ae56  move.l (0x8,A6),(0x0,A5,D0*0x1)  ; then the address
     */
    MEM_$PAGE_ERRORS[min_index - 1].count = 1;
    MEM_$PAGE_ERRORS[min_index - 1].phys_addr = phys_addr;
}
