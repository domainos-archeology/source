#ifndef TERM_H
#define TERM_H

#include <stddef.h>

typedef unsigned long ulong;
typedef unsigned int uint;
typedef unsigned short ushort;
typedef long status_$t;

// Status codes
#define status_$ok                                          0
#define status_$invalid_line_number                         0x000b0007
#define status_$requested_line_or_operation_not_implemented 0x000b000d
#define status_$term_invalid_option                         0x000b0004

// UID structure (8 bytes)
typedef struct {
    unsigned long high;
    unsigned long low;
} uid_$t;

// Eventcount type for TERM_$GET_EC
typedef struct {
    long value;
} ec2_$eventcount_t;

// Function declarations
extern void TERM_$STATUS_CONVERT(status_$t *status);
extern short TERM_$GET_REAL_LINE(short line_num, status_$t *status_ret);
extern void TERM_$SEND_KBD_STRING(void *str, void *length);
extern void TERM_$SET_DISCIPLINE(short *line_ptr, void *discipline, status_$t *status_ret);
extern void TERM_$SET_REAL_LINE_DISCIPLINE(unsigned short *line_ptr, short *discipline_ptr,
                                           status_$t *status_ret);
extern void TERM_$INQ_DISCIPLINE(short *line_ptr, unsigned short *discipline_ret,
                                 status_$t *status_ret);
extern void TERM_$INIT(short *param1, short *param2);
extern unsigned short TERM_$READ(short *line_ptr, unsigned int buffer, void *param3,
                                  status_$t *status_ret);
extern unsigned short TERM_$READ_COND(void *line_ptr, void *buffer, void *param3,
                                       status_$t *status_ret);
extern void TERM_$WRITE(void *line_ptr, void *buffer, unsigned short *count_ptr,
                        status_$t *status_ret);
extern void TERM_$CONTROL(short *line_ptr, unsigned short *option_ptr, unsigned short *value_ptr,
                          status_$t *status_ret);
extern void TERM_$INQUIRE(short *line_ptr, unsigned short *option_ptr, unsigned short *value_ret,
                          status_$t *status_ret);
extern void TERM_$GET_EC(unsigned short *ec_id, short *term_line, ec2_$eventcount_t *ec_ret,
                         status_$t *status_ret);
extern void TERM_$HELP_CALLBACK(void);
extern void TERM_$PCHIST_ENABLE(unsigned short *enable_ptr, status_$t *status_ret);
extern void TERM_$ENQUEUE_TPAD(void **param1);
extern void TERM_$P2_CLEANUP(short *param1);

#endif /* TERM_H */
