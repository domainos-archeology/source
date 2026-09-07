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

#endif /* !ARCH_M68K */
