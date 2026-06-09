//
// Created by frantisek on 9. 6. 2026.
//

#ifndef SIMPLECOMPILERINC_2_ERROR_H
#define SIMPLECOMPILERINC_2_ERROR_H
#include <stddef.h>
#include "utils.h"

typedef struct {
    const char* src_start;  // pointer to beginning of full source buffer
    size_t      src_len;
    StringView filename;
} SourceMap;

typedef struct {
    size_t line;    // 1-based
    size_t col;     // 1-based
    const char* line_start;
    size_t      line_len;
} SourceLocation;

SourceLocation srcmap_locate(const SourceMap*, const char* pos);

#if defined(__GNUC__) || defined(__clang__)
#  define PRINTF_FMT(fmt_idx, va_idx) __attribute__((format(printf, fmt_idx, va_idx)))
#else
#  define PRINTF_FMT(fmt_idx, va_idx)
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define NONNULL(...) __attribute__((nonnull(__VA_ARGS__)))
#else
#  define NONNULL(...)
#endif


void srcmap_error(const SourceMap*, const char* pos, const char* format, ...)
    PRINTF_FMT(3, 4)
    NONNULL(1,2,3)
;

#endif //SIMPLECOMPILERINC_2_ERROR_H
