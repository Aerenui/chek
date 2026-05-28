//
// Created by frantisek on 25. 5. 2026.
//

#ifndef SIMPLECOMPILERINC_2_EMIT_H
#define SIMPLECOMPILERINC_2_EMIT_H
#include "utils.h"

typedef struct {
    int next_offset; // grows downward, starts at 4
    int peek; // peek with temp vars.
} StackFrame;

typedef enum { LOC_IMMEDIATE, LOC_VAR, LOC_STACK_SLOT } LocKind;

typedef struct {
    LocKind kind;

    union {
        int value; // LOC_IMMEDIATE
        int offset; // LOC_STACK_SLOT: rbp - offset
        StringView var; // LOC_VAR
    };
} Loc;

typedef struct {
    Loc* array;
    size_t cap;
    size_t len;
} LocStack;

#define LocStack_size 4

LocStack LS_new(void);

void LS_push(LocStack*, Loc);

Loc LS_pop(LocStack*);

bool LS_is_empty(LocStack*);

Loc LS_peek(LocStack*);

void LS_free(LocStack*);

// ---------------------------------------------------

void emit_mov_eax(ByteSeg*, Loc);

void emit_op_eax(ByteSeg*, uint8_t, Loc);

void emit_imul_eax(ByteSeg*, Loc);


// -----------------------------------------------------------------------------------------

/*typedef struct {
    size_t patch_pos; // offset in buffer to overwrite
    size_t patch_size; // bytes to write: 1, 2, or 4
    StringView label; // target label name (points into source)
    size_t inst_end; // offset of byte after this instruction (for rel32 calc)
} Relocation;

typedef struct {
    StringView label; // points into source
    size_t offset; // byte offset in buffer where this label is defined
} Label;*/
typedef enum { REL_ELSE, REL_END, REL_LOOP } RelKind;

typedef struct {
    size_t  patch_pos;
    size_t  patch_size;
    size_t  inst_end;
    size_t  id;
    RelKind kind;
} Relocation;

typedef struct {
    size_t  offset;
    size_t  id;
    RelKind kind;
} Label;

typedef struct {
    Relocation* array;
    size_t len;
    size_t cap;
} RelocationList;

#define RelocationList_default_cap 8

typedef struct {
    Label* array;
    size_t len;
    size_t cap;
} LabelList;

#define LabelList_default_cap 8

RelocationList RL_new(void);

void RL_free(RelocationList*);

void RL_push(RelocationList*, Relocation);

LabelList LL_new(void);

void LL_free(LabelList*);

void LL_push(LabelList*, Label);


// -----------------------------------------------------------------------------------------

void emit_jmp_label(ByteSeg* buf, RelocationList* rels, StringView label);

void emit_je_label(ByteSeg* buf, RelocationList* rels, StringView label);

// -----------------------------------------------------------------------------------------

typedef struct {
    StringView name;
    size_t offset;
    size_t code_size;
    bool returns_value;
    uint8_t arg_count;
} Function;

typedef struct {
    Function* array;
    size_t len;
    size_t cap;
} FunctionsRegistry;
#define FunctionsRegistry_default_cap 4


typedef struct {
    StringView name;
    size_t offset;
    bool relative;
    uint8_t bit_size;
} FunctionCallPatch;

typedef struct {
    FunctionCallPatch* array;
    size_t len;
    size_t cap;
} FunctionCallPatchList;
#define FunctionCallPatchList_default_cap 4


FunctionsRegistry FR_new(void);
void FR_free(FunctionsRegistry*);
void FR_register_function(FunctionsRegistry*, Function);
void FR_overwrite_function(FunctionsRegistry*, Function);
bool FR_has_function(FunctionsRegistry*, StringView);
Function FR_lookup_function(FunctionsRegistry*, StringView);


FunctionCallPatchList FCPL_new(void);
void FCPL_free(FunctionCallPatchList*);
void FCPL_register_pach(FunctionCallPatchList*, FunctionCallPatch);

// -----------------------------------------------------------------------------------------

typedef struct {
    size_t const_index;
    size_t patch_offset;
    uint8_t bit_size;
} StringConstAddrRelocation;

typedef struct {
    StringConstAddrRelocation* array;
    size_t len;
    size_t cap;
} StringConstAddrRelocationList;
#define StringConstAddrRelocationList_default_cap 4

StringConstAddrRelocationList SCARL_new(void);
void SCARL_push(StringConstAddrRelocationList*, StringConstAddrRelocation);
void SCARL_free(StringConstAddrRelocationList*);

void SCARL_resolve(StringConstAddrRelocationList*, uint8_t*, uint64_t, StringViewList*);

// -----------------------------------------------------------------------------------------

void resolve_relocations(ByteSeg*, RelocationList*, LabelList*);
void resolve_function_calls(uint8_t* array, size_t addr_offset, FunctionsRegistry*, FunctionCallPatchList*);

#endif //SIMPLECOMPILERINC_2_EMIT_H
