/*
 * proc2/test/test_cleanup_handlers_internal.c -
 * PROC2_$CLEANUP_HANDLERS_INTERNAL (0x00E3E808): bit -> call mapping, the
 * call order, the constant cells and the asid argument.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    int _before = tests_failed; \
    printf("  Running %s... ", #name); \
    fflush(stdout); \
    test_##name(); \
    if (tests_failed == _before) { tests_passed++; printf("PASSED\n"); } \
} while (0)

#define ASSERT_EQ(expected, actual) do { \
    unsigned long _e = (unsigned long)(expected); \
    unsigned long _a = (unsigned long)(actual); \
    if (_e != _a) { \
        printf("FAILED\n    Expected: 0x%lx, Got: 0x%lx at line %d\n", \
               _e, _a, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#include "proc2/proc2_internal.h"
#include "lpr/lpr.h"
#include "mt/mt.h"
#include "ct/ct.h"
#include "disk/disk.h"
#include "pbu/pbu.h"
#include "gpu/gpu.h"
#include "net_io/net_io.h"
#include "scsi/scsi.h"
#include "mac_os/mac_os.h"
#include "xns/xns.h"

static char trace[32];
static int ntrace;
static int16_t lpr_unit, mt_unit, mt_mode, ct_unit, ct_mode, pchist_cmd;
static uint32_t pchist_long;
static uint16_t asid_seen[8];
static int nasid;

void LPR_$RELEASE(const int16_t *u, status_$t *s) { (void)s; trace[ntrace++] = 'L'; lpr_unit = *u; }
void MT_$RELEASE(const int16_t *u, const int16_t *m, status_$t *s) { (void)s; trace[ntrace++] = 'M'; mt_unit = *u; mt_mode = *m; }
void CT_$RELEASE(const int16_t *u, const int16_t *m, status_$t *s) { (void)s; trace[ntrace++] = 'C'; ct_unit = *u; ct_mode = *m; }
void DISK_$UNASSIGN_ALL(void) { trace[ntrace++] = 'D'; }
void TIME_$RELEASE(void) { trace[ntrace++] = 'T'; }
void PBU_$FREE_ASID(void) { trace[ntrace++] = 'P'; }
void MSG_$FREE_ASID(uint16_t *a) { trace[ntrace++] = 'm'; asid_seen[nasid++] = *a; }
void GPU_$TERM(void) { trace[ntrace++] = 'G'; }
void NET_IO_$FREE_ASID(uint16_t a) { trace[ntrace++] = 'N'; asid_seen[nasid++] = a; }
void PCHIST_$UNIX_PROFIL_CNTL(int16_t *cmd, void **buf, uint32_t *size,
                              uint32_t *off, uint32_t *scale, status_$t *s)
{
    (void)buf; (void)s;
    trace[ntrace++] = 'H';
    pchist_cmd = *cmd;
    pchist_long = *size | *off | *scale;
    if ((void *)size != (void *)off || (void *)off != (void *)scale ||
        (void *)buf != (void *)size) {
        pchist_long = 0xBAD;
    }
}
void SCSI_$FREE_ASID(void) { trace[ntrace++] = 'S'; }
void MAC_OS_$PROC2_CLEANUP(uint16_t a) { trace[ntrace++] = 'O'; asid_seen[nasid++] = a; }
void XNS_IDP_$PROC2_CLEANUP(uint16_t a) { trace[ntrace++] = 'X'; asid_seen[nasid++] = a; }

#include "../cleanup_handlers_internal.c"

static proc2_info_t e;

static void setup(uint16_t flags)
{
    memset(&e, 0, sizeof(e));
    memset(trace, 0, sizeof(trace));
    ntrace = nasid = 0;
    e.asid = 0x2A;
    e.cleanup_flags = flags;
}

TEST(no_flags_no_calls)
{
    setup(0x8101);                  /* bits 0, 8, 15 have no handler */
    PROC2_$CLEANUP_HANDLERS_INTERNAL(&e);
    ASSERT_EQ(0, ntrace);
}

TEST(all_flags_in_image_order)
{
    setup(0x7EFE);
    PROC2_$CLEANUP_HANDLERS_INTERNAL(&e);
    ASSERT_EQ(0, strcmp(trace, "LMDTCPmGNHSOX"));
    ASSERT_EQ(0, lpr_unit);
    ASSERT_EQ(-1, mt_unit);
    ASSERT_EQ(1, mt_mode);
    ASSERT_EQ(0, ct_unit);
    ASSERT_EQ((int16_t)0xFF00, ct_mode);
    ASSERT_EQ(1, pchist_cmd);
    ASSERT_EQ(0, pchist_long);
    ASSERT_EQ(4, nasid);
    ASSERT_EQ(0x2A, asid_seen[0]);
    ASSERT_EQ(0x2A, asid_seen[3]);
    ASSERT_EQ(0x7EFE, e.cleanup_flags);     /* the entry is not modified */
}

TEST(single_bits)
{
    setup(0x0080);
    PROC2_$CLEANUP_HANDLERS_INTERNAL(&e);
    ASSERT_EQ(0, strcmp(trace, "m"));
    setup(0x0400);
    PROC2_$CLEANUP_HANDLERS_INTERNAL(&e);
    ASSERT_EQ(0, strcmp(trace, "N"));
    setup(0x4020);
    PROC2_$CLEANUP_HANDLERS_INTERNAL(&e);
    ASSERT_EQ(0, strcmp(trace, "DX"));
}

int main(void)
{
    printf("PROC2_$CLEANUP_HANDLERS_INTERNAL\n");
    RUN_TEST(no_flags_no_calls);
    RUN_TEST(all_flags_in_image_order);
    RUN_TEST(single_bits);
    printf("%d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
