/*
 * Tests for the FP save-area layout recovered in source-djly.
 *
 * The FP context routines are hand-written assembly (fp/sau2/fp_context.s),
 * so there is no C function to drive here.  What CAN be checked on the host
 * is the record the assembly addresses: fp/fp_internal.h is #included and
 * the slot arithmetic the image performs is reproduced against it.
 *
 * Evidence (fp_$save_state 0x00E21B5C / fp_$restore_state 0x00E21B30):
 *
 *   00e21b5c  mulu.w #0x14a,D0             ; slot byte offset
 *   00e21b60  beq -> rts                   ; asid 0 -> no address space
 *   00e21b62  lea (-0x4,A1,D0*0x1),A0      ; A0 = &slot->state
 *   00e21b66  fsave -(A0)                  ; frame grows DOWNWARD
 *   00e21b68  tst.b (0x1,A0)               ; 68881/68882 frame-size byte
 *   00e21b6e  fmovem.x {FP7..FP0},-(A0)    ; 96 bytes
 *   00e21b72  fmovem.l {FPCR FPSR FPIAR},-(A0) ; 12 bytes
 *   00e21b76  move.w #-1,-(A0)             ; the 0xFFFF marker
 *   00e21b7a  move.l A0,(-0x4,A1,D0*0x1)   ; store the frame pointer
 *
 * A1 is FP_$SAVEP, the save-area table BASE POINTER at 0x00E218D0 -- not
 * the "save pending flag" the header used to describe, and not the fixed
 * address 0x00E21928, which is inside FIM_$BUS_ERR.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stddef.h>

#include "base/base.h"
#include "fp/fp_internal.h"

/* ------------------------------------------------------------------ */
/* Globals the header's declarations refer to                          */
/* ------------------------------------------------------------------ */

m68k_ptr_t FP_$SAVEP;
uint16_t FP_$OWNER;
ml_$exclusion_t FP_$EXCLUSION;

/* ------------------------------------------------------------------ */
/* The image's slot arithmetic, spelled out                            */
/* ------------------------------------------------------------------ */

/*
 * `lea (-0x4,A1,D0*0x1),A0` with D0 = asid * 0x14A.  Reproduced against a
 * byte base so that the test is checking the ADDRESS the assembly forms,
 * not the C struct's opinion of it.
 */
static uint8_t *slot_state_cell(uint8_t *base, uint16_t asid)
{
    uint32_t off = (uint32_t)asid * (uint32_t)FP_SAVE_AREA_SIZE;
    return base + off - 4;
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

/*
 * The slot is exactly 0x14A bytes (`mulu.w #0x14a`) and the state pointer
 * is its LAST longword.  The asserts in fp_internal.h are unconditional;
 * this states the numbers in the test output too.
 */
static void test_slot_layout(void)
{
    assert(FP_SAVE_AREA_SIZE == 0x14A);
    assert(sizeof(fp_save_area_t) == 0x14A);
    assert(offsetof(fp_save_area_t, frame) == 0);
    assert(offsetof(fp_save_area_t, state) == 0x146);
    assert(FP_FRAME_MAX == 0x146);

    printf("test_slot_layout: PASSED\n");
}

/*
 * The recovered frame budget: 326 bytes below the state cell is exactly
 * the 0xFFFF marker (2) + FPCR/FPSR/FPIAR (12) + FP0-FP7 (96) plus the
 * 216-byte maximum 68882 FSAVE frame.
 */
static void test_frame_budget(void)
{
    const int marker = 2;
    const int ctrl_regs = 3 * 4;        /* FPCR, FPSR, FPIAR */
    const int fp_regs = 8 * 12;         /* extended precision */
    const int max_fsave = 216;          /* 68882 busy frame */

    assert(marker + ctrl_regs + fp_regs + max_fsave == FP_FRAME_MAX);

    printf("test_frame_budget: PASSED\n");
}

/*
 * The C record and the assembly's address arithmetic must agree slot for
 * slot, and consecutive slots must be 0x14A apart with no padding.
 */
static void test_slot_arithmetic_matches_record(void)
{
    static fp_save_area_t table[4];
    uint8_t *base = (uint8_t *)table;
    uint16_t asid;

    for (asid = 1; asid < 4; asid++) {
        /* `lea (-0x4,A1,D0*0x1),A0` lands on slot[asid-1].state, because
         * the offset is measured to the END of the asid'th slot. */
        assert(slot_state_cell(base, asid) ==
               (uint8_t *)&table[asid - 1].state);
    }

    assert((uint8_t *)&table[1] - (uint8_t *)&table[0] == FP_SAVE_AREA_SIZE);

    printf("test_slot_arithmetic_matches_record: PASSED\n");
}

/*
 * asid 0 makes `mulu.w #0x14a,D0` set Z, and both helpers branch straight
 * to their rts: address space 0 never has FP state saved or restored.
 * Without that guard the `lea` would produce base-4, i.e. the four bytes
 * in front of the table.
 */
static void test_asid_zero_is_skipped(void)
{
    static fp_save_area_t table[2];
    uint8_t *base = (uint8_t *)table;

    assert((uint32_t)0 * FP_SAVE_AREA_SIZE == 0);   /* the `beq` is taken */
    assert(slot_state_cell(base, 0) == base - 4);   /* what it avoids */

    printf("test_asid_zero_is_skipped: PASSED\n");
}

/*
 * Reconstruct what fp_$save_state writes for a non-null FSAVE frame and
 * check that fp_$restore_state's reader agrees:
 *   - the stored pointer is 110 bytes below the FSAVE frame;
 *   - (0x1,A0) at that pointer is the 0xFF low byte of the 0xFFFF marker,
 *     which is non-zero, so the restore path takes the register reload;
 *   - `addq.w #2,A0` then lands on FPCR.
 */
static void test_saved_frame_shape(void)
{
    static fp_save_area_t slot;
    uint8_t *cell = (uint8_t *)&slot.state;
    uint8_t *a0 = cell;
    const uint8_t fsave_frame[4] = { 0x1F, 0x18, 0x00, 0x00 };  /* size 0x18 */
    int i;

    memset(&slot, 0, sizeof slot);

    /* fsave -(A0): four bytes of 68882 idle frame */
    a0 -= sizeof fsave_frame;
    memcpy(a0, fsave_frame, sizeof fsave_frame);

    /* tst.b (0x1,A0): frame size 0x18 != 0 -> registers follow */
    assert(a0[1] != 0);

    /* fmovem.x {FP7..FP0},-(A0) */
    a0 -= 96;
    for (i = 0; i < 96; i++) {
        a0[i] = (uint8_t)(i + 1);
    }

    /* fmovem.l {FPCR FPSR FPIAR},-(A0) */
    a0 -= 12;
    for (i = 0; i < 12; i++) {
        a0[i] = (uint8_t)(0xA0 + i);
    }

    /* move.w #-1,-(A0) */
    a0 -= 2;
    a0[0] = 0xFF;
    a0[1] = 0xFF;

    /* move.l A0,(-0x4,A1,D0*0x1) */
    assert(cell - a0 == 2 + 12 + 96 + (int)sizeof fsave_frame);
    assert(cell - a0 <= FP_FRAME_MAX);

    /* --- restore side --- */
    /* tst.b (0x1,A0) on the marker: 0xFF, so take the register path */
    assert(a0[1] != 0);
    /* addq.w #2,A0 skips the marker and lands on FPCR */
    assert(a0[2] == 0xA0);
    /* the fmovem.l consumes 12 bytes, then fmovem.x starts at FP0's data */
    assert(a0[2 + 12] == 1);
    /* frestore (A0)+ then consumes the FSAVE frame */
    assert(a0[2 + 12 + 96] == 0x1F);

    printf("test_saved_frame_shape: PASSED\n");
}

int main(void)
{
    printf("Running FP save-area layout tests...\n\n");

    test_slot_layout();
    test_frame_budget();
    test_slot_arithmetic_matches_record();
    test_asid_zero_is_skipped();
    test_saved_frame_shape();

    printf("\nAll tests PASSED!\n");
    return 0;
}
