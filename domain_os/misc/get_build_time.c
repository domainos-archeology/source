/*
 * misc/get_build_time.c - GET_BUILD_TIME implementation
 *
 * Returns the kernel build version string including revision numbers,
 * SAU type, and optionally the build timestamp.
 *
 * Original address: 0x00e38052
 *
 * The output format varies based on build configuration:
 *   - "?" if OS_$REV is non-zero (test/invalid build)
 *   - "Domain/OS kernel, revision 10.4.2" (no SAU, no VTOC)
 *   - "Domain/OS kernel(2), revision 10.4.2" (with SAU type)
 *   - Additional version components based on build flags
 */

#include "misc/misc.h"
#include "vfmt/vfmt.h"

/*
 * OS version data structure at 0xe78400
 *
 * This structure contains kernel version information populated
 * at build time. The layout is:
 *
 * Offset  Size  Field
 * ------  ----  -----
 * 0x00    4     OS_$REV - OS revision flag (0 = production)
 * 0x04    2     Major version (e.g., 10)
 * 0x06    2     Minor version (e.g., 4)
 * 0x08    2     Patch version (e.g., 2)
 * 0x0a    2     Build version flag (0 = normal)
 * 0x0c    4     Build ID/checksum
 * ...
 * 0x60    4     SAU info pointer/flags
 * 0x64    4     VTOC flag (non-zero = show timestamp)
 * 0x68    2     Kernel name length
 * 0x6c    N     Kernel name string ("Domain/OS kernel")
 * 0x8c    N     Build date string
 * 0xac    N     Build time string
 */

/* Global version info structure base */
extern int32_t OS_$REV;          /* 0xe78400 - must be 0 for production */

/* PROM-provided SAU and auxiliary type */
extern int16_t PROM_$SAU_AND_AUX; /* 0x00000100 - SAU type from PROM */

/*
 * Version data offsets from version_base (0xe78400)
 * Using A5 as base pointer in original assembly.
 */
typedef struct {
    int32_t os_rev;              /* +0x00: OS revision flag */
    int16_t major;               /* +0x04: Major version */
    int16_t minor;               /* +0x06: Minor version */
    int16_t patch;               /* +0x08: Patch version */
    int16_t build_flag;          /* +0x0a: Build version flag */
    int32_t build_id;            /* +0x0c: Build ID/checksum */
    char    pad1[0x60 - 0x10];   /* Padding */
    int32_t sau_info;            /* +0x60: SAU info */
    int32_t vtoc_flag;           /* +0x64: VTOC flag (show timestamp?) */
    int16_t name_len;            /* +0x68: Kernel name length */
    char    pad2[2];             /* Padding */
    char    name[0x40];          /* +0x6c: Kernel name string */
    char    build_date[0x20];    /* +0x8c: Build date string */
    char    build_time[0x20];    /* +0xac: Build time string */
} os_version_t;

extern os_version_t OS_VERSION_DATA;  /* At 0xe78400 */

/* Format strings from the binary */
static const char fmt_no_sau[] = "%a, revision %$";
static const char fmt_with_sau[] = "%a(%wd), revision %$";
static const char fmt_major_minor_patch[] = "%wd.%wd.%wd,%$";
static const char fmt_major_minor[] = "%wd.%wd,%$";
static const char fmt_full_version[] = "%wd.%wd.%wd.%wd,%$";
static const char fmt_date_time[] = " %a  %a %$";
static const char fmt_date_only[] = " %a %$";

/*
 * GET_BUILD_TIME - Get kernel build version string
 *
 * Formats the kernel version information into the provided buffer.
 * The format includes:
 *   - Kernel name
 *   - SAU type (if applicable)
 *   - Revision numbers (major.minor.patch or variants)
 *   - Build date/time (if VTOC flag is set)
 *
 * Parameters:
 *   buf   - Output buffer for the version string
 *   len_p - Pointer to receive the actual output length
 */
void GET_BUILD_TIME(char *buf, int16_t *len_p)
{
    int16_t max_len = 100;
    int16_t written = 0;
    int16_t segment_len;
    int16_t remaining;
    os_version_t *ver = &OS_VERSION_DATA;

    /*
     * If OS_$REV is non-zero, this is a test or invalid build.
     * Just return "?" to indicate unknown version.
     */
    if (OS_$REV != 0) {
        *len_p = 1;
        *buf = '?';
        return;
    }

    /*
     * Format the kernel name with optional SAU type.
     * SAU (System Architecture Unit) type indicates the CPU variant.
     */
    if (PROM_$SAU_AND_AUX == 0) {
        /* No SAU type - simple format */
        VFMT_$FORMATN(fmt_no_sau, buf, &max_len, len_p,
                      ver->name, ver->name_len);
    } else {
        /* Include SAU type in parentheses */
        int16_t sau_type = PROM_$SAU_AND_AUX;
        VFMT_$FORMATN(fmt_with_sau, buf, &max_len, len_p,
                      ver->name, ver->name_len, &sau_type);
    }
    written = *len_p;

    /*
     * Append version numbers.
     * Format varies based on build_flag:
     *   - build_flag != 0: major.minor.patch.build (4 components)
     *   - build_flag == 0 && patch != 0: major.minor.patch (3 components)
     *   - build_flag == 0 && patch == 0: major.minor (2 components)
     */
    remaining = 100 - written;

    if (ver->build_flag != 0) {
        /* Full 4-component version */
        VFMT_$FORMATN(fmt_full_version, buf + written, &remaining, &segment_len,
                      &ver->major, &ver->minor, &ver->patch, &ver->build_flag);
    } else if (ver->patch != 0) {
        /* 3-component version */
        VFMT_$FORMATN(fmt_major_minor_patch, buf + written, &remaining, &segment_len,
                      &ver->major, &ver->minor, &ver->patch);
    } else {
        /* 2-component version */
        VFMT_$FORMATN(fmt_major_minor, buf + written, &remaining, &segment_len,
                      &ver->major, &ver->minor);
    }
    written += segment_len;
    *len_p = written;

    /*
     * Optionally append build date/time based on VTOC flag.
     */
    remaining = 100 - written;

    if (ver->vtoc_flag != 0) {
        /* Include both date and time */
        VFMT_$FORMATN(fmt_date_time, buf + written, &remaining, &segment_len,
                      ver->build_date, (int16_t)sizeof(ver->build_date),
                      ver->build_time, (int16_t)sizeof(ver->build_time));
    } else {
        /* Just date, no time */
        VFMT_$FORMATN(fmt_date_only, buf + written, &remaining, &segment_len,
                      ver->build_date, (int16_t)sizeof(ver->build_date));
    }
    written += segment_len;
    *len_p = written;
}
