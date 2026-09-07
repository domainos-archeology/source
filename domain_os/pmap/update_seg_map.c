/*
 * pmap_$update_seg_map - release or invalidate a page after it has been
 *                        written back
 *
 * A nested Pascal procedure of PMAP_$FLUSH, called only from the sibling
 * nested procedure pmap_$flush_write_batch (`movea.l A4,A1` /
 * `bsr.w 0x00e1359c` at 0x00E13718).  A1 carries the STATIC LINK -- the
 * frame pointer of PMAP_$FLUSH -- not an ASTE, and the routine reaches two
 * of PMAP_$FLUSH's own arguments through it:
 *
 *   (0x08,A2) = PMAP_$FLUSH's `aste`   argument
 *   (0x15,A2) = the LOW byte of PMAP_$FLUSH's `flags` word at (0x14,A6),
 *               so `btst.b #0x0` there is flags bit 0 ("remove from the
 *               MMU after the flush")
 *
 * The flattening therefore passes `aste` and `flags` explicitly, and
 * pmap_$flush_write_batch forwards them.
 *
 * Parameters (the three real stack arguments):
 *   segmap_entry - &segmap[page_idx]  (0x08,A6)
 *   vpn          - virtual page number (0x0C,A6)
 *   page_idx     - page index within the segment (0x10,A6)
 *
 * Original address: 0x00E1359C, 112 bytes.
 *
 * Full instruction trace:
 *   00e1359c  link.w A6,-0x4
 *   00e135a0  movem.l {A3 A2 D2},-(SP)
 *   00e135a4  movea.l A1,A2            ; A2 = PMAP_$FLUSH's frame
 *   00e135a6  move.l (0xc,A6),D2       ; vpn
 *   00e135aa  btst.b #0x0,(0x15,A2)    ; flags & 0x0001
 *   00e135b0  beq.b 0x00e135fa
 *   00e135b2  move.l D2,-(SP)          ; vpn                 (arg 3)
 *   00e135b4  move.l (0x8,A6),-(SP)    ; segmap_entry        (arg 2)
 *   00e135b8  move.l (0x8,A2),-(SP)    ; aste                (arg 1)
 *   00e135bc  jsr 0x00e00f16.l         ; AST_$INVALIDATE_PAGE
 *   00e135c2  lea (0xc,SP),SP
 *   00e135c6  tst.b (0x00e248e0).l     ; NETLOG_$OK_TO_LOG
 *   00e135cc  bpl.b 0x00e13602
 *   00e135ce  subq.l #0x2,SP           ; Pascal result slot
 *   00e135d0  clr.l -(SP)              ; param7, param8 = 0
 *   00e135d2  clr.w -(SP)              ; param6 = 0
 *   00e135d4  move.w D2w,-(SP)         ; param5 = vpn (low word)
 *   00e135d6  move.w (0x10,A6),-(SP)   ; param4 = page_idx
 *   00e135da  movea.l (0x8,A2),A0
 *   00e135de  move.w (0xc,A0),-(SP)    ; param3 = aste->timestamp (+0x0C)
 *   00e135e2  movea.l (0x8,A2),A1
 *   00e135e6  movea.l (0x4,A1),A3      ; aste->aote
 *   00e135ea  pea (0x10,A3)            ; param2 = &aote->uid (+0x10)
 *   00e135ee  move.w #0x5,-(SP)        ; param1 = kind 5
 *   00e135f2  jsr 0x00e71b38.l         ; NETLOG_$LOG_IT
 *   00e135f8  bra.b 0x00e13602         ; NOTE: the 18 argument bytes and the
 *                                      ;       result slot are never popped;
 *                                      ;       the `unlk` discards them.
 *   00e135fa  move.l D2,-(SP)          ; vpn
 *   00e135fc  jsr 0x00e0cc64.l         ; MMAP_$AVAIL(vpn)
 *   00e13602  movem.l (-0x10,A6),{D2 A2 A3}
 *   00e13608  unlk A6
 *   00e1360a  rts
 *
 * Note that MMAP_$AVAIL's single argument is not popped either (the `unlk`
 * cleans it up), and that NETLOG_$LOG_IT is only reached on the
 * invalidate path.
 */

#include "pmap/pmap_internal.h"
#include "ast/ast.h"
#include "mmap/mmap.h"
#include "netlog/netlog.h"

void pmap_$update_seg_map(aste_t *aste, uint16_t flags,
                          uint32_t *segmap_entry, uint32_t vpn,
                          uint16_t page_idx)
{
    /* 0x00E135AA: bit 0 of the LOW byte of PMAP_$FLUSH's flags word */
    if ((flags & 0x0001) != 0) {
        /* 0x00E135BC */
        AST_$INVALIDATE_PAGE(aste, segmap_entry, vpn);

        /* 0x00E135C6: tst.b / bpl -- a Domain boolean, TRUE is negative */
        if (NETLOG_$OK_TO_LOG < 0) {
            /* 0x00E135F2 */
            NETLOG_$LOG_IT(5, &aste->aote->uid.high,
                           aste->timestamp,
                           page_idx,
                           (uint16_t)vpn,
                           0, 0, 0);
        }
    } else {
        /* 0x00E135FC */
        MMAP_$AVAIL(vpn);
    }
}
