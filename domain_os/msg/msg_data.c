/*
 * MSG global data
 *
 * On m68k the record lives at MSG_$DATA_BASE (0xE80D84) and MSG_$DATA is just
 * a cast of that address; on other hosts the storage is defined here.
 */

#include "msg/msg_internal.h"

#if !defined(ARCH_M68K)

/*
 * The image is all zeros: the socket depth table, the ownership bitmaps and
 * the open-socket count are all filled in at run time by MSG_$OPENI /
 * MSG_$ALLOCATEI.
 */
msg_$data_t MSG_$DATA_STRUCT;

/*
 * The exclusion lock at 0xE242E4 and the bounce page record at 0xE242F8.
 * ML_$EXCLUSION_INIT leaves the lock unlocked; the page addresses are filled
 * in by MSG_$INIT.
 */
ml_$exclusion_t MSG_$SOCK_LOCK_STRUCT;
msg_$dpage_t MSG_$DPAGE_STRUCT;

#endif /* !ARCH_M68K */
