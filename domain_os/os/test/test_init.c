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
 * The address-space boundaries (source-wh9b) are tested through the
 * os/os_internal.h macros OS_$INIT applies to its link symbols: with the SAU2
 * map's values they give back the image's immediates, and for the image's
 * layout and a sample of our link the paging loop's page classes keep the
 * stacks, MMAP and the wired ranges safe.
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
void *arch_$vector_table[ARCH_VECTOR_COUNT];

int __host_intr_disable_count = 0;

/* ------------------------------------------------------------------ */
/* Globals os/init.c refers to.  None of these matter to the tests --  */
/* they exist so the translation unit links.                          */
/* ------------------------------------------------------------------ */
uint32_t BOOT_INFO_TABLE[512];
MODULE_DATA_DEFINE(os_$stack_t, OS_$STACK, 0x00EB0000);   /* NULL_PC, INT_STACK_BASE */
MODULE_DATA_DEFINE(mmap_$mmape_table_t, MMAP_$MMAPE, 0x00EB4800); /* the pages OS_$INIT frees */
void NULLPROC(void) {}
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
void IO_$INIT(void *a, const int8_t *b, status_$t *c)
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
int8_t NET_IO_$BOOT_DEVICE(uint16_t a, uint16_t b)
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
void *DBUF_$GET_BLOCK(uint16_t a, int32_t b, uid_t *c, uint32_t d,
                      uint16_t e, uint16_t e2, status_$t *f)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    (void)e2;
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
void OS_$INSTALL_DISPLAY_ASTE(uid_t *a, const uint32_t *b, const int32_t *c,
                              const int8_t *d)
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
uint32_t PROC2_$INIT(uint16_t *a, status_$t *b)
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
void MST_$DISKLESS_INIT(boolean a, uint32_t b, uint32_t c)
{
    (void)a;
    (void)b;
    (void)c;
}

/* Globals owned by other subsystems that os/init.c reads or writes */
MODULE_DATA_DEFINE(pmap_$data_t, PMAP_$DATA, 0x00E24D44);
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

/*
 * The link symbols OS_$INIT takes its address-space boundaries from
 * (sau2.ld / tools/gen_layout_ld.py; os/os_internal.h).  OS_$INIT is never
 * run here, so they only need to exist; the boundary arithmetic is tested
 * through the os_internal.h macros with the map's and the link's values.
 * OS_DISK_PROC is a function (os/disk_proc.c), stubbed.
 */
char OS_PROC[1];
char OS_DATA_WIRED[1];
char OS_PROC_UNWIRED[1];
char OS_DISK_PROC_END[1];
char OS_DATA_UNWIRED[1];
char OS_DISK_DATA[1];
char OS_DISK_DATA_END[1];
char OS_LINK_UNPLACED[1];
char RELOC[1];
char OS_DATA_END[1];
char OS_LOW[1];
char OS_LOW_END[1];
char OS_BEGIN[1];
void OS_DISK_PROC(int16_t proc_id) { (void)proc_id; }

/* The start of the I/O space (arch/m68k/sau2/hw.h, not defined on the
 * host): the map's IODEFS = DISP1_MEM */
#define SAU2_DISPLAY_MEM_BASE 0x00FC0000u

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
        memset(arch_$vector_table, 0, sizeof(arch_$vector_table));                 \
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
    CHECK_EQ(0u, (uint32_t)(uintptr_t)arch_$vector_table[64]);
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

    CHECK_EQ(0x11111111u, (uint32_t)(uintptr_t)arch_$vector_table[64]);
    CHECK_EQ(0x22222222u, (uint32_t)(uintptr_t)arch_$vector_table[65]);
    CHECK_EQ(0x33333333u, (uint32_t)(uintptr_t)arch_$vector_table[66]);
    CHECK_EQ(0u, (uint32_t)(uintptr_t)arch_$vector_table[67]);
}

/* 0x00E338B8: a zero value leaves the vector alone */
static void test_vectors_zero_skipped(void)
{
    static const uint32_t vals[3] = {0x11111111u, 0u, 0x33333333u};

    arch_$vector_table[65] = (void *)(uintptr_t)0xAAAAAAAAu;
    put_group(1, 64, 3, vals);
    os_$install_vectors(BOOT_INFO_TABLE);

    CHECK_EQ(0x11111111u, (uint32_t)(uintptr_t)arch_$vector_table[64]);
    CHECK_EQ(0xAAAAAAAAu, (uint32_t)(uintptr_t)arch_$vector_table[65]); /* untouched */
    CHECK_EQ(0x33333333u, (uint32_t)(uintptr_t)arch_$vector_table[66]);
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

    CHECK_EQ(0xA0u, (uint32_t)(uintptr_t)arch_$vector_table[32]);
    CHECK_EQ(0xA1u, (uint32_t)(uintptr_t)arch_$vector_table[33]);
    CHECK_EQ(0xB0u, (uint32_t)(uintptr_t)arch_$vector_table[100]);
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
    CHECK_EQ(0u, (uint32_t)(uintptr_t)arch_$vector_table[200]);

    /* entry 40 is still inside the bound */
    memset(BOOT_INFO_TABLE, 0, sizeof(BOOT_INFO_TABLE));
    memset(arch_$vector_table, 0, sizeof(arch_$vector_table));
    put_group(40, 200, 1, vals);
    os_$install_vectors(BOOT_INFO_TABLE);
    CHECK_EQ(0xDEADBEEFu, (uint32_t)(uintptr_t)arch_$vector_table[200]);
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


/* ------------------------------------------------------------------ */
/* Address-space boundaries (source-wh9b)                              */
/* ------------------------------------------------------------------ */

/*
 * Applied to the SAU2 map's values, each boundary macro gives back the
 * image's run-time value, and the `+ 0x7FFF' / `+ 0x3FF' forms give back the
 * immediates the binder wrote (os/os_internal.h has the table).
 */
static void test_bounds_reproduce_image(void)
{
    /* canned map 1: 0x00E33996 / 0x00E3399C */
    CHECK_EQ(0x00D00000u, OS_INIT_SEG_DOWN(0x00D00000u));      /* OS_LOW */
    CHECK_EQ(0x00DAC7FFu, 0x00DA4800u + 0x7FFFu);              /* immediate */
    CHECK_EQ(0x00DA8000u, OS_INIT_SEG_UP(0x00DA4800u));        /* OS_LOW_END */
    /* canned maps 2-5: 0x00E339AE .. 0x00E339DC */
    CHECK_EQ(0x00E00000u, OS_INIT_SEG_DOWN(0x00E00000u));      /* OS_BEGIN */
    CHECK_EQ(0x00E38000u, OS_INIT_SEG_DOWN(0x00E38000u));      /* .TEXT */
    CHECK_EQ(0x00E78000u, OS_INIT_SEG_DOWN(0x00E78400u));      /* .DATA */
    CHECK_EQ(0x00EB1683u, 0x00EA9684u + 0x7FFFu);              /* immediate */
    CHECK_EQ(0x00EB0000u, OS_INIT_SEG_UP(0x00EA9684u));        /* OS_DATA_END */
    /* 0x00E339E6-0x00E33A0A: MSTE_PAGES + limit pages, rounded up */
    CHECK_EQ(0x00F18000u, OS_INIT_MST_HIGH(0x00EF6400u, 0x80));
    CHECK_EQ(0x00F50000u, OS_INIT_MST_HIGH(0x00EF6400u, 0x160));
    /* the limit word is sign-extended (`ext.l') */
    CHECK_EQ(0x00EF8000u, OS_INIT_MST_HIGH(0x00EF6400u, 0xFFFF));
    /* type-4 walk: 0x00E33B78 / 0x00E33B7E */
    CHECK_EQ(0x00E00BFFu, 0x00E00800u + 0x3FFu);               /* immediate */
    CHECK_EQ(0x00E00800u, OS_INIT_PAGE_UP(0x00E00800u));       /* OS_PROC */
    CHECK_EQ(0x00E1DC00u, OS_INIT_PAGE_DOWN(0x00E1DC00u));     /* OS_DATA_WIRED */
    /* keep-wired ranges: 0x00E343E6 .. 0x00E34412 */
    CHECK_EQ(0x00E38000u, OS_INIT_PAGE_DOWN(0x00E3824Cu));     /* OS_DISK_PROC */
    CHECK_EQ(0x00E3EB45u, 0x00E3E746u + 0x3FFu);
    CHECK_EQ(0x00E3E800u, OS_INIT_PAGE_UP(0x00E3E746u));       /* .._END */
    CHECK_EQ(0x00E78400u, OS_INIT_PAGE_DOWN(0x00E784D0u));     /* OS_DISK_DATA */
    CHECK_EQ(0x00E7B443u, 0x00E7B044u + 0x3FFu);
    CHECK_EQ(0x00E7B400u, OS_INIT_PAGE_UP(0x00E7B044u));       /* .._END */
    /* os_$start_proc2: 0x00E6D25C, OS_INIT_END up to .TEXT */
    CHECK_EQ(0x00E3D37Fu, 0x00E35380u + 0x7FFFu);
    CHECK_EQ(0x00E38000u, OS_INIT_SEG_UP(0x00E35380u));
    /* 0x00E3397C: IODEFS_GUARD = MSTE_PAGES + 0x166 pages, below IODEFS.
     * Holds while MST_PAGE_TABLE_BASE is the map's 0xEF6400 (source-o7s2
     * will move it). */
    CHECK_EQ(0x00F4FC00u, OS_IODEFS_GUARD);
    CHECK_EQ(1, OS_IODEFS_GUARD <= SAU2_DISPLAY_MEM_BASE);
}

/* One layout: the link-time values OS_$INIT reads */
typedef struct {
    uint32_t os_proc, data_wired, proc_unwired, disk_proc, disk_proc_end;
    uint32_t data_unwired, disk_data, disk_data_end, unplaced, reloc;
    uint32_t data_end, os_page, mmap, pttx, stack;
} layout_t;

/* What the paging loop (0x00E34426-0x00E345E2) does with one page.  A
 * model of the loop's page classes (the loop itself is inside OS_$INIT,
 * which is not run here), using the real boundary macros. */
enum { PAGE_FREED, PAGE_KEPT_WIRED, PAGE_UNWIRED };

static int classify_page(const layout_t *l, uint32_t va, int diskless)
{
    int in1, in2, in3;

    if (va >= l->data_end) { /* 0x00E34452 */
        return PAGE_FREED;
    }
    /* 0x00E34506: a diskless node lets the two disk ranges go */
    in1 = !diskless && va >= OS_INIT_PAGE_DOWN(l->disk_proc) &&
          va < OS_INIT_PAGE_UP(l->disk_proc_end);
    in2 = !diskless && va >= OS_INIT_PAGE_DOWN(l->disk_data) &&
          va < OS_INIT_PAGE_UP(l->disk_data_end);
    /* the third range, kept on every node; the image has none: its
     * fixture marks it empty */
    in3 = l->unplaced < l->reloc && va >= OS_INIT_PAGE_DOWN(l->unplaced) &&
          va < OS_INIT_PAGE_UP(l->reloc);
    return (in1 || in2 || in3) ? PAGE_KEPT_WIRED : PAGE_UNWIRED;
}

/*
 * The relationships OS_$INIT depends on, for any layout: the canned maps
 * tile the kernel without gaps, the paging loop starts and ends on the
 * segments the maps use, the stacks/MMAP/PTTX lie past it, the ranges kept
 * wired lie inside it and nothing the loop frees is below OS_DATA_END.
 */
static void check_layout(const layout_t *l)
{
    uint32_t kernel_base = OS_INIT_SEG_DOWN(0x00E00000u);
    uint32_t unwired_proc = OS_INIT_SEG_DOWN(l->proc_unwired);
    uint32_t unwired_data = OS_INIT_SEG_DOWN(l->data_unwired);
    uint32_t paged_end = OS_INIT_SEG_UP(l->data_end);
    uint32_t ro_low = OS_INIT_PAGE_UP(l->os_proc);
    uint32_t ro_high = OS_INIT_PAGE_DOWN(l->data_wired);
    uint32_t va;
    int kept = 0, unwired = 0, freed = 0;

    /* nothing wired is lost to the rounding of map 2's end */
    CHECK_EQ(l->proc_unwired, unwired_proc);
    CHECK_EQ(0u, unwired_proc % OS_SEG_SIZE);
    /* map 4 ends on OS_PAGE, where map 5 starts */
    CHECK_EQ(l->os_page, paged_end);
    CHECK_EQ(1, kernel_base < ro_low && ro_low < ro_high &&
                    ro_high < unwired_proc);
    CHECK_EQ(1, unwired_proc < unwired_data && unwired_data < paged_end);
    /* the stacks, MMAP and PTTX are never visited by the loop */
    CHECK_EQ(1, l->stack >= paged_end && l->mmap >= paged_end &&
                    l->pttx >= paged_end);
    /* the disk ranges and the unplaced range lie inside the loop */
    CHECK_EQ(1, unwired_proc <= OS_INIT_PAGE_DOWN(l->disk_proc) &&
                    OS_INIT_PAGE_UP(l->disk_proc_end) <=
                        OS_INIT_PAGE_UP(l->data_unwired));
    CHECK_EQ(1, unwired_data <= OS_INIT_PAGE_DOWN(l->disk_data) &&
                    OS_INIT_PAGE_UP(l->disk_data_end) <= l->data_end);
    CHECK_EQ(1, l->unplaced <= l->reloc && l->reloc <= l->data_end);
    /* every page from .TEXT to OS_PAGE: freed only at or past OS_DATA_END */
    for (va = unwired_proc; va < paged_end; va += OS_PAGE_SIZE) {
        int c = classify_page(l, va, 0);
        if (c == PAGE_FREED) {
            freed++;
            CHECK_EQ(1, va >= l->data_end);
        } else if (c == PAGE_KEPT_WIRED) {
            kept++;
        } else {
            unwired++;
        }
    }
    CHECK_EQ(1, kept > 0 && unwired > 0);
    CHECK_EQ(PAGE_KEPT_WIRED,
             classify_page(l, OS_INIT_PAGE_DOWN(l->disk_proc), 0));
    CHECK_EQ(PAGE_KEPT_WIRED,
             classify_page(l, OS_INIT_PAGE_DOWN(l->disk_data), 0));
    /* diskless: the disk ranges are unwired ... */
    CHECK_EQ(PAGE_UNWIRED,
             classify_page(l, OS_INIT_PAGE_DOWN(l->disk_proc), 1));
    CHECK_EQ(PAGE_UNWIRED,
             classify_page(l, OS_INIT_PAGE_DOWN(l->disk_data), 1));
    if (l->unplaced < l->reloc) {
        /* ... the unplaced range is kept, disked or diskless */
        CHECK_EQ(PAGE_KEPT_WIRED,
                 classify_page(l, OS_INIT_PAGE_DOWN(l->reloc - 1), 0));
        CHECK_EQ(PAGE_KEPT_WIRED,
                 classify_page(l, OS_INIT_PAGE_DOWN(l->unplaced), 1));
        CHECK_EQ(PAGE_KEPT_WIRED,
                 classify_page(l, OS_INIT_PAGE_DOWN(l->reloc - 1), 1));
    }
    (void)freed;
}

/* The SAU2 map's values: the image itself (nothing unplaced) */
static void test_bounds_image_layout(void)
{
    static const layout_t image = {
        0x00E00800u, 0x00E1DC00u, 0x00E38000u, 0x00E3824Cu, 0x00E3E746u,
        0x00E78400u, 0x00E784D0u, 0x00E7B044u,
        0x00E88834u, 0x00E88834u, /* OS_LINK_UNPLACED = RELOC: empty */
        0x00EA9684u, 0x00EB0000u, 0x00EB4800u, 0x00EC2800u, 0x00EB0000u,
    };
    check_layout(&image);
    /* in the image, the first page past the disk data is unwired */
    CHECK_EQ(PAGE_UNWIRED, classify_page(&image, 0x00E7B400u, 0));
    /* and the last page below OS_DATA_END is still given to the file */
    CHECK_EQ(PAGE_UNWIRED, classify_page(&image, 0x00EA9400u, 0));
    CHECK_EQ(PAGE_FREED, classify_page(&image, 0x00EA9800u, 0));
}

/* A sample of our link (build of 2026-10-06, source-wh9b) */
static void test_bounds_link_layout(void)
{
    static const layout_t link = {
        0x00E00800u, 0x00E1C400u, 0x00E30000u, 0x00E302DCu, 0x00E37FD2u,
        0x00E6E800u, 0x00E6E800u, 0x00E6E998u,
        0x00E78D2Au, 0x00EBA22Cu,
        0x00EDD2DEu, 0x00EE0000u, 0x00EE2C00u, 0x00EF0C00u, 0x00EE0000u,
    };
    check_layout(&link);
    /* the unplaced code and C data stay wired */
    CHECK_EQ(PAGE_KEPT_WIRED, classify_page(&link, 0x00E79000u, 0));
    CHECK_EQ(PAGE_KEPT_WIRED, classify_page(&link, 0x00EBA000u, 0));
    /* the fixup table's room past RELOC is unwired data, as the image's
     * table (inside ACL_$DATA) was */
    CHECK_EQ(PAGE_UNWIRED, classify_page(&link, 0x00EBA400u, 0));
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
    RUN(test_bounds_reproduce_image);
    RUN(test_bounds_image_layout);
    RUN(test_bounds_link_layout);

    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed != 0;
}
