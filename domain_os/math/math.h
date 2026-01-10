
#ifdef __M68K
#define HIGH16(x) ((x) >> 16)
#define LOW16(x) ((x) & 0xffff)
#endif

#define CONCAT(hi, lo) (((hi) << 16) | (lo))

typedef unsigned long ulong;
typedef unsigned int uint;
typedef unsigned short ushort;

extern long M$DIS$LLL(long dividend, long divisor);
extern long M$DIS$LLW(long dividend, short divisor);
extern ulong M$DIU$LLL(ulong dividend, ulong divisor);
extern ulong M$DIU$LLW(ulong dividend, ushort divisor);
extern long M$MIS$LLL(long multiplicand, long multiplier);
extern long M$MIS$LLW(long multiplicand, short multiplier);
extern ulong M$MIU$LLL(ulong multiplicand, ulong multiplier);
extern ulong M$MIU$LLW(ulong multiplicand, ushort multiplier);
extern long M$OIS$LLL(long dividend,long divisor);
extern short M$OIS$WLW(long dividend,short divisor);
extern short M$OIS$WWL(short dividend,long divisor);
extern short M$OIU$WLW(long dividend,short divisor);
