
typedef unsigned long ulong;
typedef unsigned int uint;
typedef unsigned short ushort;
typedef long status_$t;

#define status_$ok 0

extern void TERM_$STATUS_CONVERT(status_$t *status);
