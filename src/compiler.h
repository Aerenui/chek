#ifndef SIMPLECOMPILERINC_2_COMPILER_H
#define SIMPLECOMPILERINC_2_COMPILER_H
#include <stdint.h>

#include "codegen.h"
#include "error.h"
#include "main.h"
#include "utils.h"

// all of these externs are shared state for the entirety of compiler

extern size_t label_cnt;
extern ByteSeg* code_output;

extern FunctionCallPatchList function_patch_list;

extern GlobalsRegistry globals_registry;
extern GlobalPatchList global_patch_list_initializer;
extern GlobalPatchList global_patch_list;

extern StackFrame global_frame;
extern SourceMap source_map;

void process(const StringView*, const StringView*, const char*, CompilerTarget);

typedef enum {
    VM_CONST, VM_RUNTIME
} VarMaterialization;

typedef struct {
    StringView name;
    int offset; // rbp - offset
    VarMaterialization vm;
    int vm_value;
} VarEntry;

int declare_var(StringView, VarMaterialization, int);
int lookup_var(StringView);
VarEntry* get_var(StringView);
bool exists_var(StringView);

int alloc_stack_slot(StackFrame*);
int alloc_tmp_stack_slot(StackFrame*);

StackFrame* get_frame(void);
int get_stack_frame_max(const StackFrame*);


void create_frame(void);
void resolve_frame(void);

#endif //SIMPLECOMPILERINC_2_COMPILER_H