#ifndef BASE_DEFS_H
#define BASE_DEFS_H

#include <stdint.h>
#include <stdbool.h>

typedef int8_t  i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef i8 b8;
typedef i32 b32;

typedef float f32;
typedef double f64;

typedef struct {
    u8* data;
    u64 size;
} string8;

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CLAMP(x, a, b) (MIN((b), MAX((a), (x))))

#define ABS(x) ((x) < 0 ? -(x) : (x))

#define KiB(x) ((u64)x << 10)
#define MiB(x) ((u64)x << 20)
#define GiB(x) ((u64)x << 30)

#define ROUND_UP_POW2(n, b) (((u64)(n) + ((u64)(b) - 1)) & (~((u64)(b) - 1)))

#endif // BASE_DEFS_H

