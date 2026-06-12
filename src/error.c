#include "error.h"


#include <stdarg.h>
#include <stdio.h>


SourceLocation srcmap_locate(const SourceMap* sm, const char* pos) {
    size_t line = 1;
    size_t col = 1;
    const char* line_start = sm->src_start;

    for (const char* p = sm->src_start; p < pos; p++) {
        if (*p == '\n') {
            line++;
            col = 1;
            line_start = p + 1;
        } else {
            col++;
        }
    }

    const char* line_end = line_start;
    while (line_end < sm->src_start + sm->src_len && *line_end != '\n')
        line_end++;

    return (SourceLocation){
        .line       = line,
        .col        = col,
        .line_start = line_start,
        .line_len   = (size_t)(line_end - line_start),
    };
}

void srcmap_intern_v(const char* restrict e_type, const SourceMap* restrict sm, const char* restrict pos, const char *restrict format, va_list args) {
// void srcmap_intern(const char* restrict e_type, const SourceMap* restrict sm, const char* restrict pos, const char *restrict format, ...) {
    const SourceLocation loc = srcmap_locate(sm, pos);

    fprintf(stderr, "%.*s:%zu:%zu: %s: ", (int)sm->filename.len, sm->filename.start, loc.line, loc.col, e_type);

    // va_list ap;
    // va_start(ap, format);
    vfprintf(stderr, format, args);
    // va_end(ap);
    // va_list ap = args;

    fprintf(stderr, "\n");
    fprintf(stderr, "  %.*s\n", (int)loc.line_len, loc.line_start);
    fprintf(stderr, "  %*s^\n", (int)(loc.col - 1), "");
}


void srcmap_warn(const SourceMap* restrict sm, const char* restrict pos, const char *restrict format, ...) {
    va_list args;
    va_start(args, format);
    srcmap_intern_v(ANSI_COLOR_YELLOW "warn" ANSI_COLOR_RESET, sm, pos, format, args);
    va_end(args);
}

void srcmap_error(const SourceMap* restrict sm, const char* restrict pos, const char *restrict format, ...) {
    va_list args;
    va_start(args, format);
    srcmap_intern_v( ANSI_COLOR_RED "error" ANSI_COLOR_RESET, sm, pos, format, args);
    va_end(args);
}