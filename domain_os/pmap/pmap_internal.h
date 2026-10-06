/*
 * pmap/pmap_internal.h - Internal PMAP Definitions
 *
 * Contains internal functions, data, and types used only within
 * the pmap (physical map) subsystem. External consumers should use pmap/pmap.h.
 */

#ifndef PMAP_INTERNAL_H
#define PMAP_INTERNAL_H

#include "pmap/pmap.h"
#include "uid/uid.h"   /* UID_$NIL */
#include "mmap/mmap.h"
#include "mmu/mmu.h"
#include "time/time.h"
#include "proc1/proc1.h"
#include "network/network.h"
#include "disk/disk.h"
#include "log/log.h"
#include "netlog/netlog.h"
#include "anon/anon.h"

/*
 * ============================================================================
 * Internal Configuration
 * ============================================================================
 */

/*
 * The module's own cells - the thresholds, intervals, counters, eventcounts,
 * timer elements, MOUNT_LOCK, the scan slot and the random seed - are
 * fields of PMAP_$DATA (pmap/pmap.h), the PMAP_ A5 block.
 */

/*
 * ============================================================================
 * Working Set Data (at fixed addresses on m68k)
 * ============================================================================
 */

/*
 * Global page-pool page counts.  These are not standalone variables: each
 * one is MMAP_$WSL[pool].page_count, i.e. 0xE232B0 + pool*0x24 + 4.  New
 * code should use MMAP_WSL[MMAP_WSL_POOL_*].page_count (mmap/mmap.h); the
 * flat MMAP_$WSL_*_CNT names below (the Ghidra labels for those addresses)
 * remain for the pmap/ast files that have not been re-emitted yet.
 */
/* MMAP_$WSL_*_CNT are macros over MMAP_WSL[].page_count; see mmap/mmap.h. */

/*
 * Global working-set scan state.
 *
 * These four addresses are not standalone cells either: they are fields of
 * the wired pool's ws_hdr_t, MMAP_WSL[MMAP_WSL_POOL_WIRED] at
 * 0xE232B0 + 5 * 0x24 = 0xE23364.
 *
 *   0xE23366  = record + 0x02  ws_hdr_t.owner          (word)
 *   0xE2336C  = record + 0x08  ws_hdr_t.scan_pos
 *   0xE2337C  = record + 0x18  ws_hdr_t.pri_timestamp
 *   0xE23380  = record + 0x1C  ws_hdr_t.ws_timestamp
 *
 * pmap_$ws_scan_callback reuses them as the global scan counter and the two
 * scan timestamps.  They are reached by name at that one use site
 * (pmap/ws_scan_callback.c); the DAT_ aliases that used to stand in for them
 * here are gone (bead source-ffh1).
 */

/* The scan slot (+0x7A0) and the random seed (+0x79E) are
 * PMAP_$DATA.current_slot / ws_random_seed (pmap/pmap.h). */

/* Segment map: PMAP_$SEGMAP / PMAP_SEGMAP_ROW in pmap/pmap.h, because
 * mmap_$trim_wsl (0x00E0C850) reads and clears its entries too. */

/*
 * ============================================================================
 * Disk queue block, as used by the purifier write path
 * ============================================================================
 *
 * DISK_$GET_QBLKS hands back a chain of these; DISK_$WRITE_MULTI fills in
 * the per-page status.  Only the fields PMAP_$PURIFIER_L touches are named
 * (0x00E13D28-0x00E13D78 and 0x00E13D96-0x00E13DA0).
 */
typedef struct pmap_qblk_t {
    uint8_t  reserved_00[0x08]; /* 0x00: allocation chain + DISK private */
    struct pmap_qblk_t *next;   /* 0x08: next block of the result chain */
    status_$t status;           /* 0x0C: per-page write status */
    uint8_t  reserved_10[0x04]; /* 0x10 */
    uint32_t vpn;               /* 0x14: page that was written */
    uint8_t  reserved_18[0x24]; /* 0x18 */
    uint32_t log_info;          /* 0x3C: word pair logged by NETLOG_$LOG_IT */
} pmap_qblk_t;

/* Remaining documented offsets (bead source-pewa). */
#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(pmap_qblk_t, reserved_00) == 0x00, "pmap_qblk_t.reserved_00");
_Static_assert(__builtin_offsetof(pmap_qblk_t, reserved_10) == 0x10, "pmap_qblk_t.reserved_10");
_Static_assert(__builtin_offsetof(pmap_qblk_t, reserved_18) == 0x18, "pmap_qblk_t.reserved_18");
#endif

#if defined(ARCH_M68K)
_Static_assert(__builtin_offsetof(pmap_qblk_t, next) == 0x08, "pmap_qblk_t.next");
_Static_assert(__builtin_offsetof(pmap_qblk_t, status) == 0x0C, "pmap_qblk_t.status");
_Static_assert(__builtin_offsetof(pmap_qblk_t, vpn) == 0x14, "pmap_qblk_t.vpn");
_Static_assert(__builtin_offsetof(pmap_qblk_t, log_info) == 0x3C,
               "pmap_qblk_t.log_info");
#endif

/*
 * PMAP_$DATA.short_wait_delay (+0x798, 0xE254DC, image {0, 5}) is the
 * relative delay PMAP_$PURIFIER_L waits between working-set scans.  The
 * working-set scan timers are PMAP_$DATA.ws_timer[1..64], Pascal-indexed by
 * working-set index (pmap/pmap.h); their queue is TIME_$VTQ[index - 1], not
 * a PMAP array (bead source-7sda).  The update and purifier timer elements
 * are PMAP_$DATA.update_timer / purifier_timer.
 */

/*
 * ============================================================================
 * Internal Function Declarations
 * ============================================================================
 */

/*
 * pmap_$flush_write_batch - Batch write dirty pages to disk
 *
 * Nested Pascal procedure from PMAP_$FLUSH, flattened with explicit
 * parameters (originally accessed parent frame via A6 chain).
 * Unlocks lock 14, allocates disk queue blocks, fills them
 * with write requests, calls DISK_$WRITE_MULTI, processes
 * results, and advances AST_$PMAP_IN_TRANS_EC.
 *
 * Parameters:
 *   batch_count_p - Pointer to batch count (cleared to 0 on return)
 *   batch_vpns    - Array of VPNs to write
 *   segmap        - Segment map base pointer
 *   status        - Pointer to caller's status output
 *   aste          - PMAP_$FLUSH's `aste` argument, forwarded to
 *                   pmap_$update_seg_map (the original reaches it through
 *                   the static link in A4)
 *   flags         - PMAP_$FLUSH's `flags` argument, forwarded likewise
 *
 * Original address: 0x00e1360c
 */
void pmap_$flush_write_batch(int16_t *batch_count_p, uint32_t *batch_vpns,
                              uint32_t *segmap, status_$t *status,
                              struct aste_t *aste, uint16_t flags);

/*
 * pmap_$update_seg_map - release or invalidate a page after write-back
 *
 * A nested Pascal procedure of PMAP_$FLUSH.  A1 carries the static link
 * (PMAP_$FLUSH's frame), through which the original reads PMAP_$FLUSH's
 * `aste` argument at (0x08,A1) and the low byte of its `flags` argument at
 * (0x15,A1); both are passed explicitly in this flattening.
 *
 * flags bit 0 clear -> MMAP_$AVAIL(vpn);
 * flags bit 0 set   -> AST_$INVALIDATE_PAGE + optional NETLOG_$LOG_IT.
 *
 * Original address: 0x00e1359c
 * Size: 112 bytes
 */
void pmap_$update_seg_map(struct aste_t *aste, uint16_t flags,
                          uint32_t *segmap_entry, uint32_t vpn,
                          uint16_t page_idx);

/*
 * pmap_$write_page - Write a single page to disk or network
 *
 * Handles writing a page to either local disk or remote network
 * node depending on whether the page belongs to a network-mapped
 * object. Manages checksums, logging, error handling, and
 * page map invalidation on failure.
 *
 * Original address: 0x00e12e5e
 */
void pmap_$write_page(uint32_t vpn, status_$t *status, int8_t sync_flag);

/*
 * pmap_$wait_in_transit - Wait for PMAP in-transit event count
 *
 * Waits for the AST_$PMAP_IN_TRANS_EC event count to advance past
 * its current value. Unlocks PMAP lock while waiting, re-acquires
 * it before returning. Used when pages are in transit to disk.
 *
 * Original address: 0x00e12d38
 * Size: 70 bytes
 */
void pmap_$wait_in_transit(void);

/*
 * pmap_$fill_write_qblks - Fill disk queue blocks with write descriptors
 *
 * Populates a linked list of disk queue blocks with the information
 * needed to write dirty pages to disk. Handles disk address allocation
 * via BAT_$ALLOCATE for pages without existing disk addresses.
 *
 * Original address: 0x00e1327e
 * Size: 798 bytes
 */
void pmap_$fill_write_qblks(int32_t *pages, uint32_t *qblk, int16_t count);

/*
 * pmap_$write_complete - Page write I/O completion handler
 *
 * Frame: (0x8,A6) vpn, a longword physical page number (MMAPE_FOR_VPN,
 * PFT_FOR_PPN); (0xC,A6) the write's status_$t, read and rewritten
 * (`move.l (A0),D3`, `clr.l (A0)`, `bset.b #7,(A0)`).  See
 * pmap/write_complete.c for the blocks.
 *
 * Original address: 0x00e12d84
 * Size: 218 bytes
 */
void pmap_$write_complete(int32_t vpn, status_$t *status_ptr);

/*
 * mmape_t.disk_addr: the block address is the low 22 bits (moved here from
 * pmap/fill_write_qblks.c; pmap_$write_page masks with it too, 0x00E1311A
 * `andi.l #0x3fffff`).  Bit 22 (`btst.b #6,(0xd,A1)` - bit 6 of the byte at
 * mmape+0x0D, the second byte of the big-endian longword) is a flag the
 * write-completion path tests and clears before it marks the page's ASTE
 * dirty (0x00E12DDC-0x00E12DFC); AST_$LOOKUP_OR_CREATE_ASTE plants the same
 * bit in a segment-map entry (ast/lookup_or_create_aste.c).
 */
#define PMAP_DADDR_MASK         0x003FFFFFu
#define PMAP_DADDR_ASTE_DIRTY   0x00400000u

/*
 * PMAP_$INIT_TIMERS - Initialize PMAP purifier and update timers
 *
 * Sets up two periodic timer callbacks in the real-time event queue:
 * - Purifier timer (PMAP_$T_PURIF_CALLBACK): fires every ~29 seconds
 * - Update timer (PMAP_$UPDATE_CALLBACK): fires every ~0.9ms
 *
 * Original address: 0x00e2f880
 * Size: 216 bytes
 */
void PMAP_$INIT_TIMERS(void);

/*
 * ============================================================================
 * Status Codes
 * ============================================================================
 */

/* status_$t_00e13a14 and status_$t_00e145ec declared in pmap.h */

/*
 * ============================================================================
 * External Module Dependencies
 * ============================================================================
 *
 * NETLOG_$OK_TO_LOG   - netlog/netlog.h
 * LOG_$LOGFILE_PTR,
 * LOG_$UPDATE         - log/log.h
 * DISK_$DO_CHKSUM     - disk/disk.h
 * NETWORK_$DISKLESS   - network/network.h
 * ANON_$UID           - anon/anon.h
 */


#endif /* PMAP_INTERNAL_H */
