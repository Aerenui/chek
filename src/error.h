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

void srcmap_warn(const SourceMap* restrict sm, const char* restrict pos, const char *restrict format, ...)
    PRINTF_FMT(3, 4)
    NONNULL(1,2,3)
;


#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#endif //SIMPLECOMPILERINC_2_ERROR_H
