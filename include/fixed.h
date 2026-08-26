#ifndef FIXED_H
#define FIXED_H

#include <stdint.h>

typedef int32_t fix16_t;

#define FIX16_SHIFT 16
#define FIX16_ONE   (1 << FIX16_SHIFT)

#define FIX16_FROM_INT(n) ((fix16_t)((n) << FIX16_SHIFT))
#define FIX16_TO_INT(n)   ((int32_t)((n) >> FIX16_SHIFT))
#define FIX16_MUL(a, b)   ((fix16_t)(((int64_t)(a) * (int64_t)(b)) >> FIX16_SHIFT))
#define FIX16_DIV(a, b)   ((fix16_t)(((int64_t)(a) << FIX16_SHIFT) / (b)))

#endif
