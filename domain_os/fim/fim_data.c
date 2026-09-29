/*
 * fim/fim_data.c - FIM Global Data
 *
 * Module data blocks FIM_$DATA and FIM_$WIRED_DATA: Claude Opus 5.5
 * (source-l2yd).
 *
 * The FIM module's data lives in four SAU2 map segments:
 *
 *   D E2126C FIM_         size = 134   FIM_$DATA (below): the A5 block of
 *                                      the Pascal FIM routines
 *   D E213A0 FIM_UNWIRED  size = 4F0   hand-written code (fim/sau2/*.s,
 *                                      one section per routine) with two
 *                                      cells: FIM_$CLEANUP_STACK (defined
 *                                      in fim/sau2/signal.s) and
 *                                      FIM_$INITIAL_STACK_SIZE (below)
 *   D E21890 FIM_WIRED    size = 1074  hand-written code (fim/sau2/*.s and
 *                                      fp/sau2/*.s) with its cells; the
 *                                      data run 0xE21FE6..0xE2277C is
 *                                      FIM_$WIRED_DATA (below), the rest
 *                                      stays with the assembly
 *   D E35004 FIM_WIRED    size = 24    FIM_$COLD_BUS_ERR, code in the
 *                                      OS_INIT_DATA region
 *
 * Layouts, biases and asserts are in fim/fim.h.  Both blocks are linked in
 * the SAU2 map's order; their addresses are ordering keys, not link
 * addresses.
 */

#include "fim/fim_internal.h"

/*
 * FIM_$DATA - map "D E2126C FIM_ size = 134".
 *
 * Image contents, `gsk read 0xE2126C 308': zero except the frame size
 * table at +0x124 (0xE21390): 08 08 0c 00 10 00 00 00 3a 14 20 5c 00 00
 * 00 00.  (The tree's former host table had 12 in the zero slots; these are
 * the image's bytes.)
 */
MODULE_DATA_DEFINE_INIT(fim_$data_t, FIM_$DATA, 0x00E2126C, {
    .frame_size = {
        0x08,   /* format 0: short frame */
        0x08,   /* format 1: throwaway frame */
        0x0C,   /* format 2: instruction exception */
        0x00,   /* format 3 */
        0x10,   /* format 4 */
        0x00,   /* format 5 */
        0x00,   /* format 6 */
        0x00,   /* format 7 */
        0x3A,   /* format 8: 68010 bus error */
        0x14,   /* format 9: coprocessor mid-instruction */
        0x20,   /* format A: short bus cycle fault */
        0x5C,   /* format B: long bus cycle fault */
        0x00,   /* format C */
        0x00,   /* format D */
        0x00,   /* format E */
        0x00,   /* format F */
    },
});

/*
 * FIM_$WIRED_DATA - the data run 0xE21FE6..0xE2277C of map segment
 * "D E21890 FIM_WIRED size = 1074".
 *
 * Image contents, `gsk read 0xE21FE6 1024' and `gsk read 0xE223E6 918':
 *
 *   quit_ec[0..57]   0xE22002..0xE222B9  each eventcount 0 with both waiter
 *                                        links at itself, e.g. 0xE22002:
 *                                        00000000 00e22002 00e22002
 *   quit_inh[0..57]  0xE2248A..0xE224C3  ff throughout
 *   deliv_ec[0..57]  0xE224C4..0xE2277B  as quit_ec, e.g. 0xE224C4:
 *                                        00000000 00e224c4 00e224c4
 *
 * and zero elsewhere.  The self links are the empty-queue state EC_$INIT
 * produces, spelled as the eventcount's own address like the other
 * pre-linked eventcounts in the tree (pmap/pmap_data.c).
 */
#define FIM_EC_SELF(t, i)                                                    \
    { .value = 0,                                                           \
      .waiter_list_head = (ec_$eventcount_waiter_t *)&FIM_$WIRED_DATA.t[i], \
      .waiter_list_tail = (ec_$eventcount_waiter_t *)&FIM_$WIRED_DATA.t[i] }
#define FIM_EC_SELF_8(t, b)                                                  \
    FIM_EC_SELF(t, (b) + 0), FIM_EC_SELF(t, (b) + 1),                       \
    FIM_EC_SELF(t, (b) + 2), FIM_EC_SELF(t, (b) + 3),                       \
    FIM_EC_SELF(t, (b) + 4), FIM_EC_SELF(t, (b) + 5),                       \
    FIM_EC_SELF(t, (b) + 6), FIM_EC_SELF(t, (b) + 7)
/* FIM_AS_COUNT = 58 = 7 * 8 + 2 */
#define FIM_EC_SELF_ALL(t)                                                   \
    FIM_EC_SELF_8(t, 0),  FIM_EC_SELF_8(t, 8),  FIM_EC_SELF_8(t, 16),       \
    FIM_EC_SELF_8(t, 24), FIM_EC_SELF_8(t, 32), FIM_EC_SELF_8(t, 40),       \
    FIM_EC_SELF_8(t, 48), FIM_EC_SELF(t, 56),   FIM_EC_SELF(t, 57)
_Static_assert(FIM_AS_COUNT == 58, "FIM_EC_SELF_ALL spells out 58 eventcounts");

#define FIM_FF_8 -1, -1, -1, -1, -1, -1, -1, -1

MODULE_DATA_DEFINE_INIT(fim_$wired_data_t, FIM_$WIRED_DATA, 0x00E21FE6, {
    .quit_ec  = { FIM_EC_SELF_ALL(quit_ec) },
    .quit_inh = { FIM_FF_8, FIM_FF_8, FIM_FF_8, FIM_FF_8,
                  FIM_FF_8, FIM_FF_8, FIM_FF_8, -1, -1 },
    .deliv_ec = { FIM_EC_SELF_ALL(deliv_ec) },
});

/*
 * FIM_$INITIAL_STACK_SIZE - Bytes reserved above a new process's startup
 * context on its initial stack (used by PROC2_$CREATE / PROC2_$FORK).
 * Original address: 0x00E21824 (4 bytes; image value 0x00000008), a cell of
 * the FIM_UNWIRED code segment (see fim/fim.h); its own `.text.' section
 * (fim/fim_internal.h) links it there, between fim/sau2/single_step.s and
 * fim/sau2/fault_return.s.
 */
uint32_t FIM_$INITIAL_STACK_SIZE FIM_INITIAL_STACK_SIZE_SECTION = 8;

/*
 * FIM_$SPUR_CNT (0x00E21F7E) is defined with FIM_$SPURIOUS_INT, which
 * reaches it off its module base, in fim/sau2/spurious_int.s (source-kt66).
 */
