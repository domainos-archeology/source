/*
 * misc/prompt_for_yes_or_no.c - Prompt user for yes/no answer
 *
 * Original address: 0x00e33778
 *
 * Prompts the user to answer yes or no, looping until a valid
 * response is given.
 */

#include "misc/misc.h"
#include "term/term.h"

/*
 * prompt_for_yes_or_no - Prompt user for yes/no answer
 *
 * Reads from terminal line 1 and checks for Y/y (yes) or N/n (no).
 * If neither, displays error message and loops until valid response.
 *
 * Returns:
 *   0xff (true)  - User answered yes
 *   0x00 (false) - User answered no
 */
uint8_t prompt_for_yes_or_no(void)
{
    char buffer[8];                /* local_14 - input buffer */
    status_$t status[2];           /* asStack_c - status return */
    short line_num = 1;            /* terminal line 1 */
    short max_len = 6;             /* max characters to read */

    for (;;) {
        TERM_$READ(&line_num, buffer, &max_len, status);

        if (buffer[0] == 'Y' || buffer[0] == 'y') {
            return 0xff;
        }

        if (buffer[0] == 'N' || buffer[0] == 'n') {
            return 0x00;
        }

        /* Invalid response - prompt again */
        ERROR_$PRINT("Please answer \"yes\" or \"no\": %$", NULL);
    }
}
