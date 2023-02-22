// this was originally written in pascal (the source file was //us/us/com/misc/src/stcode.pas)
// but let's convert to C while we do this.

#include <apollo/error.h>
#include <apollo/streams.h>
#include <apollo/pgm.h>
#include <apollo/vfmt.h>

static short arg_number = 1;
static char* decode_string = "%lh%$";
static char prefix_char = '?';
static char* err_fmt = "%$";
static char* command_length = "stcode";
static short command_name_length = 6;

void entry(void)
{
  char arg_buffer[32];
  const short buffer_length = sizeof(arg_buffer);
  status_$t status;
  short arg_length;
  int decode_length;
  status_$t parsed_status;
  status_$t another_status; // ??

  arg_length = pgm_$get_arg(&arg_number, arg_buffer, &status, &buffer_length);
  if (status == status_$ok) {
    vfmt_decode2(decode_string, arg_buffer, arg_length, &decode_count, &status, &parsed_status, &another_status);

    if (status == status_$ok) {
      error_$print(&parsed_status);
      return;
    }
  }

  error_$print_format(&status, stream_$stderr, &prefix_char, command_name, command_name_length, err_fmt);
  pgm_$set_severity(pgm_$error);
}
