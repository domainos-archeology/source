/*
 * PMAP - Page Map Management
 *
 * This module provides page map management for Domain/OS.
 * It handles flushing dirty pages to disk, managing page purifier
 * processes, and working set scanning.
 *
 * Key concepts:
 * - Page flushing: Writing modified pages back to disk
 * - Purifier: Background process that cleans dirty pages
 * - Working set: Pages actively used by a process
 *
 * The PMAP layer works closely with MMAP (Memory Map) and AST
 * (Active Segment Table) to manage the page cache.
 *
 * Memory layout (SAU2 image addresses; the module data blocks are linked in
 * the map's order, not at these addresses - docs/design-per-process-data.md):
 * - PMAP_$DATA, the PMAP_ module block (A5): 0xE24D44, 0x7A4 bytes
 * - PMAP_$SEGMAP, the segment map (map AST_PMAPS): 0xED5000, 0xFC00 bytes
 * - Hardware page table: 0xFFB802
 * - PMAPE base: 0xEB2800
 *
 * Module data blocks PMAP_$DATA / PMAP_$SEGMAP: Claude Opus 5.5 (source-iq58).
 */

#ifndef PMAP_H
#define PMAP_H

#include "base/base.h"
#include "arch/arch.h"
#include "ec/ec.h"
#include "ml/ml.h"
#include "time/time.h"   /* time_queue_elem_t, clock_t */
#include "mmap/mmap.h"   /* MMAP_$WSL_*_CNT, MMAP_WSL */

/*
 * Forward declarations
 */
struct aste_t;
struct aote_t;

/*
 * ============================================================================
 * PMAP_$DATA - the PMAP_ module block, 0x00E24D44..0x00E254E7
 * ============================================================================
 *
 * SAU2 map "D E24D44 PMAP_ size = 7A4".  The PMAP routines load
 * "lea (0xe24d44).l,A5" (PMAP_$PURIFIER_L 0x00E13AA4, PMAP_$FLUSH,
 * PMAP_$INIT_WS_SCAN, PMAP_$T_PURIF_CALLBACK, ...; PMAP_$INIT_TIMERS uses A0),
 * so every cell below is an (off,A5) field.  The map's interior symbols are
 * the exported scalars from +0x740 on (PMAP_$IDLE_INTERVAL 0xE25484 ..
 * PMAP_$SHUTTING_DOWN_FLAG 0xE254DA) and MOUNT_LOCK (0xE254B8, no `$': the
 * disk module's mount/dismount lock, which the image keeps here); the cells
 * before +0x740 and after +0x796 have no map names.  The address is the
 * block's image address - the ordering key of tools/gen_layout_ld.py, not
 * where it is linked.
 *
 * The working-set scan timers are a Pascal [1..64] array of 0x1C-byte time
 * queue elements indexed by the process's working-set index:
 * PMAP_$INIT_WS_SCAN computes "lea (0x24,A5,D4w*0x1)" with D4 = index*0x1C
 * (0x00E14626-0x00E14632), so element 1 is at +0x40 (0xE24D84) and the
 * array is declared at its bias slot +0x24 (0xE24D68), 65 elements that end
 * exactly at PMAP_$IDLE_INTERVAL (+0x740).  The stride is two bytes more
 * than time_queue_elem_t.  Element 0 overlays the tail of the purifier timer
 * element (+0x20..+0x39), so the block is a union of one arm per object; no
 * working-set index is 0, so nothing writes through the bias slot (beads
 * source-7sda, source-v4gu).
 *
 * Image contents (`gsk read 0xE24D44 0x7A4`, pmap/pmap_data.c): the three
 * eventcounts and MOUNT_LOCK are self-linked (their waiter-list cells hold
 * their own VAs), MOUNT_LOCK.f5 = -1, IDLE_INTERVAL 0x63C, SCAN_FRACT 1,
 * WS_SCAN_DELTA 2, MIN/MAX/WS_INTERVAL 1/8/5, SHORT_WAIT_DELAY {0, 5},
 * WS_RANDOM_SEED 0x4D, CURRENT_SLOT 5; every other byte zero.
 *
 * Up to +0x740 the block is pointer-free, so those offsets and the timer
 * stride are asserted on every build; the eventcounts and the exclusion
 * lock carry native pointers, so the offsets from +0x750 on and the size
 * are asserted on the target only.
 */
#define PMAP_$DATA_SIZE         0x7A4   /* map: PMAP_ size = 7A4 */
#define PMAP_WS_TIMER_COUNT     65      /* index 0..64: element 0 is the bias
                                         * slot, 1..64 the working-set indices */
#define PMAP_WS_TIMER_STRIDE    0x1C

/*
 * pmap_$ws_timer_t - one working-set scan timer: a time_queue_elem_t in a
 * 0x1C-byte slot.  A union so the stride is 0x1C on the target (element
 * 0x1A, word aligned) and on a host (element padded to 0x1C) alike.
 */
typedef union pmap_$ws_timer_t {
    time_queue_elem_t elem;
    uint8_t           _slot[PMAP_WS_TIMER_STRIDE];
} pmap_$ws_timer_t;

typedef struct pmap_$data_t {
    union {
        struct {
            /* +0x000 (0xE24D44): the update timer (PMAP_$INIT_TIMERS
             * 0x00E2F880, callback PMAP_$UPDATE_CALLBACK) */
            time_queue_elem_t update_timer;
        };
        struct {
            uint8_t  _purifier_bias[0x020];
            /* +0x020 (0xE24D64): the purifier timer (PMAP_$INIT_TIMERS,
             * callback PMAP_$T_PURIF_CALLBACK) */
            time_queue_elem_t purifier_timer;
        };
        struct {
            uint8_t  _ws_timer_bias[0x024];
            /* +0x024 (0xE24D68), Pascal [1..64] at +0x040: the working-set
             * scan timers (PMAP_$INIT_WS_SCAN) */
            pmap_$ws_timer_t ws_timer[PMAP_WS_TIMER_COUNT];

            uint32_t idle_interval;     /* +0x740 PMAP_$IDLE_INTERVAL */
            uint32_t t_pur_scans;       /* +0x744 PMAP_$T_PUR_SCANS */
            uint32_t pur_r_cnt;         /* +0x748 PMAP_$PUR_R_CNT */
            uint32_t pur_l_cnt;         /* +0x74C PMAP_$PUR_L_CNT */
            ec_$eventcount_t pages_ec;       /* +0x750 PMAP_$PAGES_EC */
            ec_$eventcount_t r_purifier_ec;  /* +0x75C PMAP_$R_PURIFIER_EC */
            ec_$eventcount_t l_purifier_ec;  /* +0x768 PMAP_$L_PURIFIER_EC */
            /* +0x774 MOUNT_LOCK: guards the disk mount/dismount/LV-assign
             * paths (disk/lv_mount.c, dismount.c, ...) */
            ml_$exclusion_t mount_lock;
            uint8_t  _unknown_786[2];   /* +0x786: not referenced */
            uint16_t scan_fract;        /* +0x788 PMAP_$SCAN_FRACT */
            uint16_t mid_thresh;        /* +0x78A PMAP_$MID_THRESH */
            uint16_t low_thresh;        /* +0x78C PMAP_$LOW_THRESH */
            uint16_t ws_scan_delta;     /* +0x78E PMAP_$WS_SCAN_DELTA */
            uint16_t min_ws_interval;   /* +0x790 PMAP_$MIN_WS_INTERVAL */
            uint16_t max_ws_interval;   /* +0x792 PMAP_$MAX_WS_INTERVAL */
            uint16_t ws_interval;       /* +0x794 PMAP_$WS_INTERVAL */
            int8_t   shutting_down_flag;/* +0x796 PMAP_$SHUTTING_DOWN_FLAG */
            uint8_t  _unknown_797;      /* +0x797: not referenced */
            /* +0x798: relative delay PMAP_$PURIFIER_L waits between passes */
            clock_t  short_wait_delay;
            /* +0x79E: the 16-bit LCG state PMAP_$PURIFIER_L uses to pick
             * how much of a working set to steal ("move.w (0x79e,A5),D5w"
             * 0x00E13ED0, "move.w D5w,(0x79e,A5)" 0x00E13EDC) */
            uint16_t ws_random_seed;
            /* +0x7A0: the working-set scan slot pmap_$t_purif_callback wraps
             * from 0x45 back to 5 ("cmpi.w #0x45,(0x7a0,A5)" 0x00E143DA) */
            uint16_t current_slot;
            uint8_t  _unknown_7a2[2];   /* +0x7A2: not referenced */
        };
    };
} pmap_$data_t;

/* Pointer-free part: every build. */
_Static_assert(offsetof(pmap_$data_t, update_timer) == 0x000, "update_timer (0xE24D44)");
_Static_assert(offsetof(pmap_$data_t, purifier_timer) == 0x020, "purifier_timer (0xE24D64)");
_Static_assert(offsetof(pmap_$data_t, ws_timer) == 0x024, "ws_timer bias base (0xE24D68)");
_Static_assert(sizeof(pmap_$ws_timer_t) == PMAP_WS_TIMER_STRIDE, "ws_timer stride (D4 = index*0x1C)");
_Static_assert(offsetof(pmap_$data_t, ws_timer[1]) == 0x040, "ws_timer[1]");
_Static_assert(offsetof(pmap_$data_t, ws_timer[PMAP_WS_TIMER_COUNT]) == 0x740,
               "ws_timer[64] ends at PMAP_$IDLE_INTERVAL");
_Static_assert(offsetof(pmap_$data_t, idle_interval) == 0x740, "PMAP_$IDLE_INTERVAL 0xE25484");
_Static_assert(offsetof(pmap_$data_t, t_pur_scans) == 0x744, "PMAP_$T_PUR_SCANS 0xE25488");
_Static_assert(offsetof(pmap_$data_t, pur_r_cnt) == 0x748, "PMAP_$PUR_R_CNT 0xE2548C");
_Static_assert(offsetof(pmap_$data_t, pur_l_cnt) == 0x74C, "PMAP_$PUR_L_CNT 0xE25490");
_Static_assert(offsetof(pmap_$data_t, pages_ec) == 0x750, "PMAP_$PAGES_EC 0xE25494");
/* Eventcounts and the exclusion lock hold native pointers: target only. */
#if defined(ARCH_M68K)
_Static_assert(sizeof(pmap_$data_t) == PMAP_$DATA_SIZE, "PMAP_ block: map size 0x7A4");
_Static_assert(offsetof(pmap_$data_t, r_purifier_ec) == 0x75C, "PMAP_$R_PURIFIER_EC 0xE254A0");
_Static_assert(offsetof(pmap_$data_t, l_purifier_ec) == 0x768, "PMAP_$L_PURIFIER_EC 0xE254AC");
_Static_assert(offsetof(pmap_$data_t, mount_lock) == 0x774, "MOUNT_LOCK 0xE254B8");
_Static_assert(offsetof(pmap_$data_t, scan_fract) == 0x788, "PMAP_$SCAN_FRACT 0xE254CC");
_Static_assert(offsetof(pmap_$data_t, mid_thresh) == 0x78A, "PMAP_$MID_THRESH 0xE254CE");
_Static_assert(offsetof(pmap_$data_t, low_thresh) == 0x78C, "PMAP_$LOW_THRESH 0xE254D0");
_Static_assert(offsetof(pmap_$data_t, ws_scan_delta) == 0x78E, "PMAP_$WS_SCAN_DELTA 0xE254D2");
_Static_assert(offsetof(pmap_$data_t, min_ws_interval) == 0x790, "PMAP_$MIN_WS_INTERVAL 0xE254D4");
_Static_assert(offsetof(pmap_$data_t, max_ws_interval) == 0x792, "PMAP_$MAX_WS_INTERVAL 0xE254D6");
_Static_assert(offsetof(pmap_$data_t, ws_interval) == 0x794, "PMAP_$WS_INTERVAL 0xE254D8");
_Static_assert(offsetof(pmap_$data_t, shutting_down_flag) == 0x796, "PMAP_$SHUTTING_DOWN_FLAG 0xE254DA");
_Static_assert(offsetof(pmap_$data_t, short_wait_delay) == 0x798, "short_wait_delay 0xE254DC");
_Static_assert(offsetof(pmap_$data_t, ws_random_seed) == 0x79E, "ws_random_seed 0xE254E2");
_Static_assert(offsetof(pmap_$data_t, current_slot) == 0x7A0, "current_slot 0xE254E4");
#endif

MODULE_DATA_DECLARE(pmap_$data_t, PMAP_$DATA, 0x00E24D44);

/*
 * External references to other modules
 */
/* MMAP_$WSL_*_CNT are macros over MMAP_WSL[].page_count; see mmap/mmap.h. */

/*
 * Lock IDs
 */
#define PMAP_LOCK_ID                    0x14    /* PMAP lock */
#define PROC_LOCK_ID                    0x0D    /* Process lock */

/* LOG_$UPDATE, LOG_$LOGFILE_PTR - declared in log/log.h */

/*
 * NETLOG functions - declared in ast/ast.h
 * Note: signature is NETLOG_$LOG_IT(type, uid, seg, page, ppn, count, param7, param8)
 */

/*
 * Internal helper functions.
 *
 * pmap_$write_page, pmap_$update_seg_map and pmap_$flush_write_batch are
 * PMAP-private (the latter two are nested procedures of PMAP_$FLUSH);
 * their prototypes live in pmap/pmap_internal.h.
 */

/*
 * Error strings
 */
extern status_$t status_$t_00e13a14;
extern status_$t status_$t_00e145ec;

/*
 * Function prototypes - Page flushing
 */
int16_t PMAP_$FLUSH(struct aste_t *aste, uint32_t *segmap, uint16_t start_page,
                    int16_t count, uint16_t flags, status_$t *status);

/*
 * Function prototypes - Purifier control
 */
void PMAP_$WAKE_PURIFIER(int8_t wait);

/*
 * Function prototypes - Purifier processes (run as background tasks)
 */
void PMAP_$PURIFIER_L(void);
void PMAP_$PURIFIER_R(void);

/*
 * Function prototypes - Callbacks
 */
void PMAP_$UPDATE_CALLBACK(void);
void PMAP_$T_PURIF_CALLBACK(void);
/* The time queue hands its callbacks the address of a longword holding the
 * element address (time_$callback_arg_t); see pmap/ws_scan_callback.c. */
void PMAP_$WS_SCAN_CALLBACK(void *arg);

/*
 * Function prototypes - Working set management
 */
void PMAP_$INIT_WS_SCAN(uint16_t index, int16_t param);
void PMAP_$PURGE_WS(int16_t index, int16_t flags);

/*
 * ============================================================================
 * PMAP_$SEGMAP - the segment map, 0x00ED5000..0x00EE4BFF
 * ============================================================================
 *
 * One 4-byte entry per page of a segment, 32 pages (0x80 bytes) per
 * segment.  The SAU2 map names the table AST_PMAPS (0xED5000, first symbol
 * of the "D98 ED5000 VM_TABLES loaded at 1E331A, size = 7AC00" region) and
 * its end AST_PMAPS_END (0xEE4C00, = AREA_$RPMAP_CACHE): 0xFC00 bytes, one
 * row for each of the AST_MAX_ASTE (0x1F8 = 504) segment slots.  The region
 * is uninitialised in the image (Ghidra has no bytes there), so the block is
 * zero-filled.  AST_PMAPS has no `$', so the block keeps its tree name.
 *
 * The segment index is Pascal 1-based and the compiler folds the bias into
 * the displacement: PMAP_$PURIFIER_L reaches an entry with
 *   movea.l #0xed5000,A1 ; lea (0,A1,seg*0x80),A2 ; lea (0,A2,page*4),A0
 *   ... bset.b #7,(-0x80,A0)           (0x00E13BEA-0x00E13C32)
 * and pmap_$fill_write_qblks with "lea (-0x80,A0,D2w*1)" (0x00E13378), so
 * row `seg' is at 0xED5000 + (seg - 1)*0x80.  Row 0 would be 0xED4F80, in
 * the gap before AST_PMAPS that no map segment covers, so the bias cannot
 * be folded into the block's declaration; like P2_INFO_ENTRY
 * (docs/design-per-process-data.md, section 3) the table is declared from
 * row 1 and PMAP_SEGMAP_ROW(seg) applies the bias once.
 */
typedef struct pmap_segmap_entry_t {
    uint8_t  flags;         /* 0x00: bit 7 = write in progress */
    uint8_t  reserved[3];   /* 0x01: rest of the 4-byte entry */
} pmap_segmap_entry_t;

/* Remaining documented offsets (bead source-pewa). */
_Static_assert(__builtin_offsetof(pmap_segmap_entry_t, flags) == 0x00, "pmap_segmap_entry_t.flags");
_Static_assert(__builtin_offsetof(pmap_segmap_entry_t, reserved) == 0x01, "pmap_segmap_entry_t.reserved");

#define PMAP_SEGMAP_WRITING     0x80    /* bset.b #7 at 0x00E13C32 */
/*
 * Bit 5 of the flags byte (bit 13 of the entry's first word): the page is
 * installed in the MMU.  mmap_$trim_wsl tests it with `btst.l #0xd' on the
 * word (0x00E0C858) and clears it with `bclr.b #5' before MMU_$REMOVE
 * (0x00E0C85E-0x00E0C866).
 */
#define PMAP_SEGMAP_INSTALLED   0x20
#define PMAP_SEGMAP_PAGES_PER_SEG 32    /* 0x80 bytes / 4 bytes per entry */
#define PMAP_SEGMAP_ROWS        0x1F8   /* = AST_MAX_ASTE (ast/ast.h) */
#define PMAP_$SEGMAP_SIZE       0xFC00  /* AST_PMAPS_END - AST_PMAPS */

typedef pmap_segmap_entry_t pmap_segmap_row_t[PMAP_SEGMAP_PAGES_PER_SEG];

typedef struct pmap_$segmap_t {
    pmap_segmap_row_t row[PMAP_SEGMAP_ROWS];    /* row[0] = segment 1 */
} pmap_$segmap_t;

_Static_assert(sizeof(pmap_segmap_entry_t) == 4, "segmap entry is 4 bytes");
_Static_assert(sizeof(pmap_segmap_row_t) == 0x80, "segmap row is 0x80 bytes (lsl.l #7)");
_Static_assert(sizeof(pmap_$segmap_t) == PMAP_$SEGMAP_SIZE,
               "AST_PMAPS 0xED5000..AST_PMAPS_END 0xEE4C00");

MODULE_DATA_DECLARE(pmap_$segmap_t, PMAP_$SEGMAP, 0x00ED5000);

/*
 * PMAP_SEGMAP_ROW(seg) - the row of 1-based segment slot `seg' (an lvalue
 * of type pmap_segmap_row_t): PMAP_SEGMAP_ROW(seg)[page] is the image's
 * 0xED5000 + (seg - 1)*0x80 + page*4.
 */
#define PMAP_SEGMAP_ROW(seg)    (PMAP_$SEGMAP.row[(seg) - 1])

/*
 * The segment-map entry as the LONGWORD the PMAP code tests it as
 * (pmap_segmap_entry_t above is its first byte).  PMAP_$FLUSH reads the
 * high word with `tst.w (A2)` / `btst.l #0xe` / `btst.l #0xd`, the low word
 * (the VPN) with `move.w (0x2,A2)`, and sets/clears bits 7 and 5 of the
 * first byte (`bset.b #7,(A2)`, `bclr.b #5,(A2)`).  Appended 2026-09-27.
 */
#define PMAP_SEGMAP_L_WRITING    0x80000000u  /* first byte bit 7 = PMAP_SEGMAP_WRITING */
#define PMAP_SEGMAP_L_VALID      0x40000000u  /* high word bit 14: entry holds a VPN */
#define PMAP_SEGMAP_L_INSTALLED  0x20000000u  /* first byte bit 5 = PMAP_SEGMAP_INSTALLED */
#define PMAP_SEGMAP_L_VPN_MASK   0x0000FFFFu  /* low word: the VPN */

#endif /* PMAP_H */
