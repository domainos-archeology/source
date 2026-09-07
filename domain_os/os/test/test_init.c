/*
 * os/test/test_init.c - unit tests for OS_$INIT's testable pieces
 *
 * OS_$INIT (0x00E337F4) cannot be run without a machine, but three parts of
 * it are pure and were lifted into static helpers in os/init.c so they can be
 * exercised here:
 *
 *   os_$install_vectors    the boot info table walk (0x00E33876-0x00E338D6)
 *   os_$boot_ws_mode       the boot device / workstation mode decoding
 *                          (0x00E338D8-0x00E338EE)
 *   os_$boot_term_params   the boot flags -> (mode, ctrl) mapping
 *                          (0x00E33C4A-0x00E33C8C)
 *
 * The real os/init.c is #included, so the helpers under test are the ones the
 * kernel builds.  Everything OS_$INIT itself calls is stubbed out; OS_$INIT
 * is never invoked.
 */

#include <stdio.h>
#include <string.h>

/*
 * os/init.c pulls in os/os_internal.h, which reaches most of the kernel's
 * public headers.  All that is needed here is the vector table backing store
 * and definitions for the handful of globals init.c refers to.
 */
#include "os/os_internal.h"

/* The host stand-in for the m68k exception vector table (os_internal.h) */
uint32_t os_$vector_table[256];

int __host_intr_disable_count = 0;

/* ------------------------------------------------------------------ */
/* Globals os/init.c refers to.  None of these matter to the tests --  */
/* they exist so the translation unit links.                          */
/* ------------------------------------------------------------------ */
uint32_t BOOT_INFO_TABLE[512];
char *INT_STACK_BASE;
void *NULL_PC;
void *NULLPROC;
os_$boot_device_t OS_$BOOT_DEVICE;

/* ------------------------------------------------------------------ */
/* Stubs for everything OS_$INIT calls.  OS_$INIT is not run.          */
/* ------------------------------------------------------------------ */
#define STUB_VOID0(name)                                                       \
    void name(void) {}
#define STUB_VOID1(name, t1)                                                   \
    void name(t1 a) { (void)a; }

STUB_VOID0(MST_$PRE_INIT)
STUB_VOID0(MMU_$INIT)
STUB_VOID0(AS_$INIT)
STUB_VOID0(MST_$INIT)
STUB_VOID0(DXM_$INIT)
STUB_VOID0(PEB_$INIT)
STUB_VOID0(UID_$INIT)
STUB_VOID0(PROC1_$INIT)
STUB_VOID0(SMD_$INIT)
STUB_VOID0(TPAD_$INIT)
STUB_VOID0(EC2_$INIT_S)
STUB_VOID0(PRINT_BUILD_TIME)
STUB_VOID0(ACL_$INIT)
STUB_VOID0(AST_$INIT)
STUB_VOID0(AREA_$INIT)
STUB_VOID0(DISK_$INIT)
STUB_VOID0(DBUF_$INIT)
STUB_VOID0(ACL_$ENTER_SUPER)
STUB_VOID0(SOCK_$INIT)
STUB_VOID0(NETWORK_$INIT)
STUB_VOID0(PROC1_$INIT_LOADAV)
STUB_VOID0(FILE_$LOCK_INIT)
STUB_VOID0(HINT_$INIT_CACHE)
STUB_VOID0(HINT_$INIT)
STUB_VOID0(LOG_$INIT)
STUB_VOID0(XPD_$INIT)
STUB_VOID0(PCHIST_$INIT)
STUB_VOID0(NETWORK_$LOAD)
STUB_VOID0(PEB_$LOAD_WCS)
STUB_VOID0(SMD_$INIT_BLINK)
STUB_VOID0(PACCT_$INIT)
STUB_VOID0(AUDIT_$INIT)
STUB_VOID0(PROC1_$INHIBIT_BEGIN)
STUB_VOID0(PROC1_$INHIBIT_END)
STUB_VOID0(MMU_$SET_SYSREV)
STUB_VOID0(FIM_$PARITY_TRAP)
STUB_VOID0(FIM_$BUS_ERR)
STUB_VOID0(PMAP_$PURIFIER_L)
STUB_VOID0(PMAP_$PURIFIER_R)
STUB_VOID0(DXM_$HELPER_WIRED)
STUB_VOID0(DXM_$HELPER_UNWIRED)

STUB_VOID1(MMU_$REMOVE, uint32_t)
STUB_VOID1(MMAP_$INIT, void *)
STUB_VOID1(MMAP_$UNWIRE, uint32_t)
STUB_VOID1(CRASH_SYSTEM, const status_$t *)
STUB_VOID1(OS_$PRINT_INIT_ERROR, const char *)
STUB_VOID1(os_$free_va_page, uint32_t)
STUB_VOID1(os_$start_proc2, void *)
STUB_VOID1(OS_$SHUTDOWN, status_$t *)
STUB_VOID1(PROC1_$SET_ASID, uint16_t)
STUB_VOID1(TIME_$INIT, uint8_t *)
STUB_VOID1(ML_$LOCK, int16_t)
STUB_VOID1(ML_$UNLOCK, int16_t)
STUB_VOID1(HINT_$ADD_NET, uint32_t)

void PROC1_$SET_TYPE(uint16_t a, uint16_t b) { (void)a; (void)b; }
void TERM_$INIT(short *a, short *b) { (void)a; (void)b; }
void DTTY_$INIT(int16_t *a, uint16_t *b) { (void)a; (void)b; }
void NAME_$INIT(uid_t *a, uid_t *b) { (void)a; (void)b; }
void VOLX_$REC_ENTRY(int16_t *a, uid_t *b) { (void)a; (void)b; }
void CAL_$SEC_TO_CLOCK(uint *a, clock_t *b) { (void)a; (void)b; }
void DBUF_$SET_BUFF(void *a, uint16_t b, status_$t *c)
{
    (void)a;
    (void)b;
    *c = status_$ok;
}
void AST_$ACTIVATE_AOTE_CANNED(uint32_t *a, uint32_t *b) { (void)a; (void)b; }
aste_t *AST_$ACTIVATE_ASTE_CANNED(uid_t *a, uint16_t b)
{
    (void)a;
    (void)b;
    return NULL;
}
void AST_$PMAP_ASSOC(aste_t *a, uint16_t b, uint32_t c, uint16_t d, uint16_t e,
                     status_$t *f)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    *f = status_$ok;
}
void IO_$INIT(void *a, char *b, status_$t *c)
{
    (void)a;
    (void)b;
    *c = status_$ok;
}
dcte_t *IO_$GET_DCTE(uint16_t *a, uint16_t *b, status_$t *c)
{
    (void)a;
    (void)b;
    *c = status_$ok;
    return NULL;
}
uint32_t RING_$GET_ID(void *a)
{
    (void)a;
    return 0;
}
char NET_IO_$BOOT_DEVICE(short a, short b)
{
    (void)a;
    (void)b;
    return 0;
}
void MST_$MAP_CANNED_AT(uint32_t a, uid_t *b, uint32_t c, uint32_t d,
                        uint32_t e, boolean f, boolean g, uint32_t h,
                        status_$t *i)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    (void)f;
    (void)g;
    (void)h;
    *i = status_$ok;
}
uint16_t MMU_$SET_PROT(uint32_t a, uint16_t b)
{
    (void)a;
    (void)b;
    return 0;
}
uint32_t VTOP_OR_CRASH(uint32_t *a) { return *a >> 10; }
int8_t MMU_$NORMAL_MODE(void) { return 0; }
uint8_t prompt_for_yes_or_no(void) { return 0; }
void VOLX_$MOUNT(int16_t *a, int16_t *b, int16_t *c, int16_t *d, int8_t *e,
                 int8_t *f, uid_t *g, uid_t *h, status_$t *i)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    (void)f;
    (void)g;
    (void)h;
    *i = status_$ok;
}
status_$t VOLX_$SHUTDOWN(void) { return status_$ok; }
char CAL_$VERIFY(int *a, void *b, char *c, status_$t *d)
{
    (void)a;
    (void)b;
    (void)c;
    *d = status_$ok;
    return 0;
}
void *DBUF_$GET_BLOCK(uint16_t a, int32_t b, uid_t *c, uint32_t d, uint32_t e,
                      status_$t *f)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    *f = status_$ok;
    return NULL;
}
void VTOCE_$READ(vtoc_$lookup_req_t *a, vtoce_$result_t *b, status_$t *c)
{
    (void)a;
    (void)b;
    *c = status_$ok;
}
void OS_$CHKSUM(void *a, void *b, void *c, char *d, status_$t *e)
{
    (void)a;
    (void)b;
    (void)c;
    *d = 0;
    *e = status_$ok;
}
int8_t io_$probe(void *a, void *b, void *c)
{
    (void)a;
    (void)b;
    (void)c;
    return 0;
}
void OS_$INSTALL_DISPLAY_ASTE(uid_t *a, void *b, int *c, char *d)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
}
uint16_t PROC1_$CREATE_P(void *a, uint32_t b, status_$t *c)
{
    (void)a;
    (void)b;
    *c = status_$ok;
    return 0;
}
uint16_t MST_$ALLOC_ASID(status_$t *a)
{
    *a = status_$ok;
    return 0;
}
void network_$fetch_diskless_info(int16_t a, uint32_t b) { (void)a; (void)b; }
ulong CAL_$CLOCK_TO_SEC(clock_t *a)
{
    (void)a;
    return 0;
}
int8_t SUB48(clock_t *a, clock_t *b)
{
    (void)a;
    (void)b;
    return 0;
}
void FILE_$SET_LEN(uid_t *a, uint32_t *b, status_$t *c)
{
    (void)a;
    (void)b;
    *c = status_$ok;
}
void FILE_$SET_REFCNT(uid_t *a, uint32_t *b, status_$t *c)
{
    (void)a;
    (void)b;
    *c = status_$ok;
}
void FILE_$LOCK(uid_t *a, const uint16_t *b, const uint16_t *c,
                const uint8_t *d, void *e, status_$t *f)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    *f = status_$ok;
}
int16_t NETWORK_$ADD_REQUEST_SERVERS(int16_t *a, status_$t *b)
{
    (void)a;
    *b = status_$ok;
    return 0;
}
void NAME_$SET_WDIR(char *a, int16_t *b, status_$t *c)
{
    (void)a;
    (void)b;
    *c = status_$ok;
}
status_$t PROC2_$INIT(uint16_t *a, status_$t *b)
{
    (void)a;
    *b = status_$ok;
    return status_$ok;
}
void VFMT_$WRITE10(const char *a, ...) { (void)a; }
uint16_t SMD_$INQ_DISP_TYPE(uint16_t *a)
{
    (void)a;
    return 0;
}
void MST_$DISKLESS_INIT(int16_t a, uint32_t b, uint32_t c)
{
    (void)a;
    (void)b;
    (void)c;
}

/* Globals owned by other subsystems that os/init.c reads or writes */
int8_t PMAP_$SHUTTING_DOWN_FLAG;
int8_t NETWORK_$DISKLESS;
int8_t NETWORK_$REALLY_DISKLESS;
char NETWORK_$DO_CHKSUM;
uint32_t NETWORK_$MOTHER_NODE;
uid_t NETWORK_$PAGING_FILE_UID;
uint32_t NODE_$ME;
uid_t UID_$NIL;
uid_t OS_WIRED_$UID;
uid_t LV_LABEL_$UID;
uid_t DISPLAY1_$UID;
uid_t ACL_$FNDWRX;
name_$data_t NAME_$DATA;
uint16_t PROC1_$CURRENT;
int16_t CAL_$BOOT_VOLX;
cal_$timezone_rec_t CAL_$TIMEZONE;
uint16_t MST_$MST_PAGES_LIMIT;
uint32_t TIME_$CLOCKH;
uint32_t TIME_$CURRENT_CLOCKH;
uint32_t TIME_$BOOT_TIME;
uint32_t TIME_$CURRENT_TIME;
uint32_t TIME_$CURRENT_USEC;
uint32_t ROUTE_$PORT;
as_$info_t AS_$INFO; /* AS_$STACK_HIGH is AS_$INFO.stack_high, 0xE2B950 */

/* ------------------------------------------------------------------ */
/* The code under test                                                 */
/* ------------------------------------------------------------------ */
#include "os/init.c"

/* DISK_$DIAG, DISK_$DO_CHKSUM and the module exclusion lock are cells of the
 * DISK_ module block (disk/disk.h), so the host build provides the block. */
uint8_t DISK_$DATA[DISK_$DATA_SIZE];

/* ------------------------------------------------------------------ */
/* Harness                                                             */
/* ------------------------------------------------------------------ */
static int tests_run;
static int tests_failed;
static int current_failed;

#define CHECK_EQ(expected, actual)                                             \
    do {                                                                       \
        long long e_ = (long long)(expected);                                  \
        long long a_ = (long long)(actual);                                    \
        if (e_ != a_) {                                                        \
            printf("\n    FAIL line %d: %s: expected 0x%llx, got 0x%llx",      \
                   __LINE__, #actual, (unsigned long long)e_,                  \
                   (unsigned long long)a_);                                    \
            current_failed = 1;                                                \
        }                                                                      \
    } while (0)

#define RUN(fn)                                                                \
    do {                                                                       \
        printf("  %-40s", #fn);                                                \
        current_failed = 0;                                                    \
        tests_run++;                                                           \
        memset(os_$vector_table, 0, sizeof(os_$vector_table));                 \
        memset(BOOT_INFO_TABLE, 0, sizeof(BOOT_INFO_TABLE));                   \
        fn();                                                                  \
        if (current_failed) {                                                  \
            tests_failed++;                                                    \
            printf("\n  %-40s FAILED\n", #fn);                                 \
        } else {                                                               \
            printf(" ok\n");                                                   \
        }                                                                      \
    } while (0)

/*
 * Build one descriptor + value group in the boot info table.
 *
 * Entry `idx` gets the {first vector, count} descriptor and entries
 * idx+1 .. idx+count get the values, all at longword offset 0x37 (byte
 * offset 0xDC) from the entry.  Returns the index of the entry after the
 * group.
 */
static int16_t put_group(int16_t idx, uint16_t first, uint16_t count,
                         const uint32_t *values)
{
    uint16_t k;

    BOOT_INFO_TABLE[idx + 0x37] = ((uint32_t)first << 16) | count;
    for (k = 0; k < count; k++) {
        BOOT_INFO_TABLE[idx + 1 + k + 0x37] = values[k];
    }
    return (int16_t)(idx + 1 + count);
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

/* 0x00E33888-0x00E33896: a zero count skips the whole group */
static void test_vectors_empty(void)
{
    put_group(1, 64, 0, NULL);
    os_$install_vectors(BOOT_INFO_TABLE);
    CHECK_EQ(0u, os_$vector_table[64]);
}

/*
 * 0x00E338B0-0x00E338C6: the pointers advance BEFORE the value is read, so
 * the values live in the entries AFTER the descriptor and the descriptor's
 * own slot is never installed.
 */
static void test_vectors_one_group(void)
{
    static const uint32_t vals[3] = {0x11111111u, 0x22222222u, 0x33333333u};

    put_group(1, 64, 3, vals);
    os_$install_vectors(BOOT_INFO_TABLE);

    CHECK_EQ(0x11111111u, os_$vector_table[64]);
    CHECK_EQ(0x22222222u, os_$vector_table[65]);
    CHECK_EQ(0x33333333u, os_$vector_table[66]);
    CHECK_EQ(0u, os_$vector_table[67]);
}

/* 0x00E338B8: a zero value leaves the vector alone */
static void test_vectors_zero_skipped(void)
{
    static const uint32_t vals[3] = {0x11111111u, 0u, 0x33333333u};

    os_$vector_table[65] = 0xAAAAAAAAu;
    put_group(1, 64, 3, vals);
    os_$install_vectors(BOOT_INFO_TABLE);

    CHECK_EQ(0x11111111u, os_$vector_table[64]);
    CHECK_EQ(0xAAAAAAAAu, os_$vector_table[65]); /* untouched */
    CHECK_EQ(0x33333333u, os_$vector_table[66]);
}

/* Two groups back to back, the second starting where the first left off */
static void test_vectors_two_groups(void)
{
    static const uint32_t a[2] = {0xA0u, 0xA1u};
    static const uint32_t b[1] = {0xB0u};
    int16_t next;

    next = put_group(1, 32, 2, a);
    put_group(next, 100, 1, b);
    os_$install_vectors(BOOT_INFO_TABLE);

    CHECK_EQ(0xA0u, os_$vector_table[32]);
    CHECK_EQ(0xA1u, os_$vector_table[33]);
    CHECK_EQ(0xB0u, os_$vector_table[100]);
}

/*
 * 0x00E338D2: `cmpi.w #0x28,D0w / ble` -- the walk stops once the entry
 * index passes 40, however the entries are grouped.  A descriptor placed at
 * entry 41 is never reached.
 */
static void test_vectors_index_bound(void)
{
    static const uint32_t vals[1] = {0xDEADBEEFu};

    put_group(41, 200, 1, vals);
    os_$install_vectors(BOOT_INFO_TABLE);
    CHECK_EQ(0u, os_$vector_table[200]);

    /* entry 40 is still inside the bound */
    memset(BOOT_INFO_TABLE, 0, sizeof(BOOT_INFO_TABLE));
    memset(os_$vector_table, 0, sizeof(os_$vector_table));
    put_group(40, 200, 1, vals);
    os_$install_vectors(BOOT_INFO_TABLE);
    CHECK_EQ(0xDEADBEEFu, os_$vector_table[200]);
}

/* 0x00E338D8-0x00E338EE */
static void test_ws_mode(void)
{
    int16_t device;

    device = 1;
    CHECK_EQ(2, os_$boot_ws_mode(&device));
    CHECK_EQ(0, device); /* the device number is cleared */

    device = 3;
    CHECK_EQ(0, os_$boot_ws_mode(&device));
    CHECK_EQ(3, device);

    device = 0;
    CHECK_EQ(0, os_$boot_ws_mode(&device));
    CHECK_EQ(0, device);
}

/* 0x00E33C4A-0x00E33C8C */
static void test_term_params(void)
{
    int16_t mode;
    uint16_t ctrl;

    /* bit 0 clear: mode 0, and ctrl forced back to 1 even with bit 4 set */
    mode = 0x7F;
    ctrl = 0x7F;
    os_$boot_term_params(0x0010, &mode, &ctrl);
    CHECK_EQ(0, mode);
    CHECK_EQ(1, ctrl);

    /* bit 0 set, bit 1 clear: mode 2, ctrl forced back to 1 */
    mode = 0x7F;
    ctrl = 0x7F;
    os_$boot_term_params(0x0011, &mode, &ctrl);
    CHECK_EQ(2, mode);
    CHECK_EQ(1, ctrl);

    /* bit 0 and bit 1 set: mode 1 and the bit-4 ctrl survives */
    mode = 0x7F;
    ctrl = 0x7F;
    os_$boot_term_params(0x0013, &mode, &ctrl);
    CHECK_EQ(1, mode);
    CHECK_EQ(2, ctrl);

    /* the same without bit 4 */
    mode = 0x7F;
    ctrl = 0x7F;
    os_$boot_term_params(0x0003, &mode, &ctrl);
    CHECK_EQ(1, mode);
    CHECK_EQ(1, ctrl);
}

/* The boot record really is words at the offsets OS_$INIT reads */
static void test_boot_params_layout(void)
{
    uint32_t raw[9];
    boot_params_t *b = (boot_params_t *)raw;

    memset(raw, 0, sizeof(raw));
    raw[0] = 0x00030007u; /* device 3, ctlr 7 */
    raw[1] = 0x00050006u; /* unit 5, flags 6 */

    CHECK_EQ(36u, sizeof(boot_params_t));
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    CHECK_EQ(3, b->device);
    CHECK_EQ(7, b->ctlr);
    CHECK_EQ(5, b->unit);
    CHECK_EQ(6, b->flags);
#else
    (void)b; /* the field order only reads back on a big-endian host */
#endif
    /* the offsets are what matters, and they hold everywhere */
    CHECK_EQ(0u, __builtin_offsetof(boot_params_t, device));
    CHECK_EQ(2u, __builtin_offsetof(boot_params_t, ctlr));
    CHECK_EQ(4u, __builtin_offsetof(boot_params_t, unit));
    CHECK_EQ(6u, __builtin_offsetof(boot_params_t, flags));
}

/* The recovered VTOCE record offsets */
static void test_vtoce_layout(void)
{
    CHECK_EQ(0x90u, sizeof(os_$init_vtoce_t));
    /*
     * source-eb9k: the record VTOCE_$READ/$WRITE and VTOC_$ALLOCATE move is
     * 36 longwords (0x00E395B0, 0x00E3977A, 0x00E38C26), which is exactly
     * what OS_$INIT provides at A6-0x128.
     */
    CHECK_EQ(sizeof(vtoce_$result_t), sizeof(os_$init_vtoce_t));
    CHECK_EQ(0x04u, __builtin_offsetof(os_$init_vtoce_t, file_uid));
    CHECK_EQ(0x14u, __builtin_offsetof(os_$init_vtoce_t, length));
    CHECK_EQ(0x74u, __builtin_offsetof(os_$init_vtoce_t, field74));
    CHECK_EQ(0x88u, __builtin_offsetof(os_$init_vtoce_t, acl_uid));
    CHECK_EQ(0x20u, sizeof(vtoc_$lookup_req_t));
    CHECK_EQ(0x08u, __builtin_offsetof(vtoc_$lookup_req_t, uid));
    CHECK_EQ(0x14u, __builtin_offsetof(vtoc_$lookup_req_t, node));
    CHECK_EQ(0x1Cu, __builtin_offsetof(vtoc_$lookup_req_t, vol_idx));
    CHECK_EQ(0x1Du, __builtin_offsetof(vtoc_$lookup_req_t, flags_1d));
}

int main(void)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("OS_$INIT (0x00E337F4) helper tests\n");

    RUN(test_vectors_empty);
    RUN(test_vectors_one_group);
    RUN(test_vectors_zero_skipped);
    RUN(test_vectors_two_groups);
    RUN(test_vectors_index_bound);
    RUN(test_ws_mode);
    RUN(test_term_params);
    RUN(test_boot_params_layout);
    RUN(test_vtoce_layout);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
