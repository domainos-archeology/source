/*
 * SYSBUS_$INIT - Initialize system bus interrupt handlers
 *
 * Sets up interrupt vectors for disk (0x1d) and ring (0x1b) interrupts,
 * then iterates through the device controller table entry (DCTE) list
 * to populate the interrupt controller data structures.
 *
 * From: 0x00e0ab28
 *
 * Original assembly:
 *   00e0ab28    link.w A6,-0x4
 *   00e0ab2c    pea (A5)
 *   00e0ab2e    lea (0xe22904).l,A5
 *   00e0ab34    subq.l #0x2,SP
 *   00e0ab36    move.l #0xe0aabc,-(SP)     ; DISK_INTERRUPT
 *   00e0ab3c    move.w #0x1d,-(SP)         ; Vector 0x1d
 *   00e0ab40    jsr 0x00e2e800.l           ; IO_$TRAP
 *   00e0ab46    addq.w #0x8,SP
 *   00e0ab48    subq.l #0x2,SP
 *   00e0ab4a    move.l #0xe0ab0c,-(SP)     ; RING_INTERRUPT
 *   00e0ab50    move.w #0x1b,-(SP)         ; Vector 0x1b
 *   00e0ab54    jsr 0x00e2e800.l           ; IO_$TRAP
 *   00e0ab5a    addq.w #0x8,SP
 *   00e0ab5c    movea.l (0x00e2c8b4).l,A0  ; IO_$DCTE_LIST
 *   00e0ab62    bra.b loop_test
 *   loop_body:
 *   00e0ab64    move.w (0x4,A0),D0w        ; dcte->ctype
 *   00e0ab68    beq.b type_0
 *   00e0ab6a    cmpi.w #0x1,D0w
 *   00e0ab6e    beq.b type_1
 *   00e0ab70    cmpi.w #0x2,D0w
 *   00e0ab74    beq.b type_2
 *   00e0ab76    bra.b next
 *   type_0:
 *   00e0ab78    move.l A0,(0x24,A5)        ; type0_dcte
 *   00e0ab7c    move.l (0x38,A0),(0x20,A5) ; type0_do_io
 *   00e0ab82    move.l (0x34,A0),(0x28,A5) ; type0_dinit
 *   00e0ab88    bra.b next
 *   type_1:
 *   00e0ab8a    move.l A0,(0x14,A5)        ; type1_dcte
 *   00e0ab8e    move.l (0x38,A0),(0x10,A5) ; type1_do_io
 *   00e0ab94    move.l (0x34,A0),(0x18,A5) ; type1_dinit
 *   00e0ab9a    bra.b next
 *   type_2:
 *   00e0ab9c    move.l A0,(0x4,A5)         ; type2_dcte
 *   00e0aba0    move.l (0x38,A0),(A5)      ; type2_do_io
 *   00e0aba4    move.l (0x34,A0),(0x8,A5)  ; type2_dinit
 *   next:
 *   00e0abaa    movea.l (0x8,A0),A0        ; dcte->nextp
 *   loop_test:
 *   00e0abae    cmpa.w #0x0,A0
 *   00e0abb2    bne.b loop_body
 *   00e0abb4    movea.l (-0x8,A6),A5
 *   00e0abb8    unlk A6
 *   00e0abba    rts
 */

#include "sysbus/sysbus_internal.h"

void SYSBUS_$INIT(void)
{
    dcte_t *dcte;

    /* Install disk interrupt handler (vector 0x1d) */
    IO_$TRAP(IO_VECTOR_DISK, DISK_INTERRUPT);

    /* Install ring network interrupt handler (vector 0x1b) */
    IO_$TRAP(IO_VECTOR_RING, RING_INTERRUPT);

    /* Iterate through DCTE list and populate interrupt controller data */
    for (dcte = IO_$DCTE_LIST; dcte != NULL; dcte = dcte->nextp) {
        switch (dcte->ctype) {
        case 0:
            /* Controller type 0 - Secondary disk */
            IO_$INT_CTRL.type0_dcte = dcte;
            IO_$INT_CTRL.type0_do_io = (void (*)(dcte_t *))dcte->disk_do_io;
            IO_$INT_CTRL.type0_dinit = (void (*)(dcte_t *))dcte->disk_dinit;
            break;

        case 1:
            /* Controller type 1 - Primary disk */
            IO_$INT_CTRL.type1_dcte = dcte;
            IO_$INT_CTRL.type1_do_io = (void (*)(dcte_t *))dcte->disk_do_io;
            IO_$INT_CTRL.type1_dinit = (void (*)(dcte_t *))dcte->disk_dinit;
            break;

        case 2:
            /* Controller type 2 */
            IO_$INT_CTRL.type2_dcte = dcte;
            IO_$INT_CTRL.type2_do_io = (void (*)(dcte_t *))dcte->disk_do_io;
            IO_$INT_CTRL.type2_dinit = (void (*)(dcte_t *))dcte->disk_dinit;
            break;

        default:
            /* Unknown controller type - skip */
            break;
        }
    }
}
