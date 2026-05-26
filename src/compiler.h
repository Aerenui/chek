#ifndef SIMPLECOMPILERINC_2_COMPILER_H
#define SIMPLECOMPILERINC_2_COMPILER_H
#include <stdint.h>

#include "emit.h"
#include "utils.h"

extern size_t tmp_cnt;
extern size_t label_cnt;
extern ByteSeg code_output;

typedef struct {
    StringView name;
    int offset; // rbp - offset
} VarEntry;

int declare_var(StringView);
int lookup_var(StringView);
int alloc_stack_slot(StackFrame*);

StackFrame* get_frame(void);

void process(const StringView*);

#endif //SIMPLECOMPILERINC_2_COMPILER_H