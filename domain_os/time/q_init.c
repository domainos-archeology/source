/*
 * TIME_$Q_INIT - Initialize queue subsystem
 *
 * This is a stub that does nothing (the actual init work is done
 * by TIME_$Q_INIT_QUEUE for each queue).
 *
 * Original address: 0x00e16c5c
 *
 * Assembly shows this is just 2 bytes at 0xe16c5c that falls through
 * to TIME_$Q_INIT_QUEUE, but in the original it was a separate entry point.
 */

#include "time.h"

void TIME_$Q_INIT(void)
{
    /* Nothing to do - each queue is initialized separately */
}
