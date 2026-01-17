/*
 * dtty/helpers.c - DTTY internal helper functions
 *
 * Internal functions used by the DTTY subsystem.
 *
 * Original addresses:
 *   dtty_$get_disp_type: 0x00E1D588
 *   dtty_$clear_window:  0x00E1D592
 *   dtty_$report_error:  0x00E1D5B2
 *   dtty_$load_font:     0x00E1D668
 */

#include "dtty/dtty_internal.h"

/* Forward declaration for ERROR_$PRINT (in error subsystem) */
extern void ERROR_$PRINT(const char *format, void *args, const char *newline);

/*
 * dtty_$get_disp_type - Get current display type
 *
 * Returns the current display type from the DTTY data block.
 * In the original code, this function uses A5 as a base pointer
 * to the DTTY data block at 0x00E2E00C.
 *
 * Assembly (0x00E1D588):
 *   link.w  A6,#-0x8
 *   move.w  (A5),D0w       ; D0 = display type (A5 points to data block)
 *   unlk    A6
 *   rts
 *
 * Returns:
 *   Display type (1 = 15", 2 = 19")
 */
uint16_t dtty_$get_disp_type(void)
{
    return DTTY_$DISP_TYPE;
}

/*
 * dtty_$clear_window - Clear display window
 *
 * Clears a display window region. This is a thin wrapper around
 * SMD_$CLEAR_WINDOW that first clears the status return value.
 *
 * Parameters:
 *   region - Window region descriptor (x1, y1, x2, y2)
 *   status_ret - Status return pointer
 *
 * Assembly (0x00E1D592):
 *   link.w  A6,#-0x4
 *   pea     (A2)
 *   movea.l (0xc,A6),A2    ; A2 = status_ret
 *   clr.l   (A2)           ; *status_ret = 0
 *   pea     (A2)           ; push status_ret
 *   move.l  (0x8,A6),-(SP) ; push region
 *   jsr     SMD_$CLEAR_WINDOW
 *   movea.l (-0x8,A6),A2
 *   unlk    A6
 *   rts
 */
void dtty_$clear_window(void *region, status_$t *status_ret)
{
    *status_ret = status_$ok;
    SMD_$CLEAR_WINDOW(region, status_ret);
}

/*
 * dtty_$report_error - Report an error during DTTY initialization
 *
 * Prints an error message in the format:
 *   " Error status %h returned from <func_name>"
 * If context doesn't start with '$':
 *   " performing <context>"
 *   "<context>"
 * Then a newline.
 *
 * Parameters:
 *   status - Error status code (printed as hex)
 *   func_name - Name of function that failed
 *   context - Additional context (if first char is '$', no "performing" message)
 *
 * Note: The original code has strings:
 *   " Error status %h returned from "
 *   " performing "
 *   "\r\n"
 *
 * Assembly (0x00E1D5B2):
 *   link.w  A6,#-0x4
 *   pea     (A2)
 *   movea.l (0x10,A6),A2          ; A2 = context
 *   pea     newline               ; "\r\n"
 *   pea     (0x8,A6)              ; &status (for %h)
 *   pea     error_msg             ; " Error status %h returned from "
 *   jsr     ERROR_$PRINT
 *   lea     (0xc,SP),SP
 *   pea     newline               ; "\r\n"
 *   move.l  (SP),-(SP)            ; duplicate
 *   move.l  (0xc,A6),-(SP)        ; push func_name
 *   jsr     ERROR_$PRINT
 *   lea     (0xc,SP),SP
 *   cmpi.b  #'$',(A2)             ; Check if context starts with '$'
 *   beq.b   skip_context
 *   pea     newline               ; "\r\n"
 *   move.l  (SP),-(SP)
 *   pea     performing_msg        ; " performing "
 *   jsr     ERROR_$PRINT
 *   lea     (0xc,SP),SP
 *   pea     newline               ; "\r\n"
 *   move.l  (SP),-(SP)
 *   pea     (A2)                  ; push context
 *   jsr     ERROR_$PRINT
 *   lea     (0xc,SP),SP
 * skip_context:
 *   pea     newline               ; "\r\n"
 *   move.l  (SP),-(SP)
 *   pea     crlf_only             ; "\r\n"
 *   jsr     ERROR_$PRINT
 *   movea.l (-0x8,A6),A2
 *   unlk    A6
 *   rts
 */
void dtty_$report_error(status_$t status, const char *func_name, const char *context)
{
    static const char *error_fmt = " Error status %h returned from ";
    static const char *performing_msg = " performing ";
    static const char *newline = "\r\n";

    /* Print: " Error status <hex> returned from " */
    ERROR_$PRINT(error_fmt, &status, newline);

    /* Print: "<func_name>" */
    ERROR_$PRINT(func_name, newline, newline);

    /* If context doesn't start with '$', print context info */
    if (*context != '$') {
        ERROR_$PRINT(performing_msg, newline, newline);
        ERROR_$PRINT(context, newline, newline);
    }

    /* Print final newline */
    ERROR_$PRINT(newline, newline, newline);
}

/*
 * dtty_$load_font - Load font to hidden display memory
 *
 * Loads a font into hidden display memory for fast text rendering.
 * This is a wrapper that sets up the A5 register for the data block
 * and then calls SMD_$COPY_FONT_TO_MD_HDM.
 *
 * Parameters:
 *   font_ptr - Pointer to pointer to font data
 *   status_ret - Status return pointer
 *
 * Returns:
 *   0 (in D0, as a short)
 *
 * Assembly (0x00E1D668):
 *   link.w  A6,#-0x8
 *   pea     (A5)
 *   lea     (0xe2e00c).l,A5      ; A5 = DTTY data block
 *   bsr.w   dtty_$get_disp_type  ; (return value unused, sets up A5)
 *   move.l  (0xc,A6),-(SP)       ; push status_ret
 *   move.l  (0x8,A6),-(SP)       ; push font_ptr
 *   jsr     SMD_$COPY_FONT_TO_MD_HDM
 *   clr.w   D0w                  ; return 0
 *   movea.l (-0xc,A6),A5
 *   unlk    A6
 *   rts
 */
void dtty_$load_font(void **font_ptr, status_$t *status_ret)
{
    /* Set up display type access (original uses A5 register) */
    dtty_$get_disp_type();

    /* Load the font to hidden display memory
     * Note: SMD_$COPY_FONT_TO_MD_HDM is at 0x00E1D750 based on the
     * original analysis. The function name in the header says
     * SMD_$COPY_FONT_TO_HDM but the actual implementation copies
     * to MD (main display) HDM area. */
    SMD_$COPY_FONT_TO_HDM(*font_ptr, NULL, status_ret);
}
