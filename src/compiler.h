#ifndef SIMPLECOMPILERINC_2_COMPILER_H
#define SIMPLECOMPILERINC_2_COMPILER_H
#include <stdint.h>

#include "emit.h"
#include "utils.h"

extern size_t tmp_cnt;
extern size_t label_cnt;
extern ByteSeg code_output;

void process(const StringView*, StringView*, const char*);

typedef struct {
    StringView name;
    int offset; // rbp - offset
} VarEntry;

int declare_var(StringView);
int lookup_var(StringView);

int alloc_stack_slot(StackFrame*);
int alloc_tmp_stack_slot(StackFrame*);

StackFrame* get_frame(void);
int get_stack_frame_max(StackFrame*);


void create_frame(void);
void resolve_frame(void);

#endif //SIMPLECOMPILERINC_2_COMPILER_H