/*
 * proc1/sau2/ready_list.s - the ready-list primitives, as the image has them
 *
 * Two blocks of hand-written code from the PROC1_ASM segment:
 *
 *   0x00E206D2 .. 0x00E206EE   PROC1_$REMOVE_READY (4-byte stack gate)
 *                              proc1_$remove_from_ready_list_int (A1 = pcb)
 *   0x00E207D4 .. 0x00E2087C   PROC1_$REORDER_READY (gate)
 *                              proc1_$reorder_if_needed_int (A1 = pcb)
 *                              PROC1_$ADD_READY (gate)
 *                              proc1_$add_ready_body_int (A1 = pcb, FIFO)
 *                              proc1_$insert_into_ready_list (A1 = pcb, LIFO)
 *                              shared insertion tail + rts
 *
 * The `_int' bodies take the PCB in A1 and are `bsr'd only from assembly
 * (proc1/sau2/clr_lock.s, set_lock.s, ec/sau2/advance_int.s).  C code calls
 * the stack gates; the internal C names proc1_$remove_from_ready_list,
 * proc1_$reorder_if_needed and proc1_$add_ready_body are aliased to the
 * gates below so the portable C bodies (proc1/remove_from_ready_list.c and
 * friends, built only for other targets) and this file present one API.
 *
 * Each block has its own section, named after its map symbol, so the
 * generated layout (tools/gen_layout_ld.py) keeps it at its image slot: the
 * first between EC_$WAITN and EC_$ADVANCE, the second after ADVANCE.
 *
 * The only deviations from the image bytes are the two PC-relative data
 * references (`movea.l (d,PC)' / `cmpa.l (d,PC)' to PROC1_$READY_PCB,
 * 0xE1EC3A) which become absolute references to the C cell; every branch
 * displacement matches the image.
 */

        .extern PROC1_$READY_PCB
        .extern PROC1_$READY_COUNT

/* ------------------------------------------------------------------ */
/* 0x00E206D2: PROC1_$REMOVE_READY / proc1_$remove_from_ready_list_int    */
/* ------------------------------------------------------------------ */

        .section ".text.PROC1_$REMOVE_READY","ax",@progbits
        .balign 2

        .globl  PROC1_$REMOVE_READY
        .globl  proc1_$remove_from_ready_list
        .set    proc1_$remove_from_ready_list, PROC1_$REMOVE_READY
PROC1_$REMOVE_READY:
        movea.l (0x4,%sp),%a1           /* 00E206D2  22 6f 00 04 */

        .globl  proc1_$remove_from_ready_list_int
proc1_$remove_from_ready_list_int:
        move.l  (%a1),%d0               /* 00E206D6  20 11        D0 = pcb->nextp */
        move.l  (0x4,%a1),%d1           /* 00E206D8  22 29 00 04  D1 = pcb->prevp */
        movea.l %d1,%a0                 /* 00E206DC  20 41 */
        move.l  %d0,(%a0)               /* 00E206DE  20 80        prev->nextp = next */
        movea.l %d0,%a0                 /* 00E206E0  20 40 */
        move.l  %d1,(0x4,%a0)           /* 00E206E2  21 41 00 04  next->prevp = prev */
        subq.w  #1,PROC1_$READY_COUNT   /* 00E206E6  53 79 00 e1 eb d0 */
        rts                             /* 00E206EC  4e 75 */

/* ------------------------------------------------------------------ */
/* 0x00E207D4: PROC1_$REORDER_READY / proc1_$reorder_if_needed_int         */
/* ------------------------------------------------------------------ */

        .section ".text.PROC1_$REORDER_READY","ax",@progbits
        .balign 2

        .globl  PROC1_$REORDER_READY
        .globl  proc1_$reorder_if_needed
        .set    proc1_$reorder_if_needed, PROC1_$REORDER_READY
PROC1_$REORDER_READY:
        movea.l (0x4,%sp),%a1           /* 00E207D4  22 6f 00 04 */

        .globl  proc1_$reorder_if_needed_int
proc1_$reorder_if_needed_int:
        move.l  (0x40,%a1),%d1          /* 00E207D8  22 29 00 40  D1 = locks */
        cmpa.l  PROC1_$READY_PCB,%a1    /* 00E207DC  b3 fa e4 5c  (PC-rel in image) */
        beq.b   .Lcheck_next            /* 00E207E0  67 1c        head: no prev test */
        movea.l (0x4,%a1),%a0           /* 00E207E2  20 69 00 04  A0 = prev */
        cmp.l   (0x40,%a0),%d1          /* 00E207E6  b2 a8 00 40 */
        bhi.b   .Lmove                  /* 00E207EA  62 0c        more locks than prev */
        bne.b   .Lcheck_next            /* 00E207EC  66 10        fewer: prev is fine */
        move.w  (0x52,%a1),%d0          /* 00E207EE  30 29 00 52 */
        cmp.w   (0x52,%a0),%d0          /* 00E207F2  b0 68 00 52 */
        bls.b   .Lcheck_next            /* 00E207F6  63 06        state <= prev's */
.Lmove:
        bsr.w   proc1_$remove_from_ready_list_int   /* 00E207F8  61 00 fe dc */
        bra.b   proc1_$insert_into_ready_list       /* 00E207FC  60 46 */
.Lcheck_next:
        movea.l (%a1),%a0               /* 00E207FE  20 51        A0 = next */
        cmp.l   (0x40,%a0),%d1          /* 00E20800  b2 a8 00 40 */
        bhi.b   .Ldone                  /* 00E20804  62 74        more locks than next */
        bne.b   .Lmove_next             /* 00E20806  66 0a        fewer: move back */
        move.w  (0x52,%a1),%d0          /* 00E20808  30 29 00 52 */
        cmp.w   (0x52,%a0),%d0          /* 00E2080C  b0 68 00 52 */
        bcc.b   .Ldone                  /* 00E20810  64 68        state >= next's */
.Lmove_next:
        bsr.w   proc1_$remove_from_ready_list_int   /* 00E20812  61 00 fe c2 */
        move.l  (0x40,%a1),%d1          /* 00E20816  22 29 00 40 */
        move.w  (0x52,%a1),%d0          /* 00E2081A  30 29 00 52 */
        bra.b   .Linsert_scan           /* 00E2081E  60 34        into the LIFO walk */

/* 0x00E20820: PROC1_$ADD_READY / proc1_$add_ready_body_int (FIFO) */

        .globl  PROC1_$ADD_READY
        .globl  proc1_$add_ready_body
        .set    proc1_$add_ready_body, PROC1_$ADD_READY
PROC1_$ADD_READY:
        movea.l (0x4,%sp),%a1           /* 00E20820  22 6f 00 04 */

        .globl  proc1_$add_ready_body_int
proc1_$add_ready_body_int:
        move.l  (0x40,%a1),%d1          /* 00E20824  22 29 00 40 */
        move.w  (0x52,%a1),%d0          /* 00E20828  30 29 00 52 */
        movea.l PROC1_$READY_PCB,%a0    /* 00E2082C  20 7a e4 0c  (PC-rel in image) */
        bra.b   .Lfifo_cmp              /* 00E20830  60 02 */
.Lfifo_next:
        movea.l (%a0),%a0               /* 00E20832  20 50 */
.Lfifo_cmp:
        cmp.l   (0x40,%a0),%d1          /* 00E20834  b2 a8 00 40 */
        bhi.b   .Llink                  /* 00E20838  62 28 */
        bne.b   .Lfifo_next             /* 00E2083A  66 f6 */
        cmp.w   (0x52,%a0),%d0          /* 00E2083C  b0 68 00 52 */
        bls.b   .Lfifo_next             /* 00E20840  63 f0        <= : after equals */
        bra.b   .Llink                  /* 00E20842  60 1e */

/* 0x00E20844: proc1_$insert_into_ready_list (LIFO) */

        .globl  proc1_$insert_into_ready_list
proc1_$insert_into_ready_list:
        move.l  (0x40,%a1),%d1          /* 00E20844  22 29 00 40 */
        move.w  (0x52,%a1),%d0          /* 00E20848  30 29 00 52 */
        movea.l PROC1_$READY_PCB,%a0    /* 00E2084C  20 7a e3 ec  (PC-rel in image) */
        bra.b   .Llifo_cmp              /* 00E20850  60 02 */
.Llifo_next:
        movea.l (%a0),%a0               /* 00E20852  20 50 */
.Linsert_scan:
.Llifo_cmp:
        cmp.l   (0x40,%a0),%d1          /* 00E20854  b2 a8 00 40 */
        bhi.b   .Llink                  /* 00E20858  62 08 */
        bne.b   .Llifo_next             /* 00E2085A  66 f6 */
        cmp.w   (0x52,%a0),%d0          /* 00E2085C  b0 68 00 52 */
        bcs.b   .Llifo_next             /* 00E20860  65 f0        <  : before equals */

/* 0x00E20862: the shared insertion tail */
.Llink:
        move.l  %a0,(%a1)               /* 00E20862  22 88        pcb->nextp = pos */
        move.l  (0x4,%a0),%d0           /* 00E20864  20 28 00 04  D0 = pos->prevp */
        move.l  %d0,(0x4,%a1)           /* 00E20868  23 40 00 04  pcb->prevp = D0 */
        move.l  %a1,(0x4,%a0)           /* 00E2086C  21 49 00 04  pos->prevp = pcb */
        movea.l %d0,%a0                 /* 00E20870  20 40 */
        move.l  %a1,(%a0)               /* 00E20872  20 89        prev->nextp = pcb */
        addq.w  #1,PROC1_$READY_COUNT   /* 00E20874  52 79 00 e1 eb d0 */
.Ldone:
        rts                             /* 00E2087A  4e 75 */
