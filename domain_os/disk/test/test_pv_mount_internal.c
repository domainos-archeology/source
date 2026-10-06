/*
 * disk/test/test_pv_mount_internal.c - unit tests for DISK_$PV_MOUNT_INTERNAL
 * (0x00E6C2BC) and its nested slot finder (0x00E6C116).  The label reader,
 * the driver init and the release/shutdown callees are mocked.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

static void reset_state(void);

#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); \
    reset_state(); \
    test_##name(); \
    printf("PASSED\n"); \
    tests_passed++; \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    if ((unsigned long)(expected) != (unsigned long)(actual)) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               (unsigned long)(expected), (unsigned long)(actual), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "disk/disk_internal.h"
#include "math/math.h"
#include "audit/audit.h"
#include "io/io.h"

uint8_t DISK_$DATA[DISK_$DATA_SIZE];
MODULE_DATA_DEFINE_INIT(disk_$mount_data_t, DISK_$MOUNT_DATA, 0x00E826C4, {
    .vol_template = { .addr_start = 1, .sec_per_track = 1, .num_heads = 2,
                      .blocks_per_cyl = 4, .bat_step = 1, .num_parts = 1 },
    .log2_table = { 0, 0, 1, 0, 2, 0, 0, 0, 3, 0 },
    .pow2_table = { 1, 2, 4, 0 },
});
MODULE_DATA_DEFINE(pmap_$data_t, PMAP_$DATA, 0x00E24D44);
uint16_t PROC1_$CURRENT = 7;
int8_t AUDIT_$ENABLED = 0;
uid_t AUDIT_$ASSIGN_DISK_EU = { 0x0004000C, 0 };
uid_t AUDIT_$MOUNT_DISK_EU = { 0x0004000B, 0 };

static char log_buf[256];
static void note(const char *s) { strcat(log_buf, s); }
static disk_device_entry_t dev_a, dev_b;
static disk_device_entry_t *drte_ret;
static status_$t dinit_status[4];
static int n_dinit;
static disk_$pv_label_t labels[3];
static status_$t label_status[3];
static int n_label;
static int16_t label_vol[3];
static int released;
static int16_t invalidated[8]; static int n_inval;
static uid_t *audit_eu; static uint16_t audit_st; static uint16_t audit_rec[3];

void ML_$EXCLUSION_START(ml_$exclusion_t *e)
{ if (e != &PMAP_$DATA.mount_lock) note("!"); note("<"); }
void ML_$EXCLUSION_STOP(ml_$exclusion_t *e)
{ if (e != &PMAP_$DATA.mount_lock) note("!"); note(">"); }
void PROC2_$SET_CLEANUP(uint16_t bit_num) { if (bit_num != 5) note("!"); note("C"); }
void disk_$diskless_init(void) { note("W"); }
disk_device_entry_t *DISK_$GET_DRTE(uint16_t *ctype_ptr, uint16_t *cnum_ptr)
{
    (void)ctype_ptr;
    note("G");
    if (drte_ret == NULL) return NULL;
    return (*cnum_ptr == 6) ? &dev_b : drte_ret;
}
status_$t DISK_$MNT_DINIT(uint16_t unit, void **dev_ptr, void *num_blocks_ptr,
                          void *sec_per_track_ptr, void *num_heads_ptr,
                          void *pvlabel_info, void *flags_ptr)
{
    (void)unit; (void)dev_ptr; (void)pvlabel_info;
    note("D");
    *(uint32_t *)num_blocks_ptr = 0x1000;
    *(uint16_t *)sec_per_track_ptr = 0x12;
    *(uint16_t *)num_heads_ptr = 3;
    *(uint16_t *)flags_ptr = 0x0042;
    return dinit_status[n_dinit++];
}
disk_$pv_label_t *disk_$validate_pv_label(int16_t vol_idx, status_$t *status)
{
    note("V");
    label_vol[n_label] = vol_idx;
    *status = label_status[n_label];
    return &labels[n_label++];
}
void DBUF_$SET_BUFF(void *buffer, uint16_t flags, status_$t *status)
{ (void)buffer; if (flags != 8) note("!"); note("R"); released++; *status = 0; }
void DISK_$INVALIDATE(uint16_t vol_idx) { note("I"); invalidated[n_inval++] = (int16_t)vol_idx; }
void DISK_$SHUTDOWN(disk_device_entry_t *dev_info, uint16_t unit)
{ (void)dev_info; (void)unit; note("S"); }
ulong M$MIU$LLW(ulong multiplicand, ushort multiplier) { return multiplicand * multiplier; }
void AUDIT_$LOG_EVENT(uid_t *event_uid, uint16_t *event_flags,
                      status_$t *status, char *data, const uint16_t *data_len)
{
    (void)status;
    note("A");
    audit_eu = event_uid; audit_st = *event_flags;
    memcpy(audit_rec, data, 6);
    if (*data_len != 6) note("!");
}

#include "../pv_mount_internal.c"

static uint16_t unit_id;
static uint32_t num_blocks;
static uint16_t spt, heads;
static uint16_t geom[8];
static status_$t st;

static void reset_state(void)
{
    memset(DISK_$DATA, 0, sizeof(DISK_$DATA));
    memset(labels, 0, sizeof(labels));
    memset(dinit_status, 0, sizeof(dinit_status));
    memset(label_status, 0, sizeof(label_status));
    n_dinit = n_label = released = n_inval = 0;
    drte_ret = &dev_a;
    dev_a.unit_count = 0; dev_b.unit_count = 0;
    AUDIT_$ENABLED = 0;
    log_buf[0] = 0;
    unit_id = 0; num_blocks = 0; spt = heads = 0;
    memset(geom, 0, sizeof(geom));
    st = 0x5555;
    labels[0].num_parts = 1;
    labels[0].part_num = 1;
}

static void test_assign_single_volume(void)
{
    disk_$volume_t *v;
    int16_t r = DISK_$PV_MOUNT_INTERNAL(1, 0, 2, 3, &unit_id, &num_blocks,
                                        &spt, &heads, geom, &st);
    ASSERT_EQ(0, strcmp(log_buf, "<GCWDVR>"));
    ASSERT_EQ(0, st);
    ASSERT_EQ(10, r);                       /* the LAST free slot */
    v = DISK_VOL(10);
    ASSERT_EQ(DISK_VOL_STATE_ASSIGNED, v->mount_state);
    ASSERT_EQ(0x1000, v->addr_start);
    ASSERT_EQ(0x1000, v->addr_end);
    ASSERT_EQ(0x12, v->sec_per_track);
    ASSERT_EQ(3, v->num_heads);
    ASSERT_EQ(0x42, v->unit_id);
    ASSERT_EQ(7, v->mount_proc);
    ASSERT_EQ(3, v->dev_unit);
    ASSERT_EQ((unsigned long)&dev_a, (unsigned long)v->dev_info);
    ASSERT_EQ(10, v->part_volx[1]);
    ASSERT_EQ(1, v->num_parts);
    ASSERT_EQ(0x36, v->blocks_per_cyl);     /* 3 * 0x12 >> 0 */
    ASSERT_EQ(10, label_vol[0]);
}

static void test_mount_takes_label_geometry(void)
{
    disk_$volume_t *v;
    labels[0].total_blocks = 0x2000;
    labels[0].addr_end = 0;
    labels[0].blocks_per_track = 0x20;
    labels[0].tracks_per_cyl = 4;
    labels[0].pv_uid.high = 0xAB; labels[0].pv_uid.low = 0xCD;
    labels[0].sectors_per_block = 4;
    labels[0].precomp_cyl = 0x99;
    geom[3] = 2;
    DISK_$PV_MOUNT_INTERNAL(2, 0, 2, 3, &unit_id, &num_blocks, &spt, &heads,
                            geom, &st);
    ASSERT_EQ(0, strcmp(log_buf, "<GWDVR>"));   /* no SET_CLEANUP */
    v = DISK_VOL(10);
    ASSERT_EQ(DISK_VOL_STATE_MOUNTED, v->mount_state);
    ASSERT_EQ(0x2000, v->addr_start);
    ASSERT_EQ(0xFFFFFF, v->addr_end);
    ASSERT_EQ(0x20, v->sec_per_track);
    ASSERT_EQ(4, v->num_heads);
    ASSERT_EQ(2, v->sector_size_code);
    ASSERT_EQ(0x20, v->blocks_per_cyl);     /* 4 * 0x20 >> 2 */
    ASSERT_EQ(0xAB, v->lv_uid.high);
    ASSERT_EQ(0x99, geom[2]);
}

static void test_mount_unit_type_4_reinits(void)
{
    labels[0].drive_type = 0x0500;
    DISK_$PV_MOUNT_INTERNAL(2, 4, 2, 3, &unit_id, &num_blocks, &spt, &heads,
                            geom, &st);
    ASSERT_EQ(0, strcmp(log_buf, "<GWDVDR>"));
    ASSERT_EQ(0x1000, DISK_VOL(10)->addr_start);   /* the second dinit's */
}

static void test_controller_not_found(void)
{
    drte_ret = NULL;
    ASSERT_EQ(0, DISK_$PV_MOUNT_INTERNAL(1, 0, 2, 3, &unit_id, &num_blocks,
                                         &spt, &heads, geom, &st));
    ASSERT_EQ(status_$io_controller_not_found, st);
    ASSERT_EQ(0, strcmp(log_buf, "<G>"));
}

static void test_already_mounted_reports_geometry(void)
{
    disk_$volume_t *v = DISK_VOL(3);
    v->mount_state = DISK_VOL_STATE_MOUNTED;
    v->dev_info = &dev_a;
    v->dev_unit = 3;
    v->unit_id = 0x77;
    v->addr_start = 0x100;
    v->num_parts = 2;
    v->sec_per_track = 9;
    v->num_heads = 5;
    v->sector_size_code = 1;
    ASSERT_EQ(3, DISK_$PV_MOUNT_INTERNAL(2, 0, 2, 3, &unit_id, &num_blocks,
                                         &spt, &heads, geom, &st));
    ASSERT_EQ(status_$disk_already_mounted, st);
    ASSERT_EQ(0x77, unit_id);
    ASSERT_EQ(0x200, num_blocks);
    ASSERT_EQ(9, spt);
    ASSERT_EQ(5, heads);
    ASSERT_EQ(2, geom[3]);
    ASSERT_EQ(0, strcmp(log_buf, "<G>"));
    /* the same device for an assign is "in use" */
    log_buf[0] = 0;
    DISK_$PV_MOUNT_INTERNAL(1, 0, 2, 3, &unit_id, &num_blocks, &spt, &heads,
                            geom, &st);
    ASSERT_EQ(status_$volume_in_use, st);
}

static void test_volume_table_full(void)
{
    int16_t i;
    for (i = 1; i <= 10; i++) DISK_VOL(i)->mount_state = DISK_VOL_STATE_ASSIGNED;
    ASSERT_EQ(0, DISK_$PV_MOUNT_INTERNAL(1, 0, 2, 3, &unit_id, &num_blocks,
                                         &spt, &heads, geom, &st));
    ASSERT_EQ(status_$volume_table_full, st);
}

static void test_bad_label_undoes_slot(void)
{
    label_status[0] = status_$invalid_physical_volume_label;
    DISK_$PV_MOUNT_INTERNAL(1, 0, 2, 3, &unit_id, &num_blocks, &spt, &heads,
                            geom, &st);
    ASSERT_EQ(status_$invalid_physical_volume_label, st);
    ASSERT_EQ(0, strcmp(log_buf, "<GCWDVRIS>"));
    ASSERT_EQ(10, invalidated[0]);
    ASSERT_EQ(DISK_VOL_STATE_FREE, DISK_VOL(10)->mount_state);
}

static void test_assign_type_0_stops_after_dinit(void)
{
    DISK_$PV_MOUNT_INTERNAL(0, 0, 2, 3, &unit_id, &num_blocks, &spt, &heads,
                            geom, &st);
    ASSERT_EQ(0, strcmp(log_buf, "<GCWD>"));
    ASSERT_EQ(DISK_VOL_STATE_ASSIGNED, DISK_VOL(10)->mount_state);
}

static void test_striped_pair(void)
{
    disk_$volume_t *v, *m;
    labels[0].num_parts = 2;
    labels[0].interleave = 1;
    labels[0].pv_uid.high = 0x51;
    labels[0].part_dev[1] = 0x0650;         /* device 6, unit 5 */
    labels[1].pv_uid.high = 0x51;
    labels[1].part_num = 2;
    DISK_$PV_MOUNT_INTERNAL(1, 0, 2, 3, &unit_id, &num_blocks, &spt, &heads,
                            geom, &st);
    ASSERT_EQ(0, st);
    ASSERT_EQ(0, strcmp(log_buf, "<GCWDVRGDVR>"));
    v = DISK_VOL(10);
    m = DISK_VOL(9);
    ASSERT_EQ(DISK_VOL_STATE_MEMBER, m->mount_state);
    ASSERT_EQ(10, m->part_volx[1]);
    ASSERT_EQ(2, m->num_parts);
    ASSERT_EQ(5, m->dev_unit);
    ASSERT_EQ((unsigned long)&dev_b, (unsigned long)m->dev_info);
    ASSERT_EQ(10, v->part_volx[1]);
    ASSERT_EQ(9, v->part_volx[2]);
    ASSERT_EQ(1, v->part_volx[0]);
    ASSERT_EQ(2, v->num_parts);
    ASSERT_EQ(1, v->stripe_blk_mask);
    ASSERT_EQ(1, v->stripe_blk_shift);
    ASSERT_EQ(0x2000, v->addr_start);
    ASSERT_EQ(9, label_vol[1]);
}

static void test_striped_uid_mismatch(void)
{
    labels[0].num_parts = 2;
    labels[0].pv_uid.high = 0x51;
    labels[1].pv_uid.high = 0x52;
    labels[1].part_num = 2;
    DISK_$PV_MOUNT_INTERNAL(1, 0, 2, 3, &unit_id, &num_blocks, &spt, &heads,
                            geom, &st);
    ASSERT_EQ(status_$invalid_physical_volume_label, st);
    ASSERT_EQ(2, released);
    ASSERT_EQ(2, n_inval);
}

static void test_striping_refused_by_device(void)
{
    labels[0].num_parts = 2;
    dev_a.unit_count = 0x0200;
    DISK_$PV_MOUNT_INTERNAL(1, 0, 2, 3, &unit_id, &num_blocks, &spt, &heads,
                            geom, &st);
    ASSERT_EQ(status_$disk_striping_not_supported, st);
}

static void test_audit_always_assign_eu(void)
{
    AUDIT_$ENABLED = (int8_t)0xFF;
    DISK_$PV_MOUNT_INTERNAL(2, 1, 2, 3, &unit_id, &num_blocks, &spt, &heads,
                            geom, &st);
    ASSERT_EQ((unsigned long)&AUDIT_$ASSIGN_DISK_EU, (unsigned long)audit_eu);
    ASSERT_EQ(0, audit_st);
    ASSERT_EQ(1, audit_rec[0]);
    ASSERT_EQ(2, audit_rec[1]);
    ASSERT_EQ(3, audit_rec[2]);
}

int main(void)
{
    printf("DISK_$PV_MOUNT_INTERNAL tests\n");
    RUN_TEST(assign_single_volume);
    RUN_TEST(mount_takes_label_geometry);
    RUN_TEST(mount_unit_type_4_reinits);
    RUN_TEST(controller_not_found);
    RUN_TEST(already_mounted_reports_geometry);
    RUN_TEST(volume_table_full);
    RUN_TEST(bad_label_undoes_slot);
    RUN_TEST(assign_type_0_stops_after_dinit);
    RUN_TEST(striped_pair);
    RUN_TEST(striped_uid_mismatch);
    RUN_TEST(striping_refused_by_device);
    RUN_TEST(audit_always_assign_eu);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
