/*
 * vfmt/write.c - VFMT_$WRITE implementation
 *
 * Formats into a 200-byte stack buffer and writes the result to terminal
 * line 1 (the console) in chunks of at most 100 characters.
 *
 * Original address: 0x00e6afe2
 * Size: 136 bytes
 *
 * Assembly (0x00e6afe2):
 *   link.w  A6,-0xd0
 *   movem.l {D3 D2},-(SP)
 *   move.l  (0xc,A6),-(SP)      ; args (the caller's argument-list pointer)
 *   pea     (-0xd0,A6)          ; &out_len
 *   pea     (0x78,PC)           ; -> 0x00e6b06c, the constant 200
 *   pea     (-0xc8,A6)          ; buf
 *   move.l  (0x8,A6),-(SP)      ; format
 *   bsr.w   VFMT_$MAIN
 *   lea     (0x14,SP),SP
 *   move.w  (-0xd0,A6),D3w      ; D3 = characters produced
 *   moveq   #0x1,D2             ; D2 = 1-based offset into buf
 *   bra.b   check
 * loop:
 *   cmpi.w  #0x64,D3w
 *   bgt.b   long_chunk
 *   move.w  D3w,(-0xd0,A6)      ; reuse the out_len word as the count cell
 *   pea     (-0xcc,A6)          ; &status
 *   pea     (-0xd0,A6)          ; &count
 *   lea     (0x0,A6,D2w*0x1),A0
 *   pea     (-0xc9,A0)          ; &buf[D2-1]
 *   pea     (0x44,PC)           ; -> 0x00e6b070, the constant line number 1
 *   jsr     TERM_$WRITE
 *   clr.w   D3w
 *   bra.b   done                ; note: the 0x10 bytes of arguments are not
 *                               ; popped; unlk discards them
 * long_chunk:
 *   pea     (-0xcc,A6)          ; &status
 *   pea     (0x30,PC)           ; -> 0x00e6b06e, the constant 100
 *   lea     (0x0,A6,D2w*0x1),A0
 *   pea     (-0xc9,A0)          ; &buf[D2-1]
 *   pea     (0x26,PC)           ; -> 0x00e6b070, the constant line number 1
 *   jsr     TERM_$WRITE
 *   lea     (0x10,SP),SP
 *   addi.w  #0x64,D2w
 *   subi.w  #0x64,D3w
 * check:
 *   tst.w   D3w
 *   bgt.b   loop
 * done:
 *   movem.l (-0xd8,A6),{D2 D3}
 *   unlk    A6
 *   rts
 *
 * The status returned by TERM_$WRITE is stored in the frame at A6-0xCC and
 * never examined: a failing console write is silently dropped and the loop
 * carries on.  That is reproduced here.
 *
 * `args` is the address of the caller's first format argument.  VFMT_$WRITE10
 * (0x00e825f4) is a procedure-variable trampoline whose installed target is
 * this routine; the VFMT_$WRITEN thunk it jumps through (0x00e6b0a4) computes
 * that address with `pea (0xc,SP)` and passes it as the second argument.
 */

#include "vfmt/vfmt_internal.h"
#include "term/term.h"

/*
 * Constant cells in this module's code region, passed by address with
 * `pea (d,PC)`.  None of the callees writes through them.
 */
static const int16_t  vfmt_$write_buf_size_00e6b06c = 200;   /* 0x00C8 */
static const uint16_t vfmt_$write_chunk_00e6b06e    = 100;   /* 0x0064 */
static const uint16_t vfmt_$write_line_00e6b070     = 1;     /* console line */

/* Size of the format buffer at A6-0xC8; the same 200 as the cell above. */
#define VFMT_WRITE_BUF_SIZE     200

/*
 * VFMT_$WRITE - Format and write to the console
 *
 * Parameters:
 *   format - Domain/OS VFMT format string (terminated by "%$")
 *   args   - address of the caller's argument list
 */
void VFMT_$WRITE(const char *format, void *args)
{
    char buf[VFMT_WRITE_BUF_SIZE];  /* A6-0xC8 */
    int16_t out_len;                /* A6-0xD0, also the TERM_$WRITE count */
    status_$t status;               /* A6-0xCC, written but never read */
    int16_t remaining;              /* D3w */
    int16_t pos;                    /* D2w, 1-based index into buf */

    VFMT_$MAIN(format, buf, &vfmt_$write_buf_size_00e6b06c, &out_len, args);

    remaining = out_len;
    pos = 1;

    while (remaining > 0) {
        if (remaining <= 100) {
            out_len = remaining;
            TERM_$WRITE((void *)&vfmt_$write_line_00e6b070,
                        &buf[pos - 1],
                        (unsigned short *)&out_len,
                        &status);
            remaining = 0;
        } else {
            TERM_$WRITE((void *)&vfmt_$write_line_00e6b070,
                        &buf[pos - 1],
                        (unsigned short *)&vfmt_$write_chunk_00e6b06e,
                        &status);
            pos = (int16_t)(pos + 100);
            remaining = (int16_t)(remaining - 100);
        }
    }
}
