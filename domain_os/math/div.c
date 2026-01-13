#include "math.h"

// Signed division: long / long -> long
// Handles signs by converting to unsigned division, then adjusts result sign
// Returns quotient only 
long M$DIS$LLL(long dividend,long divisor) {
  ulong uVar1;
  
  if (divisor < 0) {
    if (-1 < dividend) {
      uVar1 = M$DIU$LLL(dividend,-divisor);
      return -uVar1;
    }
    uVar1 = M$DIU$LLL(-dividend,-divisor);
    return uVar1;
  }
  if (-1 < dividend) {
    uVar1 = M$DIU$LLL(dividend,divisor);
    return uVar1;
  }
  uVar1 = M$DIU$LLL(-dividend,divisor);
  return -uVar1;
}


// Signed division: long / short -> long
// Handles signs, performs two-stage division for 16-bit divisor
// Returns quotient as 32-bit value
long M$DIS$LLW(long dividend,short divisor) {
  uint uVar1;
  ushort uVar2;
  
  uVar1 = dividend;
  if (dividend < 0) {
    uVar1 = -dividend;
  }
  uVar2 = divisor;
  if (divisor < 0) {
    uVar2 = -divisor;
  }
  uVar1 = HIGH16(uVar1) / (uint)uVar2 << 0x10 |
          CONCAT((HIGH16(uVar1) % (uint)uVar2),LOW16(uVar1)) / LOW16(uVar2);
  if ((short)(HIGH16(dividend) ^ divisor) < 0) {
    uVar1 = -uVar1;
  }
  return uVar1;
}


// Unsigned division: ulong / ulong -> ulong
// Uses shift-and-subtract algorithm (32 iterations) for general case
// Optimized two-stage division when divisor < 0x10000
// Returns quotient only (remainder discarded)
ulong M$DIU$LLL(ulong dividend,ulong divisor) {
  if (HIGH16(divisor) == 0) {
    return CONCAT(
      HIGH16(dividend) / LOW16(divisor),
      CONCAT(HIGH16(dividend) % LOW16(divisor),LOW16(dividend)) / LOW16(divisor)
    );
  }

  for (short i = 0; i < 32; i ++) {
    char cVar1 = (int)dividend < 0;

    dividend = dividend << 1;
    uint uVar2 = uVar2 << 1 | (uint)cVar1;
    if (divisor <= uVar2) {
      uVar2 = uVar2 - divisor;
      dividend = CONCAT(HIGH16(dividend),dividend + 1);
    }
  }

  return dividend;
}


// Unsigned division: ulong / ushort -> uint
// Two-stage division optimized for 16-bit divisor
// Returns quotient as 32-bit value
ulong M$DIU$LLW(ulong dividend,ushort divisor) {
  return CONCAT(
    HIGH16(dividend) / divisor,
    LOW16(CONCAT(HIGH16(dividend) % divisor, LOW16(dividend)) / (uint)divisor)
  );
}
