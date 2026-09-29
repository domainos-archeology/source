/*
 * disk/test/test_map_request.c - unit tests for disk_$map_request (0x00e3cae0)
 *
 * The real disk/map_request.c is #included below and driven against a mock
 * DISK_$DATA area, so the volume descriptors, the address checks, the CHS
 * split and the per-volume request lists are all exercised through the real
 * function.
 */

#include "disk/disk_internal.h"

#include <stdio.h>
#include <string.h>

/* ================================================================
 * Test harness
 * ================================================================ */
static int tests_failed = 0;
static int tests_run = 0;

#define RUN_TEST(name) do {                     \
    tests_run++;                                \
    printf("  Running %s... ", #name);          \
    fflush(stdout);                             \
    if (test_##name() == 0) printf("PASSED\n"); \
} while (0)

#define CHECK(cond) do {                                            \
    if (!(cond)) {                                                  \
        printf("FAILED\n    %s at %s:%d\n", #cond, __FILE__,        \
               __LINE__);                                           \
        tests_failed++;                                             \
        return 1;                                                   \
    }                                                               \
} while (0)

#define CHECK_EQ(expected, actual) do {                             \
    unsigned long e_ = (unsigned long)(expected);                   \
    unsigned long a_ = (unsigned long)(actual);                     \
    if (e_ != a_) {                                                 \
        printf("FAILED\n    %s: expected 0x%lx, got 0x%lx at %s:%d\n", \
               #actual, e_, a_, __FILE__, __LINE__);                \
        tests_failed++;                                             \
        return 1;                                                   \
    }                                                               \
} while (0)

/* ================================================================
 * Globals and the DISK_$DATA override
 * ================================================================ */
static uint8_t mock_disk_data[0xB00];
#undef DISK_VOLUME_BASE
#define DISK_VOLUME_BASE (mock_disk_data)

ml_$exclusion_t ml_$exclusion_t_00e7a274;
MODULE_DATA_DEFINE(pmap_$data_t, PMAP_$DATA, 0x00E24D44);

/* The two Domain Pascal helpers map_request calls (0x00E0ACC0). */
short M$OIU$WLW(long dividend, short divisor)
{
    return (short)((unsigned long)dividend % (unsigned short)divisor);
}

#include "../map_request.c"

/* DISK_$DIAG, DISK_$DO_CHKSUM and the module exclusion lock are cells of the
 * DISK_ module block (disk/disk.h), so the host build provides the block. */
uint8_t DISK_$DATA[DISK_$DATA_SIZE];

/* ================================================================
 * Fixtures
 * ================================================================ */
static uint8_t mock_dev_info[16];
static disk_$vol_map_entry_t map[DISK_VOLUME_MAP_ENTRIES];
/*
 * disk_io_req_t.next is a 32-bit target VA (bead source-wyn9), so the
 * requests have to live in an arena that ARCH_HOST_VA_BASE points at -- see
 * arch/host/arch.h and disk/test/test_rtn_qblks_internal.c.  The array starts
 * one record into the arena so that no live request has VA 0, which is the
 * NULL link.
 */
static uint8_t req_arena[5 * sizeof(disk_io_req_t)];
#define reqs ((disk_io_req_t *)(req_arena + sizeof(disk_io_req_t)))
static status_$t status;

static disk_$volume_t *vol_of(int idx)
{
    return DISK_VOL(idx);
}

static void reset_fixture(uint16_t dev_flags)
{
    disk_$volume_t *v;

    memset(mock_disk_data, 0, sizeof(mock_disk_data));
    memset(mock_dev_info, 0, sizeof(mock_dev_info));
    memset(map, 0, sizeof(map));
    ARCH_HOST_VA_BASE = (uintptr_t)req_arena;
    memset(req_arena, 0, sizeof(req_arena));
    status = 0x0BADF00D;

    *(uint16_t *)(mock_dev_info + 8) = dev_flags;

    v = vol_of(1);
    v->dev_info = mock_dev_info;
    v->lv_start = 0;
    v->addr_start = 100000;
    v->addr_end = 200000;
    v->sec_per_track = 12;
    v->num_heads = 5;
    v->sector_size_code = 0;
    v->blocks_per_cyl = 60;
    v->part_volx[0] = 0; /* not striped */
    v->part_volx[1] = 2; /* the physical volume behind it */
}

/* ================================================================
 * Tests
 * ================================================================ */

/*
 * 0x00E3CB82-0x00E3CBC0: a plain CHS volume.  daddr 1000 with 60 blocks per
 * cylinder and 12 sectors per track is cylinder 16, block 40 in the cylinder,
 * i.e. head 3 sector 4.
 */
static int test_chs_split(void)
{
    reset_fixture(0);
    reqs[0].daddr = 1000;

    disk_$map_request(&reqs[0], 1, 1, map, &status);

    CHECK_EQ(status_$ok, status);
    CHECK_EQ(16, reqs[0].daddr >> 16);          /* cylinder, req+0x04 */
    CHECK_EQ(3, (reqs[0].daddr >> 8) & 0xFF);   /* head,     req+0x06 */
    CHECK_EQ(4, reqs[0].daddr & 0xFF);          /* sector,   req+0x07 */

    /* 0x00E3CB42: the absolute address lands in the block header */
    CHECK_EQ(1000, reqs[0].header[7]);

    /* 0x00E3CB6E: one stripe chunk is the transfer length */
    CHECK_EQ(1, reqs[0].flags);

    /* 0x00E3CC34: entry volx-1 of the map, i.e. index 1 for volume 2 */
    CHECK(map[1].head == &reqs[0]);
    CHECK(map[1].tail == &reqs[0]);
    CHECK(map[0].head == NULL);
    CHECK(reqs[0].next == 0);

    /* 0x00E3CC5E: the internal opcode is OR-ed into the low nibble */
    CHECK_EQ(1, reqs[0].op_flags);
    return 0;
}

/* 0x00E3CB3E: a logical volume's start block is added to every address */
static int test_lv_start_added(void)
{
    reset_fixture(0);
    vol_of(1)->lv_start = 0; /* keep the range check happy */
    vol_of(1)->addr_start = 100000;
    reqs[0].daddr = 1000;
    vol_of(1)->lv_start = 0;

    /* now with a non-zero start */
    vol_of(1)->lv_start = 600;
    disk_$map_request(&reqs[0], 1, 1, map, &status);

    CHECK_EQ(status_$ok, status);
    CHECK_EQ(1600, reqs[0].header[7]);
    CHECK_EQ(1600 / 60, reqs[0].daddr >> 16);
    return 0;
}

/*
 * 0x00E3CB18-0x00E3CB3A: below addr_start is always accepted; at or above it
 * a write is refused, a read is not, and a logical volume is refused either
 * way.
 */
static int test_address_range(void)
{
    /* a write past the data size */
    reset_fixture(0);
    reqs[0].daddr = 100000;
    disk_$map_request(&reqs[0], 1, 2 /* write */, map, &status);
    CHECK_EQ(status_$invalid_disk_address, status);
    CHECK(map[1].head == NULL);

    /* the same address as a read is fine on a physical volume */
    reset_fixture(0);
    reqs[0].daddr = 100000;
    disk_$map_request(&reqs[0], 1, 1 /* read */, map, &status);
    CHECK_EQ(status_$ok, status);
    CHECK(map[1].head == &reqs[0]);

    /* past addr_end is refused for any operation */
    reset_fixture(0);
    reqs[0].daddr = 200000;
    disk_$map_request(&reqs[0], 1, 1, map, &status);
    CHECK_EQ(status_$invalid_disk_address, status);

    /* a logical volume is refused past its data size even for a read */
    reset_fixture(0);
    vol_of(1)->lv_start = 8;
    reqs[0].daddr = 100000;
    disk_$map_request(&reqs[0], 1, 1, map, &status);
    CHECK_EQ(status_$invalid_disk_address, status);

    /* just below is fine */
    reset_fixture(0);
    vol_of(1)->lv_start = 8;
    reqs[0].daddr = 99999;
    disk_$map_request(&reqs[0], 1, 2, map, &status);
    CHECK_EQ(status_$ok, status);
    return 0;
}

/* 0x00E3CBC2: a device with the linear-address bit takes the block number */
static int test_linear_device(void)
{
    reset_fixture(DEV_FLAG_LINEAR_ADDRESS);
    reqs[0].daddr = 1000;

    disk_$map_request(&reqs[0], 1, 1, map, &status);

    CHECK_EQ(status_$ok, status);
    CHECK_EQ(1000, reqs[0].daddr);
    CHECK(map[1].head == &reqs[0]);
    return 0;
}

/* 0x00E3CB4E-0x00E3CB64: striping is refused on such a device */
static int test_linear_device_striped(void)
{
    reset_fixture(DEV_FLAG_LINEAR_ADDRESS);
    vol_of(1)->part_volx[0] = 4;
    reqs[0].daddr = 1000;

    disk_$map_request(&reqs[0], 1, 1, map, &status);

    CHECK_EQ(status_$disk_striping_not_supported, status);
    return 0;
}

/*
 * 0x00E3CBC8-0x00E3CC30: two blocks per chunk across two volumes.
 *
 *   daddr 5 -> chunk offset 1, group 2
 *   group % 60 = 2 blocks into the cylinder -> head 0 sector 2
 *   group / 60 = 0 -> member 0, cylinder 0
 *   part index = 1 + (0 << 1) + 1 = 2
 */
static int test_striped_split(void)
{
    disk_$volume_t *v;

    reset_fixture(0);
    v = vol_of(1);
    v->part_volx[0] = 4; /* any non-zero interleave mode */
    v->part_volx[2] = 3; /* the volume the block lands on */
    v->stripe_blk_mask = 1;
    v->stripe_blk_shift = 1;
    v->stripe_vol_mask = 1;
    v->stripe_vol_shift = 1;
    reqs[0].daddr = 5;

    disk_$map_request(&reqs[0], 1, 1, map, &status);

    CHECK_EQ(status_$ok, status);
    CHECK_EQ(0, reqs[0].daddr >> 16);        /* cylinder */
    CHECK_EQ(0, (reqs[0].daddr >> 8) & 0xFF); /* head */
    CHECK_EQ(2, reqs[0].daddr & 0xFF);        /* sector */
    CHECK_EQ(2, reqs[0].flags);               /* stripe_blk_mask + 1 */
    CHECK(map[2].head == &reqs[0]);           /* volume 3 */
    return 0;
}

/*
 * 0x00E3CC38-0x00E3CC50: a chain of blocks is appended in order, and blocks
 * that land on different volumes go on different lists.
 */
static int test_chain_linking(void)
{
    reset_fixture(0);
    reqs[0].next = ARCH_PTR_TO_VA(&reqs[1]);
    reqs[1].next = ARCH_PTR_TO_VA(&reqs[2]);
    reqs[2].next = 0;
    reqs[0].daddr = 60;
    reqs[1].daddr = 120;
    reqs[2].daddr = 180;

    disk_$map_request(&reqs[0], 1, 1, map, &status);

    CHECK_EQ(status_$ok, status);
    CHECK(map[1].head == &reqs[0]);
    CHECK(map[1].tail == &reqs[2]);
    CHECK(reqs[0].next == ARCH_PTR_TO_VA(&reqs[1]));
    CHECK(reqs[1].next == ARCH_PTR_TO_VA(&reqs[2]));
    CHECK(reqs[2].next == 0);
    CHECK_EQ(1, reqs[0].daddr >> 16);
    CHECK_EQ(2, reqs[1].daddr >> 16);
    CHECK_EQ(3, reqs[2].daddr >> 16);
    return 0;
}

/*
 * 0x00E3CB5E: a failure mid-chain stops the walk, leaving the blocks it had
 * already mapped on the list.
 */
static int test_failure_stops_the_walk(void)
{
    reset_fixture(0);
    reqs[0].next = ARCH_PTR_TO_VA(&reqs[1]);
    reqs[1].next = 0;
    reqs[0].daddr = 60;
    reqs[1].daddr = 200000; /* past addr_end */

    disk_$map_request(&reqs[0], 1, 1, map, &status);

    CHECK_EQ(status_$invalid_disk_address, status);
    CHECK(map[1].head == &reqs[0]);
    CHECK(map[1].tail == &reqs[0]);
    return 0;
}

int main(void)
{
    printf("disk_$map_request (0x00e3cae0) tests\n");

    RUN_TEST(chs_split);
    RUN_TEST(lv_start_added);
    RUN_TEST(address_range);
    RUN_TEST(linear_device);
    RUN_TEST(linear_device_striped);
    RUN_TEST(striped_split);
    RUN_TEST(chain_linking);
    RUN_TEST(failure_stops_the_walk);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
