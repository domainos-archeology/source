/*
 * TERM_$GET_EC - EC2 handle for a terminal line's input or output eventcount
 *
 * Validates *ec_id (0 = input, 1 = output), maps the line with
 * TERM_$GET_REAL_LINE, registers the DTTE's embedded eventcount with
 * EC2_$REGISTER_EC1 and stores the handle it returns in A0 through the
 * third argument.
 *
 * Parameters (frame 0x00E723C8..0x00E723CC, 0x00E723E2, 0x00E7243E):
 *   0x08 ec_id     - word by reference (A2)
 *   0x0C term_line - word by reference; its VALUE goes to GET_REAL_LINE
 *   0x10 ec_ret    - a LONGWORD cell that receives the EC2 handle
 *                    (`move.l A0,(A1)` at 0x00E72442).  term/term.h declares
 *                    it as ec2_$eventcount_t *; the store lands in that
 *                    record's first longword, `value`, which is where an
 *                    EC2 record keeps its EC1 pointer/index.
 *   0x14 status    - status return (A3)
 *
 * Original address: 0x00e723c0, 142 bytes
 *
 *   00e723d0  cmpi.w #1,(A2) / bls; 0xb0004 (invalid option) / bra exit
 *   00e723de  subq.l #2 / pea (A3) / move.w (*term_line) / jsr GET_REAL_LINE -> D2
 *   00e723f2  tst.l (A3) / bne -> exit
 *   00e723f6  move.w (A2),D0w / beq -> input; cmpi.w #1 / beq -> output; bra exit
 *   00e72402  pea (A3); D0 = line*64 - line*8 = line*0x38; pea (0xc,0xe2dc90,D0)
 *   00e7241e  pea (A3); same; pea (0x18,0xe2dc90,D0)
 *   00e72438  jsr EC2_$REGISTER_EC1 -> A0                 ; args reclaimed by unlk
 *   00e7243e  movea.l (0x10,A6),A1 / move.l A0,(A1)
 *
 * The eventcounts live INSIDE the DTTE at +0x0C and +0x18 (12 bytes each);
 * term.h's dtte_t still models those as 4-byte pointers followed by
 * padding (bead noted in term.h's appended comment).
 */

#include "term/term_internal.h"

void TERM_$GET_EC(unsigned short *ec_id, short *term_line,
                  ec2_$eventcount_t *ec_ret, status_$t *status_ret)
{
    int16_t line;               /* D2w */
    dtte_t *entry;
    ec_$eventcount_t *ec;
    void *handle;               /* A0 */

    /* 0x00E723D0..0x00E723DC: unsigned "<= 1" */
    if (*ec_id > 1) {
        *status_ret = status_$term_invalid_option;
        return;
    }

    /* 0x00E723DE..0x00E723F0 */
    line = TERM_$GET_REAL_LINE(*term_line, status_ret);

    /* 0x00E723F2 */
    if (*status_ret != status_$ok) {
        return;
    }

    /* 0x00E723F6..0x00E72434: the index is a zero-extended word times 0x38 */
    entry = &DTTE[(uint16_t)line];
    if (*ec_id == 0) {
        ec = (ec_$eventcount_t *)&entry->input_ec;      /* +0x0C */
    } else if (*ec_id == 1) {
        ec = (ec_$eventcount_t *)&entry->output_ec;     /* +0x18 */
    } else {
        return;     /* unreachable after the check above; kept as the image has it */
    }

    /* 0x00E72438..0x00E72442: the handle comes back in A0 and is stored as
     * ONE longword (a 32-bit VA) into the caller's cell */
    handle = EC2_$REGISTER_EC1(ec, status_ret);
    *(m68k_ptr_t *)ec_ret = ARCH_PTR_TO_VA(handle);
}
