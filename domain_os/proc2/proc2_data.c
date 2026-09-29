/*
 * proc2_data.c - PROC2 Global Data Definitions
 *
 * Module data blocks PROC2_$UNWIRED_DATA, PROC2_$DATA and PROC2_$WIRED_DATA:
 * Claude Opus 5.5 (source-l2yd).
 *
 * PROC2 has three data segments in the SAU2 map that C addresses, each a
 * MODULE_DATA block linked in the map's order (the address is the ordering
 * key, not the link address); layouts, biases and asserts in proc2/proc2.h
 * and proc2/proc2_internal.h:
 *
 *   D    E2B978  PROC2_WIRED_ASM  size = 690   PROC2_$WIRED_DATA
 *   D    E7BE84  PROC2            size = 1E8   PROC2_$UNWIRED_DATA (A5)
 *   D67  EA551C  PROC2_$DATA      size = 4168  PROC2_$DATA
 *
 * The three 4-byte PROC2 segments (0xE35030, 0xE86050 inside
 * PROC2_CREATE_DAT, 0xE8605C inside PROC2_DELETE_DAT) are A5 anchors of the
 * boot, create and delete code segments; nothing addresses their bytes, so
 * they have no C object.
 */

#include "proc2/proc2_internal.h"

status_$t PROC2_Internal_Error = status_$proc2_internal_error;

/*
 * PROC2_$WIRED_DATA, 0xE2B978..0xE2C007: zero in the image (`gsk read
 * 0xE2B978 1680'); PROC2_$INIT initialises the eventcounts.
 */
MODULE_DATA_DEFINE(proc2_$wired_data_t, PROC2_$WIRED_DATA, 0x00E2B978);

/*
 * PROC2_$UNWIRED_DATA, 0xE7BE84..0xE7C06B (`gsk read 0xE7BE84 488'): zero
 * except the word at 0xE7C06A (+0x1E6), 00 41 - next_upid starts at the
 * value the wrap at 0x00E7333C resets it to.
 */
MODULE_DATA_DEFINE_INIT(proc2_$unwired_data_t, PROC2_$UNWIRED_DATA, 0x00E7BE84, {
    .next_upid = P2_UPID_WRAP_TO,
});

/*
 * PROC2_$DATA, 0xEA551C..0xEA9683: loaded at file offset 0x1B3836, past the
 * end of the SR10.2 SAU2 file (0xE00000..0xE9534E), so zero-filled.
 */
MODULE_DATA_DEFINE(proc2_$data_t, PROC2_$DATA, 0x00EA551C);

/*
 * PTR_PROC2_$DATA - the literal longword cell at 0x00E3238C (image bytes
 * 00 ea 55 1c, PROC2_$DATA's image address).  MST_$WIRE_AREA reads it as a
 * longword VA (`move.l (A1),D1', 0x00E44BA4), so it is a stored-VA cell:
 * the block's link address on the target, the image's value on a host.
 * See proc2/proc2.h.
 */
uint32_t PTR_PROC2_$DATA = ARCH_PTR_TO_VA_STATIC(&PROC2_$DATA, 0x00EA551C);
