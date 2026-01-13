#include "math.h"

/* Signed modulo: long % long -> long
   Computes remainder using shift-and-subtract algorithm
   Remainder sign matches dividend sign
   Returns remainder only */

long M$OIS$LLL(long dividend,long divisor) {
  int bVar1;
  long uVar1;
  int iVar2;
  short sVar3;
  
  iVar2 = dividend;
  if (dividend < 0) {
    iVar2 = -dividend;
  }
  if (divisor < 0) {
    divisor = -divisor;
  }
  sVar3 = 0x1f;
  uVar1 = 0;
  do {
    bVar1 = iVar2 < 0;
    iVar2 = iVar2 << 1;
    uVar1 = uVar1 << 1 | (uint)bVar1;
    if ((uint)divisor <= (uint)uVar1) {
      uVar1 = uVar1 - divisor;
    }
    sVar3 = sVar3 + -1;
  } while (sVar3 != -1);
  if (dividend < 0) {
    uVar1 = -uVar1;
  }
  return uVar1;
}


/* Signed modulo: long % short -> short
   Wrapper around M$OIS$LLL for 16-bit divisor
   Returns remainder as 16-bit value */

short M$OIS$WLW(long dividend,short divisor) {
  long lVar1;
  
  lVar1 = M$OIS$LLL(dividend,(int)divisor);
  return (short)lVar1;
}


/* Signed modulo: short % long -> short
   Wrapper around M$OIS$LLL for 16-bit dividend
   Returns remainder as 16-bit value */

short M$OIS$WWL(short dividend,long divisor) {
  long lVar1;
  
  lVar1 = M$OIS$LLL((int)dividend,divisor);
  return (short)lVar1;
}


/* Unsigned modulo: long % short -> short
   Two-stage modulo operation for 16-bit divisor
   Returns remainder as 16-bit value */

short M$OIU$WLW(long dividend,short divisor) {
  return (short)(CONCAT(HIGH16(dividend) % (ushort)divisor,LOW16(dividend)) % (uint)(ushort)divisor);
}

