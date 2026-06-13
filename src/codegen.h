#ifndef SIMPLECOMPILERINC_2_EMIT_H
#define SIMPLECOMPILERINC_2_EMIT_H
#include "main.h"
#include "utils.h"

typedef struct {
    int next_offset; // grows downward, starts at 4
    int peek; // peek with temp vars.
} StackFrame;

typedef enum { LOC_IMMEDIATE, LOC_VAR, LOC_GLOBAL, LOC_STACK_SLOT } LocKind;

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


// -----------------------------------------------------------------------------------------


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

typedef struct {
    StringViewListView inlining_block_start;
    StringViewList args;
} FunctionInlineInst;

typedef struct {
    StringView name;
    size_t offset;
    size_t code_size;
    bool returns_value;
    uint8_t arg_count;
    bool is_just_predef;

    bool can_be_inlined;
    FunctionInlineInst* inlining; // NULL if can_be_inlined is false
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
    bool is_local;
} FunctionCallPatch;

typedef struct {
    FunctionCallPatch* array;
    size_t len;
    size_t cap;
} FunctionCallPatchList;
#define FunctionCallPatchList_default_cap 4


FunctionsRegistry FR_new(void);
void FR_free(const FunctionsRegistry*);
void FR_register_function(FunctionsRegistry*, Function);
void FR_overwrite_function(const FunctionsRegistry*, Function);
bool FR_has_function(const FunctionsRegistry*, StringView);
Function FR_lookup_function(const FunctionsRegistry*, StringView);


FunctionCallPatchList FCPL_new(void);
void FCPL_free(const FunctionCallPatchList*);
void FCPL_register_patch(FunctionCallPatchList*, FunctionCallPatch);

// -----------------------------------------------------------------------------------------

typedef struct {
    StringView name;
    int bss_offset;
} Global;

typedef struct {
    Global* array;
    size_t len;
    size_t cap;
} GlobalsRegistry;
#define GlobalsRegistry_default_cap 4


typedef struct {
    size_t index;
    size_t offset;
    bool relative;
    uint8_t bit_size;
} GlobalPatch;

typedef struct {
    GlobalPatch* array;
    size_t len;
    size_t cap;
} GlobalPatchList;
#define GlobalPatchList_default_cap 4


GlobalsRegistry GR_new(void);
void GR_free(const GlobalsRegistry*);
size_t GR_register_global(GlobalsRegistry*, Global);
bool GR_has_global(const GlobalsRegistry*, StringView);
Global GR_lookup_global(const GlobalsRegistry*, StringView);
size_t GR_lookup_global_index(const GlobalsRegistry*, StringView);


GlobalPatchList GPL_new(void);
void GPL_free(const GlobalPatchList*);
void GPL_register_patch(GlobalPatchList*, GlobalPatch);


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
void SCARL_free(const StringConstAddrRelocationList*);

void SCARL_resolve(const StringConstAddrRelocationList*, uint8_t*, uint64_t, const StringViewList*);


// -----------------------------------------------------------------------------------------

void resolve_relocations(const ByteSeg*, const RelocationList*, const LabelList*);
void resolve_function_calls(uint8_t* array, size_t local_code_size, const FunctionsRegistry*, const FunctionCallPatchList*);
void resolve_globals(uint8_t* array, size_t bss_base_addr, const GlobalsRegistry*, const GlobalPatchList*);

#endif //SIMPLECOMPILERINC_2_EMIT_H
