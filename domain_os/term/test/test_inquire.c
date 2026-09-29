/*
 * term/test/test_inquire.c - Unit tests for TERM_$INQUIRE (0x00E66D90),
 * TERM_$READ (0x00E6681C), TERM_$READ_COND (0x00E6689A),
 * TERM_$INQ_DISCIPLINE (0x00E1AC9E), TERM_$SET_DISCIPLINE (0x00E1AB2A),
 * TERM_$PCHIST_ENABLE (0x00E72450), TERM_$SEND_KBD_STRING (0x00E1AAFC) and
 * TERM_$P2_CLEANUP (0x00E751F0)
 *
 * The real .c files are #included; the TTY_$K_*, SIO_$K_INQ_PARAM,
 * TERM_$GET_REAL_LINE / STATUS_CONVERT / SET_REAL_LINE_DISCIPLINE and
 * KBD_$PUT callees are mocked and record their arguments.
 */

#include <stdio.h>
#include <string.h>

#include "term/term_internal.h"

static int tests_run = 0;
static int tests_failed = 0;
static int current_failed;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name)                                                        \
    do {                                                                      \
        current_failed = 0;                                                   \
        tests_run++;                                                          \
        test_##name();                                                        \
        printf("%-44s %s\n", #name, current_failed ? "FAIL" : "ok");          \
    } while (0)
#define ASSERT_EQ(expected, actual)                                           \
    do {                                                                      \
        long long e_ = (long long)(expected), a_ = (long long)(actual);       \
        if (e_ != a_) {                                                       \
            printf("  %s:%d: expected 0x%llx, got 0x%llx\n",                  \
                   __FILE__, __LINE__, (unsigned long long)e_,                \
                   (unsigned long long)a_);                                   \
            if (!current_failed) { current_failed = 1; tests_failed++; }      \
        }                                                                     \
    } while (0)
#define ASSERT_PTR_EQ(e, a) ASSERT_EQ((uintptr_t)(e), (uintptr_t)(a))

term_data_t TERM_$DATA;
MODULE_DATA_DEFINE(proc2_$unwired_data_t, PROC2_$UNWIRED_DATA, 0x00E7BE84);
uid_t UID_$NIL = { 0x11111111, 0x22222222 };
const uint16_t term_$const_word_2 = 2;

/* ------------------------------------------------------------- mocks */

static int16_t real_line_value;
static status_$t real_line_status;
static int16_t real_line_arg;
short TERM_$GET_REAL_LINE(short line_num, status_$t *status_ret)
{
    real_line_arg = line_num;
    *status_ret = real_line_status;
    return real_line_value;
}

static int convert_calls;
static status_$t *convert_arg;
void TERM_$STATUS_CONVERT(status_$t *status) { convert_calls++; convert_arg = status; }

static int srld_calls;
static uint16_t srld_line;
static short *srld_disc;
void TERM_$SET_REAL_LINE_DISCIPLINE(unsigned short *line_ptr, short *discipline_ptr,
                                    status_$t *status_ret)
{
    srld_calls++;
    srld_line = *line_ptr;
    srld_disc = discipline_ptr;
    *status_ret = status_$ok;
}

static int get_calls;
static short *get_line;
static const uint16_t *get_options;
static void *get_buffer;
static ushort *get_count;
static status_$t get_status_value;
ushort TTY_$K_GET(short *line_ptr, void *options, void *buffer, ushort *count,
                  status_$t *status)
{
    get_calls++;
    get_line = line_ptr;
    get_options = (const uint16_t *)options;
    get_buffer = buffer;
    get_count = count;
    *status = get_status_value;
    return 0x1234;
}

static const uint16_t *fc_func;
static char *fc_ch;
void TTY_$K_INQ_FUNC_CHAR(short *line_ptr, const uint16_t *func_ptr, char *ch_ptr,
                          status_$t *status)
{
    (void)line_ptr;
    fc_func = func_ptr;
    fc_ch = ch_ptr;
    *ch_ptr = 'F';
    *status = status_$ok;
}

static short raw_line;
static char *raw_ptr;
void TTY_$I_INQ_RAW(short line, char *raw, status_$t *status)
{
    raw_line = line;
    raw_ptr = raw;
    *raw = (char)0xEE;
    *status = status_$ok;
}

static uint32_t mock_enabled, mock_iflags, mock_oflags;
void TTY_$K_INQ_FUNC_ENABLED(short *l, uint32_t *p, status_$t *s) { (void)l; *p = mock_enabled; *s = status_$ok; }
void TTY_$K_INQ_INPUT_FLAGS(short *l, uint32_t *p, status_$t *s) { (void)l; *p = mock_iflags; *s = status_$ok; }
void TTY_$K_INQ_OUTPUT_FLAGS(short *l, uint32_t *p, status_$t *s) { (void)l; *p = mock_oflags; *s = status_$ok; }
void TTY_$K_INQ_PGROUP(short *l, uid_t *u, status_$t *s) { (void)l; u->high = 0xAAAA; u->low = 0xBBBB; *s = status_$ok; }

static sio_params_t mock_params;
static uint32_t inq_param_sel;
void SIO_$K_INQ_PARAM(short *line_ptr, sio_params_t *params, const uint32_t *sel,
                      status_$t *status)
{
    (void)line_ptr;
    inq_param_sel = *sel;
    *params = mock_params;
    *status = status_$ok;
}

static int put_calls;
static uint16_t put_line, put_type;
static void *put_str;
static uint16_t *put_len;
void KBD_$PUT(uint16_t *line_ptr, uint16_t *type_ptr, void *str, uint16_t *length,
              status_$t *status)
{
    put_calls++;
    put_line = *line_ptr;
    put_type = *type_ptr;
    put_str = str;
    put_len = length;
    *status = 0x000B0007;
}

/* ---------------------------------------------------- code under test */

#include "../read.c"
#include "../read_cond.c"
#include "../inquire.c"
#include "../inq_discipline.c"
#include "../set_discipline.c"
#include "../pchist_enable.c"
#include "../send_kbd_string.c"
#include "../p2_cleanup.c"

static void reset(void)
{
    memset(&TERM_$DATA, 0, sizeof(TERM_$DATA));
    memset(PROC2_$UNWIRED_DATA.uid, 0, sizeof(PROC2_$UNWIRED_DATA.uid));
    memset(&mock_params, 0, sizeof(mock_params));
    real_line_value = 1;
    real_line_status = status_$ok;
    convert_calls = get_calls = srld_calls = put_calls = 0;
    get_status_value = status_$ok;
    mock_enabled = mock_iflags = mock_oflags = 0;
}

/* ------------------------------------------------------------- READ */

TEST(read_blocking_and_conditional)
{
    short line = 7;
    uint8_t buf[4];
    ushort count = 4;
    status_$t st = 0;
    unsigned short r;

    reset();
    real_line_value = 2;
    TERM_$DATA.dtte[2].flags = 0x7F;
    r = TERM_$READ(&line, buf, &count, &st);
    ASSERT_EQ(0x1234, r);
    ASSERT_EQ(7, real_line_arg);
    ASSERT_PTR_EQ(&line, get_line);
    ASSERT_PTR_EQ(&term_$const_word_0, get_options);
    ASSERT_EQ(0, *get_options);
    ASSERT_PTR_EQ(buf, get_buffer);
    ASSERT_PTR_EQ(&count, get_count);
    ASSERT_EQ(1, convert_calls);
    ASSERT_PTR_EQ(&st, convert_arg);

    TERM_$DATA.dtte[2].flags = 0x80;
    r = TERM_$READ(&line, buf, &count, &st);
    ASSERT_PTR_EQ(&term_$const_word_1, get_options);
    ASSERT_EQ(1, *get_options);

    r = TERM_$READ_COND(&line, buf, &count, &st);
    ASSERT_EQ(0x1234, r);
    ASSERT_PTR_EQ(&term_$const_word_1, get_options);
    ASSERT_EQ(3, convert_calls);
}

TEST(read_error_skips_get_and_returns_buffer_low_word)
{
    short line = 0;
    ushort count = 0;
    status_$t st = 0;
    unsigned short r;

    reset();
    real_line_status = status_$invalid_line_number;
    ARCH_HOST_VA_BASE = 0;
    r = TERM_$READ(&line, (void *)(uintptr_t)0x12345678u, &count, &st);
    ASSERT_EQ(status_$invalid_line_number, st);
    ASSERT_EQ(0, get_calls);
    ASSERT_EQ(0, convert_calls);
    ASSERT_EQ(0x5678, r);
}

/* ---------------------------------------------------------- INQUIRE */

static unsigned short inquire(unsigned short opt, unsigned short seed)
{
    short line = 3;
    unsigned short value = seed;
    status_$t st = 0;
    convert_calls = 0;
    TERM_$INQUIRE(&line, &opt, &value, &st);
    return value;
}

TEST(inquire_func_char_cells)
{
    reset();
    (void)inquire(0, 0);
    ASSERT_PTR_EQ(&term_$const_word_0, fc_func);
    (void)inquire(1, 0);
    ASSERT_PTR_EQ(&term_$const_word_2, fc_func);
    (void)inquire(2, 0);
    ASSERT_EQ(3, *fc_func);
    (void)inquire(23, 0);
    ASSERT_EQ(8, *fc_func);
    (void)inquire(26, 0);
    ASSERT_EQ(9, *fc_func);
    (void)inquire(28, 0);
    ASSERT_EQ(10, *fc_func);
    ASSERT_EQ(1, convert_calls);
}

TEST(inquire_raw_passes_value_ret_directly)
{
    short line = 3;
    unsigned short opt = 3, value = 0;
    status_$t st = 0;
    reset();
    TERM_$INQUIRE(&line, &opt, &value, &st);
    ASSERT_EQ(3, raw_line);
    ASSERT_PTR_EQ(&value, raw_ptr);
    ASSERT_EQ(0xEE, *(unsigned char *)&value);
}

TEST(inquire_flag_bits)
{
    reset();
    mock_iflags = 0;
    ASSERT_EQ(0xFF, *(unsigned char *)&(unsigned short){ inquire(4, 0) });
    mock_iflags = 1;
    ASSERT_EQ(0x00, *(unsigned char *)&(unsigned short){ inquire(4, 0xFFFF) });
    mock_iflags = 2;
    ASSERT_EQ(0xFF, *(unsigned char *)&(unsigned short){ inquire(11, 0) });
    mock_oflags = 2;
    ASSERT_EQ(0xFF, *(unsigned char *)&(unsigned short){ inquire(5, 0) });
    ASSERT_EQ(0xFF, *(unsigned char *)&(unsigned short){ inquire(29, 0) });
    mock_oflags = 1;
    ASSERT_EQ(0x00, *(unsigned char *)&(unsigned short){ inquire(5, 0xFFFF) });

    mock_enabled = 0x6000;
    ASSERT_EQ(0xFF, *(unsigned char *)&(unsigned short){ inquire(8, 0) });
    mock_enabled = 0x2000;
    ASSERT_EQ(0x00, *(unsigned char *)&(unsigned short){ inquire(8, 0xFFFF) });
    mock_enabled = 0x100;
    ASSERT_EQ(0xFF, *(unsigned char *)&(unsigned short){ inquire(10, 0) });
    mock_enabled = 0x200;
    ASSERT_EQ(0xFF, *(unsigned char *)&(unsigned short){ inquire(25, 0) });
    mock_enabled = 0x400;
    ASSERT_EQ(0xFF, *(unsigned char *)&(unsigned short){ inquire(27, 0) });
}

TEST(inquire_sio_params)
{
    reset();
    mock_params.baud_rate = 0x00012580;
    mock_params.char_size = 3;
    mock_params.stop_bits = 2;
    mock_params.parity = 3;
    mock_params.flags1 = 0x0F;
    mock_params.flags2 = 0x07;
    mock_params.break_mask = 0x1B;      /* bits 0,1,3,4 */

    ASSERT_EQ(0x2580, inquire(6, 0));  ASSERT_EQ(1, inq_param_sel);
    ASSERT_EQ(0x2580, inquire(32, 0));
    ASSERT_EQ(3, inquire(19, 0));       ASSERT_EQ(0x10, inq_param_sel);
    ASSERT_EQ(2, inquire(20, 0));       ASSERT_EQ(0x08, inq_param_sel);
    ASSERT_EQ(3, inquire(18, 0));       ASSERT_EQ(0x04, inq_param_sel);
    mock_params.parity = 2;
    ASSERT_EQ(0x5555, inquire(18, 0x5555));   /* untouched */
    ASSERT_EQ(0x000F, inquire(21, 0));  ASSERT_EQ(0x2000, inq_param_sel);
    mock_params.break_mask = 0x04;
    ASSERT_EQ(0x0000, inquire(21, 0));

    ASSERT_EQ(0xFF, *(unsigned char *)&(unsigned short){ inquire(12, 0) }); ASSERT_EQ(0x20, inq_param_sel);
    ASSERT_EQ(0xFF, *(unsigned char *)&(unsigned short){ inquire(13, 0) }); ASSERT_EQ(0x40, inq_param_sel);
    ASSERT_EQ(0xFF, *(unsigned char *)&(unsigned short){ inquire(14, 0) }); ASSERT_EQ(0x80, inq_param_sel);
    ASSERT_EQ(0xFF, *(unsigned char *)&(unsigned short){ inquire(16, 0) }); ASSERT_EQ(0x100, inq_param_sel);
    ASSERT_EQ(0xFF, *(unsigned char *)&(unsigned short){ inquire(15, 0) }); ASSERT_EQ(0x800, inq_param_sel);
    ASSERT_EQ(0xFF, *(unsigned char *)&(unsigned short){ inquire(17, 0) }); ASSERT_EQ(0x400, inq_param_sel);
    ASSERT_EQ(0xFF, *(unsigned char *)&(unsigned short){ inquire(31, 0) }); ASSERT_EQ(0x200, inq_param_sel);
    mock_params.flags1 = 0; mock_params.flags2 = 0;
    ASSERT_EQ(0x00, *(unsigned char *)&(unsigned short){ inquire(12, 0xFFFF) });
    ASSERT_EQ(0x00, *(unsigned char *)&(unsigned short){ inquire(31, 0xFFFF) });
}

TEST(inquire_misc_arms)
{
    short line = 3;
    unsigned short opt, value;
    status_$t st;
    uid_t pg;

    reset();
    ASSERT_EQ(0, inquire(9, 0xFFFF));
    {
        /* option 24 is `clr.b (A0)`: only the FIRST byte of the buffer */
        unsigned short v = inquire(24, 0xFFFF);
        ASSERT_EQ(0x00, ((unsigned char *)&v)[0]);
        ASSERT_EQ(0xFF, ((unsigned char *)&v)[1]);
    }

    opt = 30; st = 0; convert_calls = 0;
    TERM_$INQUIRE(&line, &opt, (unsigned short *)&pg, &st);
    ASSERT_EQ(0xAAAA, pg.high);
    ASSERT_EQ(0xBBBB, pg.low);
    ASSERT_EQ(1, convert_calls);

    /* option 7: dtte flags of the REAL line, error exit skips convert */
    real_line_value = 2;
    TERM_$DATA.dtte[2].flags = 0xA5;
    ASSERT_EQ(0x00A5, inquire(7, 0));
    ASSERT_EQ(1, convert_calls);
    real_line_status = status_$invalid_line_number;
    opt = 7; st = 0; value = 0x5555; convert_calls = 0;
    TERM_$INQUIRE(&line, &opt, &value, &st);
    ASSERT_EQ(status_$invalid_line_number, st);
    ASSERT_EQ(0x5555, value);
    ASSERT_EQ(0, convert_calls);
    real_line_status = status_$ok;

    /* 22 and >= 33: invalid option, then convert */
    opt = 22; st = 0; convert_calls = 0;
    TERM_$INQUIRE(&line, &opt, &value, &st);
    ASSERT_EQ(status_$term_invalid_option, st);
    ASSERT_EQ(1, convert_calls);
    opt = 33; st = 0;
    TERM_$INQUIRE(&line, &opt, &value, &st);
    ASSERT_EQ(status_$term_invalid_option, st);
}

/* ------------------------------------------------- the small wrappers */

TEST(discipline_wrappers)
{
    short line = 5;
    unsigned short disc = 0;
    short new_disc = 2;
    status_$t st = 0;

    reset();
    real_line_value = 1;
    TERM_$DATA.dtte[1].discipline = 3;
    TERM_$INQ_DISCIPLINE(&line, &disc, &st);
    ASSERT_EQ(5, real_line_arg);
    ASSERT_EQ(3, disc);

    TERM_$SET_DISCIPLINE(&line, &new_disc, &st);
    ASSERT_EQ(1, srld_calls);
    ASSERT_EQ(1, srld_line);
    ASSERT_PTR_EQ(&new_disc, srld_disc);

    real_line_status = status_$invalid_line_number;
    disc = 0x77;
    TERM_$INQ_DISCIPLINE(&line, &disc, &st);
    ASSERT_EQ(0x77, disc);
    TERM_$SET_DISCIPLINE(&line, &new_disc, &st);
    ASSERT_EQ(1, srld_calls);
}

TEST(pchist_and_send_kbd_string)
{
    unsigned short en = 0xABCD;
    status_$t st = 0x55;
    char str[3];
    uint16_t len = 3;

    reset();
    TERM_$PCHIST_ENABLE(&en, &st);
    ASSERT_EQ(status_$ok, st);
    ASSERT_EQ(0xABCD, TERM_$DATA.pchist_enable);

    TERM_$SEND_KBD_STRING(str, &len);
    ASSERT_EQ(1, put_calls);
    ASSERT_EQ(0, put_line);
    ASSERT_EQ(0, put_type);
    ASSERT_PTR_EQ(str, put_str);
    ASSERT_PTR_EQ(&len, put_len);
    ASSERT_EQ(1, convert_calls);
    ASSERT_EQ(0x000B0007, *convert_arg);
}

TEST(p2_cleanup_clears_matching_owners)
{
    short as_id = 4;
    uid_t *o0 = (uid_t *)TERM_$DATA_AT(0x1A4);
    uid_t *o1 = (uid_t *)TERM_$DATA_AT(0x1A4 + 0x4DC);
    uid_t *o2 = (uid_t *)TERM_$DATA_AT(0x1A4 + 2 * 0x4DC);
    uid_t *o3 = (uid_t *)TERM_$DATA_AT(0x1A4 + 3 * 0x4DC);

    reset();
    PROC2_$UNWIRED_DATA.uid[4].high = 0xDEAD; PROC2_$UNWIRED_DATA.uid[4].low = 0xBEEF;
    *o0 = PROC2_$UNWIRED_DATA.uid[4];
    o1->high = 0xDEAD; o1->low = 0x0001;            /* low differs */
    *o2 = PROC2_$UNWIRED_DATA.uid[4];
    *o3 = PROC2_$UNWIRED_DATA.uid[4];                            /* a fourth: not walked */

    TERM_$P2_CLEANUP(&as_id);

    ASSERT_EQ(0x11111111, o0->high); ASSERT_EQ(0x22222222, o0->low);
    ASSERT_EQ(0xDEAD, o1->high);     ASSERT_EQ(0x0001, o1->low);
    ASSERT_EQ(0x11111111, o2->high);
    ASSERT_EQ(0xDEAD, o3->high);
}

int main(void)
{
    RUN_TEST(read_blocking_and_conditional);
    RUN_TEST(read_error_skips_get_and_returns_buffer_low_word);
    RUN_TEST(inquire_func_char_cells);
    RUN_TEST(inquire_raw_passes_value_ret_directly);
    RUN_TEST(inquire_flag_bits);
    RUN_TEST(inquire_sio_params);
    RUN_TEST(inquire_misc_arms);
    RUN_TEST(discipline_wrappers);
    RUN_TEST(pchist_and_send_kbd_string);
    RUN_TEST(p2_cleanup_clears_matching_owners);
    printf("%d tests, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
