
#ifndef ARCH_H
#define ARCH_H

#if defined(ARCH_M68K)
#include "arch/m68k/arch.h"
#else
#error "undefined architecture"
#endif

#endif /* ARCH_H */