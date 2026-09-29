/*
 * pmap_data.c - PMAP Module Global Data Definitions
 *
 * Module data blocks PMAP_$DATA and PMAP_$SEGMAP: Claude Opus 5.5
 * (source-iq58).
 *
 * The PMAP module's data (docs/design-per-process-data.md; the addresses are
 * the SAU2 image's, used as ordering keys, not link addresses):
 *
 *   0x00E24D44  PMAP_$DATA    map "D E24D44 PMAP_ size = 7A4": the A5 block
 *                             (timer elements, the exported scalars
 *                             PMAP_$IDLE_INTERVAL..PMAP_$SHUTTING_DOWN_FLAG,
 *                             MOUNT_LOCK); layout in pmap/pmap.h
 *   0x00ED5000  PMAP_$SEGMAP  map AST_PMAPS..AST_PMAPS_END in VM_TABLES:
 *                             the segment map, 0xFC00 bytes
 *
 * plus two status constants inside the PMAP_ code segment.
 */

#include "pmap/pmap_internal.h"

/*
 * ============================================================================
 * PMAP_$DATA - the PMAP_ module block, 0x00E24D44..0x00E254E7
 * ============================================================================
 *
 * Layout, biases and asserts in pmap/pmap.h.  The map places it after the
 * PKT segment (0xE24C9C, PKT_$N_MISSING 0xE24CF4) and before PROC1_
 * (0xE254E8).  Image contents
 * (`gsk read 0xE24D44 0x7A4`); every byte not listed is zero:
 *
 *   +0x740 0xE25484  00 00 06 3c                   idle_interval = 0x63C
 *   +0x750 0xE25494  00 00 00 00 00 e2 54 94 00 e2 54 94   pages_ec
 *   +0x75C 0xE254A0  00 00 00 00 00 e2 54 a0 00 e2 54 a0   r_purifier_ec
 *   +0x768 0xE254AC  00 00 00 00 00 e2 54 ac 00 e2 54 ac   l_purifier_ec
 *   +0x774 0xE254B8  00 00 00 00 00 e2 54 b8 00 e2 54 b8 00 00 00 00 ff ff
 *                                                  mount_lock
 *   +0x788 0xE254CC  00 01                          scan_fract = 1
 *   +0x78E 0xE254D2  00 02                          ws_scan_delta = 2
 *   +0x790 0xE254D4  00 01 00 08 00 05              min/max/ws_interval
 *   +0x798 0xE254DC  00 00 00 00 00 05              short_wait_delay {0, 5}
 *   +0x79E 0xE254E2  00 4d                          ws_random_seed = 0x4D
 *   +0x7A0 0xE254E4  00 05                          current_slot = 5
 *
 * The eventcounts and MOUNT_LOCK are the EC_$INIT / ML_$EXCLUSION_INIT
 * state: an empty circular waiter list whose head and tail point back at
 * the object itself, plus f5 = -1 (unlocked) for the lock.  (The previous
 * pmap_data.c had scan_fract 0xFFFF, low_thresh 1 and ws_scan_delta 0; the
 * image has 1, 0 and 2.)
 */
MODULE_DATA_DEFINE_INIT(pmap_$data_t, PMAP_$DATA, 0x00E24D44, {
    .idle_interval = 0x063C,
    .pages_ec = {
        .value = 0,
        .waiter_list_head = (ec_$eventcount_waiter_t *)&PMAP_$DATA.pages_ec,
        .waiter_list_tail = (ec_$eventcount_waiter_t *)&PMAP_$DATA.pages_ec,
    },
    .r_purifier_ec = {
        .value = 0,
        .waiter_list_head = (ec_$eventcount_waiter_t *)&PMAP_$DATA.r_purifier_ec,
        .waiter_list_tail = (ec_$eventcount_waiter_t *)&PMAP_$DATA.r_purifier_ec,
    },
    .l_purifier_ec = {
        .value = 0,
        .waiter_list_head = (ec_$eventcount_waiter_t *)&PMAP_$DATA.l_purifier_ec,
        .waiter_list_tail = (ec_$eventcount_waiter_t *)&PMAP_$DATA.l_purifier_ec,
    },
    .mount_lock = { 0, &PMAP_$DATA.mount_lock, &PMAP_$DATA.mount_lock, 0, -1 },
    .scan_fract = 1,
    .mid_thresh = 0,
    .low_thresh = 0,
    .ws_scan_delta = 2,
    .min_ws_interval = 1,
    .max_ws_interval = 8,
    .ws_interval = 5,
    .shutting_down_flag = 0,
    .short_wait_delay = { 0, 5 },
    .ws_random_seed = 0x004D,
    .current_slot = 5,
});

/*
 * ============================================================================
 * PMAP_$SEGMAP - the segment map, 0x00ED5000..0x00EE4BFF
 * ============================================================================
 *
 * Layout in pmap/pmap.h.  Uninitialised in the image (VM_TABLES has no
 * loaded bytes in Ghidra), so zero-filled.  The map places AST_PMAPS first
 * in VM_TABLES, after the AST/AOT tables (0xEC5400, VM_TABLES size F960)
 * and before AREA_$RPMAP_CACHE (0xEE4C00).
 */
MODULE_DATA_DEFINE(pmap_$segmap_t, PMAP_$SEGMAP, 0x00ED5000);

/*
 * ============================================================================
 * In-code status constants
 * ============================================================================
 */

/*
 * PMAP_$FLUSH's crash status.  A literal inside the PMAP_ code segment;
 * image bytes at 0x00E13A14: 00 05 00 03.
 */
status_$t status_$t_00e13a14 = 0x00050003;

/*
 * pmap_$ws_scan_callback's crash status.  Image bytes at 0x00E145EC:
 * 00 05 00 10.
 */
status_$t status_$t_00e145ec = 0x00050010;
