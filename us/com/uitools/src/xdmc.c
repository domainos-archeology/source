// this was originally written in pascal (the source file was //us/us/com/uitools/src/xdmc.pas)
// but let's convert to C while we do this.

#include <apollo/error.h>
#include <apollo/streams.h>
#include <apollo/pgm.h>

#include <apollointernal/help.h>

static short argc;
static pgm_$argv_ptr argv;

static char* progname = "xdmc";
static short progname_len = 4;
static char* progversion = "9.0";

void entry(void)
{
  error_$init_std_format(stream_$stderr, '?', progname, progname_len);
  help_$args(progname, progname_len, progversion, pgm_$error);
  pgm_$get_args(&argc, &argv);

  if (argc < 2) {
    error_$std_format(0," DM command expected %$");
    pgm_$set_severity(pgm_$error);
    pgm_$exit();
  }
  else {
    short cmd_len = 0;
    short sVar3 = argc - 2;
    if (sVar3 > -1) {
      int argIdx = 1;
      do {
        short argLen = argv[argIdx].len - 1;
        if (argLen > -1) {
          do {
            cmd[cmd_len++] = argv[argIdx].chars[sVar6--];
          } while (argLen != -1);
        }

        // add a space between each arg
        cmd[cmd_len++] = " ";

        argIdx++;
        sVar3--;
      } while (sVar3 != -1);
    }

    status_$t cmd_status;
    pad_$dm_cmd(stream_$stdout, cmd, cmd_len, &cmd_status);

    if (cmd_status != 0) {
      error_$std_format(cmd_status, " Error executing DM command \"%a\"%$", cmd, cmd_len);
      pgm_$set_severity(pgm_$error);
      pgm_$exit();
    }
  }
  return;
}


