/*
 * sio6509/sio6509.h - SIO6509 Console Serial Controller Public Interface
 *
 * The 6509 is the keyboard/display console serial controller.
 *
 * Original addresses:
 *   SIO6509_$RCV        0x00e1d53e  (not yet translated; source-77db)
 *   SIO6509_$XMIT       0x00e1d586  (not yet translated)
 *   SIO6509_$PTRS       0x00e2dfd8  (sio6509/sau2/int1_rte.s)
 *   SIO6509_$INT1_RTE   0x00e2dfe8  (sio6509/sau2/int1_rte.s)
 *   SIO6509_$INIT       0x00e3350c  (sio6509/init.c)
 *   SIO6509_$SET_LINE   0x00e72656  (not yet translated)
 *   SIO6509_$INQ_LINE   0x00e72668  (not yet translated)
 */

#ifndef SIO6509_H
#define SIO6509_H

#include "base/base.h"

/*
 * SIO6509_$INIT - Initialize a 6509 console serial channel
 *
 * Frame (link.w A6,-0x4; A2-A4 saved; five pointer arguments):
 *   (0x08,A6) int_vec_ptr  -> word vec: SIO6509_$PTRS[vec-1] = chan_struct
 *                             and the exception vector at 0x64 + vec*4
 *                             (vector 25+vec) = SIO6509_$INT1_RTE
 *   (0x0C,A6) chip_num_ptr -> word chip: registers at SAU2_SIO_BASE +
 *                             (chip-1)*0x20
 *   (0x10,A6) chan_struct  sio6509_chan_t: +0 = the register VA,
 *                          +4 = *callback
 *   (0x14,A6) callback     -> longword (the SIO descriptor VA)
 *   (0x18,A6) config       two bytes, written in turn to register +1
 *
 * Original address: 0x00e3350c (sio6509/init.c)
 */
void SIO6509_$INIT(int16_t *int_vec_ptr, int16_t *chip_num_ptr,
                   void *chan_struct, m68k_ptr_t *callback, uint8_t *config);

#endif /* SIO6509_H */
