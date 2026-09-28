/*
 * MMAP_$INIT - Build the free page pool from the boot-time memory probe
 *
 * Original address: 0x00E3193E (578 bytes; `E3193E MMAP_$INIT` in the SAU2
 * map, first symbol of the `I E3190C MMAP_UNWIRED size = 278` segment).
 * Single caller: OS_$INIT at 0x00E3386E, which pushes `pea (A5)` with
 * A5 = its 0xE351F4 boot table.
 *
 * Frame (0x00E3193E-0x00E31942): `link.w A6,#-0x3C`, D2-D6/A2-A4 saved.
 * A5 is NOT loaded - every MMAP_ cell is addressed absolutely.  One
 * argument: (0x8,A6) a pointer to a table of 56 longwords, one per
 * 0x400-byte page of the mmape_t array from vpn 0x200 up (64 entries per
 * page): MMAP_$INIT seeds every entry with 0xFFF, zeroes the entry of each
 * block that holds at least one real page, and finally replaces every entry
 * that is still non-zero - the blocks with NO real page - with the physical
 * page number of that block of the mmape_t array (`tst.l / beq' at
 * 0x00E31B10 skips the zeroed ones).
 *
 * The memory probe left its result in the FIRST WORD of every mmape_t
 * entry (0xEB2800 + vpn*0x10, the wire_count/seg_offset pair): bit 15 =
 * physical memory present, bit 14 = pageable.  Both `move.w
 * (-0x2000,A2),D0w' reads (0x00E319E8, 0x00E31A5C) are of that word, with
 * A2 = 0xEB6800 + (vpn-0x200)*0x10.
 *
 * 0x00E31946-0x00E3195A  MMAP_$WS_OWNER[1..63] = 0 (`dbf' on 0x3E from
 *                        0xE23CAA), then MMAP_$WS_OWNER[0] = 7
 * 0x00E31962-0x00E31996  the 70 MMAP_$WSL headers (`dbf' on 0x45):
 *                        flags &= 7, page_count = scan_pos = 0,
 *                        max_pages = 0x1000, pri_timestamp = ws_timestamp
 *                        = 0, owner = 0, ws_floor = 0.  field_14 (+0x14)
 *                        is NOT touched.
 * 0x00E3199A-0x00E319B2  table[0..55] = 0xFFF (`dbf' on 0x37)
 * 0x00E319B6-0x00E319D6  range_count (D4) = 0, in_range (-0x28,A6) =
 *                        false, free_count (D3) = 0, 0xE00 pages from vpn
 *                        (D2) 0x200 (`dbf' on 0xDFF), A3/A4 = DUMP_$ADDRS
 *                        (0xE007EC)
 * 0x00E319DE-0x00E31AF8  per page:
 *   0x00E319E2            wsl_index = 5 (wired pool) to start with
 *   0x00E319E8-0x00E319F4 present when bit 14 or bit 15 of word 0 is set
 *   0x00E319F8-0x00E31A54 present: MMAP_$REAL_PAGES++; when not already in
 *                         a range: in_range = true, range_count++, A3 += 8,
 *                         range_count > 2 (`ble' skips) crashes with the
 *                         0x00E31B80 cell, DUMP_$ADDRS[range_count-1].start
 *                         = vpn << 10; always DUMP_$ADDRS[range_count-1]
 *                         .end = vpn << 10; table[(vpn-0x200) / 64] = 0
 *                         (the signed `lsl.l #4 / +0x3FF / asr.l #8 /
 *                         asr.l #2' idiom)
 *   0x00E31A56            absent: in_range = false
 *   0x00E31A5C-0x00E31A64 bit 14 clear -> wire_count = 1 (0x00E31AEA)
 *   0x00E31A68-0x00E31AE4 pageable: MMAP_$PAGEABLE_PAGES++; MMAP_$LPPN =
 *                         min, MMAP_$HPPN = max (unsigned); flags1 |= 0x80
 *                         (IN_WSL); wsl_index = 0; first page links to
 *                         itself and becomes MMAP_$WSL[0].head_vpn
 *                         (0xE232BC), later pages are spliced between the
 *                         head and its next_vpn; free_count++; wire_count = 0
 * 0x00E31AFC             MMAP_$WSL[0].page_count (0xE232B4) = free_count
 * 0x00E31B02-0x00E31B32  for i = 1..56 (`dbf' on 0x37): table[i-1] != 0
 *                        (still 0xFFF, no real page in the block)
 *                        -> table[i-1] = mmu_$vtop_or_crash(0xEB4800 +
 *                        (i-1)*0x400), the module-local helper at
 *                        0x00E3190C
 * 0x00E31B36-0x00E31B52  range_count iterations: DUMP_$ADDRS[i].start &=
 *                        0xFFF80000 (`andi.l #-0x80000')
 * 0x00E31B56-0x00E31B66  `bset.b #0xf,(0xE23388)': MMAP_$WSL[6].flags |=
 *                        0x80; `bset.b #0xf,(0xE23364)': MMAP_$WSL[5].flags
 *                        |= 0x80; `bset.b #0xd,(0xE23388)': MMAP_$WSL[6]
 *                        .flags |= 0x20 (bit 13 of a byte operand is bit 5)
 * 0x00E31B6E-0x00E31B70  MMAP_$WSL[6].max_pages (0xE23398) = 100
 */

#include "mmap/mmap_internal.h"
#include "mmu/mmu.h"
#include "misc/misc.h"

/*
 * 0x00E31A12: pea (0x16c,PC) -> 0x00E31B80, jsr CRASH_SYSTEM at 0x00E31A16.
 * Image bytes 00 06 00 07 = "examined max" in the SR10.2 status-code
 * database; only this routine references the cell.
 */
#define status_$mmap_examined_max 0x00060007
static const status_$t mmap_$examined_max_00e31b80 = status_$mmap_examined_max;

/* The probe scans vpn 0x200 .. 0xFFF: 0xE00 pages, 56 blocks of 64. */
#define MMAP_INIT_FIRST_VPN   0x200
#define MMAP_INIT_PAGE_COUNT  0xE00
#define MMAP_INIT_BLOCKS      56
#define MMAP_INIT_PROBE_PRESENT  0x8000  /* bit 15 of the mmape_t word 0 */
#define MMAP_INIT_PROBE_PAGEABLE 0x4000  /* bit 14 of the mmape_t word 0 */

void MMAP_$INIT(void *param)
{
    uint32_t *table = (uint32_t *)param;   /* A2 at 0x00E3199A */
    ws_hdr_t *free_pool = &MMAP_$WSL[MMAP_WSL_POOL_FREE];
    int16_t range_count;   /* D4 */
    boolean in_range;      /* (-0x28,A6) */
    uint32_t free_count;   /* D3 */
    uint32_t vpn;          /* D2 */
    uint16_t i;
    uint32_t k;

    /* 0x00E31946-0x00E3195A */
    for (i = 1; i < MMAP_WS_OWNER_SLOTS; i++) {
        MMAP_$WS_OWNER[i] = 0;
    }
    MMAP_$WS_OWNER[0] = 7;

    /* 0x00E31962-0x00E31996 */
    for (i = 0; i < MMAP_WSL_SLOTS; i++) {
        ws_hdr_t *wsl = &MMAP_$WSL[i];

        wsl->flags &= 0x07;                                     /* 0x00E3196E */
        wsl->page_count = 0;                                    /* 0x00E31972 */
        wsl->scan_pos = 0;                                      /* 0x00E31976 */
        wsl->max_pages = 0x1000;                                /* 0x00E3197A */
        wsl->pri_timestamp = 0;                                 /* 0x00E31982 */
        wsl->ws_timestamp = 0;                                  /* 0x00E31986 */
        wsl->owner = 0;                                         /* 0x00E3198A */
        wsl->ws_floor = 0;                                      /* 0x00E3198E */
    }

    /* 0x00E3199A-0x00E319B2 */
    for (i = 0; i < MMAP_INIT_BLOCKS; i++) {
        table[i] = 0xFFF;
    }

    range_count = 0;                                            /* 0x00E319B6 */
    in_range = false;                                           /* 0x00E319B8 */
    free_count = 0;                                             /* 0x00E319BC */

    /* 0x00E319DE-0x00E31AF8: 0xE00 pages from vpn 0x200 */
    vpn = MMAP_INIT_FIRST_VPN;
    for (k = 0; k < MMAP_INIT_PAGE_COUNT; k++, vpn++) {
        mmape_t *page = MMAPE_FOR_VPN(vpn);
        uint16_t probe;

        page->wsl_index = MMAP_WSL_POOL_WIRED;                  /* 0x00E319E2 */

        /* 0x00E319E8: the big-endian word at mmape_t + 0 */
        probe = (uint16_t)(((uint16_t)page->wire_count << 8) | page->seg_offset);

        if ((probe & MMAP_INIT_PROBE_PAGEABLE)                  /* 0x00E319EC */
            || (probe & MMAP_INIT_PROBE_PRESENT)) {             /* 0x00E319F2 */
            int32_t block;

            MMAP_$REAL_PAGES++;                                 /* 0x00E319F8 */

            if (in_range >= 0) {                                /* 0x00E319FE */
                in_range = true;                                /* 0x00E31A04 */
                range_count++;                                  /* 0x00E31A08 */
                if (range_count > DUMP_ADDRS_RANGES) {          /* 0x00E31A0C */
                    CRASH_SYSTEM(&mmap_$examined_max_00e31b80); /* 0x00E31A16 */
                }
                DUMP_$ADDRS[range_count - 1].start = vpn << 10; /* 0x00E31A24 */
            }
            DUMP_$ADDRS[range_count - 1].end = vpn << 10;       /* 0x00E31A2E */

            /* 0x00E31A32-0x00E31A50: (vpn - 0x200) / 64, signed */
            block = (int32_t)((vpn - MMAP_INIT_FIRST_VPN) << 4);
            if (block < 0) {
                block += 0x3FF;
            }
            block >>= 8;
            block >>= 2;
            table[(uint16_t)block] = 0;                         /* 0x00E31A50 */
        } else {
            in_range = false;                                   /* 0x00E31A56 */
        }

        /* 0x00E31A5C: word 0 re-read */
        probe = (uint16_t)(((uint16_t)page->wire_count << 8) | page->seg_offset);

        if (probe & MMAP_INIT_PROBE_PAGEABLE) {                 /* 0x00E31A60 */
            MMAP_$PAGEABLE_PAGES++;                             /* 0x00E31A68 */

            if (vpn < MMAP_$LPPN) {                             /* 0x00E31A6E */
                MMAP_$LPPN = vpn;                               /* 0x00E31A76 */
            }
            if (vpn > MMAP_$HPPN) {                             /* 0x00E31A7C */
                MMAP_$HPPN = vpn;                               /* 0x00E31A84 */
            }

            page->flags1 |= MMAPE_FLAG1_IN_WSL;                 /* 0x00E31A8C */
            page->wsl_index = MMAP_WSL_POOL_FREE;               /* 0x00E31A92 */

            if (free_count == 0) {                              /* 0x00E31A96 */
                page->prev_vpn = (uint16_t)vpn;                 /* 0x00E31A9A */
                page->next_vpn = (uint16_t)vpn;                 /* 0x00E31A9E */
                free_pool->head_vpn = vpn;                      /* 0x00E31AA2 */
            } else {
                uint32_t head = free_pool->head_vpn;            /* 0x00E31AAA */
                mmape_t *head_page = MMAPE_FOR_VPN(head);
                uint16_t tail = head_page->next_vpn;            /* 0x00E31AC0 */

                page->prev_vpn = (uint16_t)head;                /* 0x00E31AC4 */
                page->next_vpn = tail;                          /* 0x00E31AC8 */
                head_page->next_vpn = (uint16_t)vpn;            /* 0x00E31ACC */
                MMAPE_FOR_VPN(tail)->prev_vpn = (uint16_t)vpn;  /* 0x00E31ADE */
            }

            free_count++;                                       /* 0x00E31AE2 */
            page->wire_count = 0;                               /* 0x00E31AE4 */
        } else {
            page->wire_count = 1;                               /* 0x00E31AEA */
        }
    }

    free_pool->page_count = free_count;                         /* 0x00E31AFC */

    /* 0x00E31B02-0x00E31B32: i = 1..56 */
    for (i = 1; i <= MMAP_INIT_BLOCKS; i++) {
        if (table[i - 1] != 0) {                                /* 0x00E31B10 */
            /* 0xEB4800 + (i-1)*0x400 = &mmape[0x200 + (i-1)*64] */
            table[i - 1] = mmu_$vtop_or_crash(
                ARCH_PTR_TO_VA(&MMAPE_BASE[MMAP_INIT_FIRST_VPN + (i - 1) * 64]));
        }
    }

    /* 0x00E31B36-0x00E31B52 */
    if (range_count != 0) {
        for (i = 0; i < (uint16_t)range_count; i++) {
            DUMP_$ADDRS[i].start &= 0xFFF80000u;                /* 0x00E31B48 */
        }
    }

    MMAP_$WSL[6].flags |= WSL_FLAG_IN_USE;                      /* 0x00E31B56 */
    MMAP_$WSL[MMAP_WSL_POOL_WIRED].flags |= WSL_FLAG_IN_USE;    /* 0x00E31B5E */
    MMAP_$WSL[6].flags |= 0x20;                                 /* 0x00E31B66 */

    MMAP_$WSL[6].max_pages = 100;                               /* 0x00E31B70 */
}
