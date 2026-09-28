/*
 * misc/get_build_time.c - GET_BUILD_TIME
 *
 * Original address: 0x00E38052
 * Size: 406 bytes (0x00E38052 .. 0x00E381E7); the format strings and the two
 * constant cells it passes by reference follow at 0x00E381E8 .. 0x00E3824B.
 *
 * Formats the kernel identification line into the caller's buffer with
 * three VFMT_$FORMATN calls: "<name>[(<sau>)], revision ", then the
 * revision numbers, then the build date/time.  A non-zero first longword
 * of OS_$REV short-circuits everything to the single character '?'.
 *
 * Frame (link.w A6,-0x8; D2/D3/A2/A3/A5 saved; A5 = 0xE78400 = OS_$REV):
 *   (0x8,A6)   buf         pointer -> A2
 *   (0xc,A6)   len_p       pointer to a word -> A3; VFMT writes the first
 *                          piece's length into it, the other two pieces are
 *                          added to it (`add.w D0w,(A3)` 0x00E38172/0x00E381DC)
 *   (-0x2,A6)  seg_len     word, the second / third piece's length
 *   (-0x4,A6)  sau         word, the machine-id word at 0x100 (D2w)
 *   (-0x6,A6)  remaining   word, 100 - *len_p
 *
 * VFMT_$FORMATN takes (format, buf, &max_len, &out_len, ...) with every
 * variable argument BY REFERENCE: a %a consumes a string pointer and a
 * pointer to its length, a %wd a pointer to a word.  The compiler emits
 * the calls with either two or five variable arguments, padding the
 * five-argument shape with pointers to the zero longword at 0x00E38224.
 *
 * Re-emitted from the disassembly 2026-09-22: the previous C passed the
 * %a lengths by value (sizeof), put name_len at +0x68 instead of +0x60,
 * used a stack max-length instead of the constant cell at 0x00E381E8,
 * and dropped the padding arguments.
 */

#include "misc/misc_internal.h"
#include "os/os.h"
#include "prom/prom.h"
#include "vfmt/vfmt.h"

/*
 * The part of the 204-byte OS_$REV block (0x00E78400, os/os.h) this routine
 * reads, as the A5 displacements show it:
 *   +0x00 long  os_rev       `tst.l (A5)`            0x00E3806E
 *   +0x04 word  major        `pea (0x4,A5)`
 *   +0x06 word  minor        `pea (0x6,A5)`
 *   +0x08 word  patch        `tst.w (0x8,A5)`        0x00E38104
 *   +0x0a word  build        `tst.w (0xa,A5)`        0x00E380CE
 *   +0x60 long  name_len     `pea (0x60,A5)`         (image: 0x10)
 *   +0x64 long  date_len     `tst.l (0x64,A5)`       0x00E38174 (image: 0)
 *   +0x68 long  time_len     `pea (0x68,A5)`         (image: 0x1D)
 *   +0x6c char  name[0x20]   "Domain/OS kernel" + blanks
 *   +0x8c char  date[0x20]   blanks in this image
 *   +0xac char  time[0x20]   "October 13, 1989  11:47:27 am"
 */
typedef struct os_$rev_block_t {
    uint32_t os_rev;            /* +0x00 */
    int16_t  major;             /* +0x04 */
    int16_t  minor;             /* +0x06 */
    int16_t  patch;             /* +0x08 */
    int16_t  build;             /* +0x0a */
    uint8_t  reserved_0c[0x54]; /* +0x0c */
    uint32_t name_len;          /* +0x60 */
    uint32_t date_len;          /* +0x64 */
    uint32_t time_len;          /* +0x68 */
    char     name[0x20];        /* +0x6c */
    char     date[0x20];        /* +0x8c */
    char     time[0x20];        /* +0xac */
} os_$rev_block_t;

_Static_assert(__builtin_offsetof(os_$rev_block_t, build) == 0x0a, "os_$rev_block_t.build");
_Static_assert(__builtin_offsetof(os_$rev_block_t, name_len) == 0x60, "os_$rev_block_t.name_len");
_Static_assert(__builtin_offsetof(os_$rev_block_t, date_len) == 0x64, "os_$rev_block_t.date_len");
_Static_assert(__builtin_offsetof(os_$rev_block_t, time_len) == 0x68, "os_$rev_block_t.time_len");
_Static_assert(__builtin_offsetof(os_$rev_block_t, name) == 0x6c, "os_$rev_block_t.name");
_Static_assert(__builtin_offsetof(os_$rev_block_t, date) == 0x8c, "os_$rev_block_t.date");
_Static_assert(__builtin_offsetof(os_$rev_block_t, time) == 0xac, "os_$rev_block_t.time");
_Static_assert(sizeof(os_$rev_block_t) == 0xcc, "os_$rev_block_t is the 204-byte OS_$REV");

/* Constant cells in the code region, all passed by reference (`pea (d,PC)`). */
/* 0x00E381E8: 00 64 - the maximum length handed to the first FORMATN */
static const int16_t get_build_time_max_len_00e381e8 = 100;
/* 0x00E38224: 00 00 00 00 - the padding argument of the 9-argument calls */
static const uint32_t get_build_time_zero_00e38224 = 0;
/* 0x00E381EA */
static const char get_build_time_fmt_date_00e381ea[] = " %a %$";
/* 0x00E381F0 */
static const char get_build_time_fmt_date_time_00e381f0[] = " %a  %a %$";
/* 0x00E381FA */
static const char get_build_time_fmt_rev3_00e381fa[] = "%wd.%wd.%wd,%$";
/* 0x00E38208 */
static const char get_build_time_fmt_rev2_00e38208[] = "%wd.%wd,%$";
/* 0x00E38212 */
static const char get_build_time_fmt_rev4_00e38212[] = "%wd.%wd.%wd.%wd,%$";
/* 0x00E38228 */
static const char get_build_time_fmt_name_00e38228[] = "%a, revision %$";
/* 0x00E38238 */
static const char get_build_time_fmt_name_sau_00e38238[] = "%a(%wd), revision %$";

void GET_BUILD_TIME(char *buf, int16_t *len_p)
{
    os_$rev_block_t *rev = (os_$rev_block_t *)OS_$REV;   /* A5 = 0xE78400 */
    int16_t sau;            /* (-0x4,A6) */
    int16_t seg_len;        /* (-0x2,A6) */
    int16_t remaining;      /* (-0x6,A6) */

    /*
     * 0x00E38068 `move.w (0x00000100).l,D2w`: a 16-bit read of the machine
     * id at 0x100.  prom/prom.h declares PROM_$MACHINE_ID as the 32-bit
     * word there, so the word read is its high half.
     */
    sau = (int16_t)(PROM_$MACHINE_ID >> 16);

    /* 0x00E3806E .. 0x00E3807A: a non-zero os_rev answers "?" */
    if (rev->os_rev != 0) {
        *len_p = 1;
        *buf = '?';
        return;
    }

    /* 0x00E3807E tst.w D2w / beq.b 0x00e380b0 */
    if (sau != 0) {
        /* 0x00E38082 .. 0x00E380AA: nine arguments, two of them padding */
        VFMT_$FORMATN(get_build_time_fmt_name_sau_00e38238, buf,
                      (int16_t *)&get_build_time_max_len_00e381e8, len_p,
                      rev->name, &rev->name_len, &sau,
                      &get_build_time_zero_00e38224, &get_build_time_zero_00e38224);
    } else {
        /* 0x00E380B0 .. 0x00E380CA: six arguments */
        VFMT_$FORMATN(get_build_time_fmt_name_00e38228, buf,
                      (int16_t *)&get_build_time_max_len_00e381e8, len_p,
                      rev->name, &rev->name_len);
    }

    /* 0x00E380CE tst.w (0xa,A5) / beq.b 0x00e38104 */
    if (rev->build != 0) {
        /* 0x00E380D4 .. 0x00E3816A: major.minor.patch.build plus one pad */
        remaining = (int16_t)(100 - *len_p);
        VFMT_$FORMATN(get_build_time_fmt_rev4_00e38212, buf + *len_p,
                      &remaining, &seg_len,
                      &rev->major, &rev->minor, &rev->patch, &rev->build,
                      &get_build_time_zero_00e38224);
    } else if (rev->patch == 0) {
        /* 0x00E3810A .. 0x00E38132: major.minor, six arguments */
        remaining = (int16_t)(100 - *len_p);
        VFMT_$FORMATN(get_build_time_fmt_rev2_00e38208, buf + *len_p,
                      &remaining, &seg_len,
                      &rev->major, &rev->minor);
    } else {
        /* 0x00E38138 .. 0x00E3816A: major.minor.patch plus two pads */
        remaining = (int16_t)(100 - *len_p);
        VFMT_$FORMATN(get_build_time_fmt_rev3_00e381fa, buf + *len_p,
                      &remaining, &seg_len,
                      &rev->major, &rev->minor, &rev->patch,
                      &get_build_time_zero_00e38224, &get_build_time_zero_00e38224);
    }

    /* 0x00E3816E .. 0x00E38172 */
    *len_p = (int16_t)(*len_p + seg_len);

    /* 0x00E38174 tst.l (0x64,A5) / beq.b 0x00e381b0 */
    if (rev->date_len != 0) {
        /* 0x00E3817A .. 0x00E381A8: " date  time " plus one pad.  No stack
         * cleanup follows this call; the frame is discarded by unlk. */
        remaining = (int16_t)(100 - *len_p);
        VFMT_$FORMATN(get_build_time_fmt_date_time_00e381f0, buf + *len_p,
                      &remaining, &seg_len,
                      rev->date, &rev->date_len, rev->time, &rev->time_len,
                      &get_build_time_zero_00e38224);
    } else {
        /* 0x00E381B0 .. 0x00E381D2: " time " only, six arguments */
        remaining = (int16_t)(100 - *len_p);
        VFMT_$FORMATN(get_build_time_fmt_date_00e381ea, buf + *len_p,
                      &remaining, &seg_len,
                      rev->time, &rev->time_len);
    }

    /* 0x00E381D8 .. 0x00E381DC */
    *len_p = (int16_t)(*len_p + seg_len);
}
