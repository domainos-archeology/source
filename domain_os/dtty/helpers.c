/*
 * dtty/helpers.c - DTTY module-local helpers
 *
 * Re-emitted from the image:
 *   dtty_$get_disp_type  0x00E1D588 (10 bytes)
 *   dtty_$clear_window   0x00E1D592 (32 bytes)
 *   dtty_$report_error   0x00E1D5B2 (120 bytes)
 *   dtty_$load_font      0x00E1D668 (40 bytes)
 * All are called only from within DTTY.
 */

#include "dtty/dtty_internal.h"
#include "vfmt/vfmt.h"

/*
 * Constant cells in the code region between dtty_$report_error and
 * dtty_$load_font (cell = pea address + 2 + displacement; bytes read from
 * the image with `gsk read 0x00e1d62a`):
 *
 *   0x00E1D62A  "%/Error status %h returned from %$"   (34 bytes)
 *   0x00E1D64C  "%."                                    (2 bytes)
 *   0x00E1D64E  " while attempting to%."                (22 bytes)
 *   0x00E1D664  00 00 00 00                            a zero longword
 *
 * VFMT format text: "%/" starts a new line, "%h" prints the next argument
 * in hex, "%$" ends the text without a newline and "%." ends it with one.
 * Every VFMT_$WRITE10 call pushes three operands; the zero cell at
 * 0x00E1D664 is what stands in for the operands a call does not use, and it
 * is duplicated on the stack with `move.l (SP),-(SP)`.
 */
static const char dtty_$fmt_error_status_00e1d62a[] = "%/Error status %h returned from %$";
static const char dtty_$fmt_newline_00e1d64c[] = "%.";
static const char dtty_$fmt_while_00e1d64e[] = " while attempting to%.";
static const uint32_t dtty_$zero_00e1d664 = 0;

/*
 * dtty_$get_disp_type - 0x00E1D588
 *
 *   00e1d58c  move.w (A5),D0w    ; A5 = 0xE2E00C, the module block, whose
 *                                ; first word is DTTY_$DISP_TYPE
 *
 * Callers: dtty_$load_font 0x00E1D674 and 0x00E1D6A2, both of which have
 * just loaded A5.
 */
uint16_t dtty_$get_disp_type(void)
{
    return DTTY_$DISP_TYPE;
}

/*
 * dtty_$clear_window - 0x00E1D592
 *
 *   00e1d598  movea.l (0xc,A6),A2 ; clr.l (A2)     ; *status_ret = 0
 *   00e1d59e  pea (A2) ; move.l (0x8,A6),-(SP)     ; (region, status_ret)
 *   00e1d5a4  jsr SMD_$CLEAR_WINDOW (0x00E8495C)
 */
void dtty_$clear_window(void *region, status_$t *status_ret)
{
    *status_ret = status_$ok;
    SMD_$CLEAR_WINDOW((smd_rect_t *)region, status_ret);
}

/*
 * dtty_$report_error - 0x00E1D5B2
 *
 * Frame: (0x8,A6) status longword (its ADDRESS is what "%h" is given),
 * (0xC,A6) func_name, (0x10,A6) context -> A2.
 *
 *   00e1d5bc  VFMT_$WRITE10(&"%/Error status %h returned from %$", &status, &0)
 *   00e1d5d2  VFMT_$WRITE10(func_name, &0, &0)
 *   00e1d5e6  cmpi.b #'$',(A2) / beq 0x00E1D612
 *   00e1d5ec  VFMT_$WRITE10(&" while attempting to%.", &0, &0)
 *   00e1d600  VFMT_$WRITE10(context, &0, &0)
 *   00e1d612  VFMT_$WRITE10(&"%.", &0, &0)
 *
 * The three-operand shape is the real one; the old body passed "\r\n"
 * literals that do not exist in the image.
 */
void dtty_$report_error(status_$t status, const char *func_name, const char *context)
{
    VFMT_$WRITE10(dtty_$fmt_error_status_00e1d62a, &status, &dtty_$zero_00e1d664);
    VFMT_$WRITE10(func_name, &dtty_$zero_00e1d664, &dtty_$zero_00e1d664);
    if (*context != '$') {
        VFMT_$WRITE10(dtty_$fmt_while_00e1d64e, &dtty_$zero_00e1d664, &dtty_$zero_00e1d664);
        VFMT_$WRITE10(context, &dtty_$zero_00e1d664, &dtty_$zero_00e1d664);
    }
    VFMT_$WRITE10(dtty_$fmt_newline_00e1d64c, &dtty_$zero_00e1d664, &dtty_$zero_00e1d664);
}

/*
 * dtty_$load_font - 0x00E1D668
 *
 *   00e1d66e  lea (0xe2e00c).l,A5          ; module block
 *   00e1d674  bsr.w dtty_$get_disp_type    ; result discarded
 *   00e1d678  move.l (0xc,A6),-(SP)        ; status_ret
 *   00e1d67c  move.l (0x8,A6),-(SP)        ; font_ptr (a pointer to the
 *                                          ; font pointer; the callee
 *                                          ; dereferences it)
 *   00e1d680  jsr SMD_$COPY_FONT_TO_MD_HDM (0x00E1D750)
 *   00e1d686  clr.w D0w                    ; D0 = 0; the caller (DTTY_$INIT
 *                                          ; 0x00E34CC4) pushes no result slot
 *
 * The 8 bytes of arguments are left for `unlk` to discard.
 */
void dtty_$load_font(void **font_ptr, status_$t *status_ret)
{
    (void)dtty_$get_disp_type();                      /* 0x00E1D674 */
    SMD_$COPY_FONT_TO_MD_HDM(font_ptr, status_ret);   /* 0x00E1D680 */
}
