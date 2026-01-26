// OS_$BOOT_ERRCHK - Check and report boot errors
// Address: 0x00e34b14
// Size: 164 bytes
//
// If the status is non-zero, formats and displays an error message,
// waits briefly, then returns 0. Otherwise returns 0xFF (true).

#include "os/os_internal.h"

// Static wait duration data (from original binary)
// Zero duration means minimal wait
static uint16_t wait_delay_type = 0;  /* 0 = relative wait */
static clock_t wait_duration = { 0, 0 };  // Brief delay

char OS_$BOOT_ERRCHK(const char *format_str, const char *arg_str,
                     short *line_ptr, status_$t *status)
{
    status_$t local_status;
    int percent_pos;
    int line_num;
    char err_buf[104];
    short err_buf_len;
    short i;

    local_status = *status;

    // Check if status is OK (high word zero means no error)
    if ((local_status >> 16) == 0) {
        return (char)0xFF;  // true - no error
    }

    // Find position of '%' in format string (for argument insertion)
    percent_pos = 0;
    for (i = 0x31; i >= 0; i--) {
        if (format_str[percent_pos] == '%') {
            break;
        }
        percent_pos++;
    }

    // Get line number
    line_num = (int)*line_ptr;

    // Format the error message
    // Format: "      @  @     lh       " (placeholder for actual format)
    // Arguments: format_str, arg_str, percent_pos, line_num, local_status
    err_buf_len = 104;
    VFMT_$FORMATN("      @  @     lh       ",
                  err_buf, &err_buf_len,
                  format_str, percent_pos,
                  arg_str,
                  line_num,
                  local_status);

    // Display the error message
    CRASH_SHOW_STRING(err_buf);

    // Wait briefly before returning
    {
        status_$t wait_status;
        TIME_$WAIT(&wait_delay_type, &wait_duration, &wait_status);
    }

    return 0;  // false - error occurred
}
