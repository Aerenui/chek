#include "compiler.h"

#include <assert.h>
#include <stdio.h>
#include <limits.h>

#include "error.h"
#include "arch_specific/x86_64.h"
#ifndef PATH_MAX
#define PATH_MAX 260
#endif

#if defined(_WIN32) || defined(__MINGW32__)
#include <stdlib.h>
#define realpath(rel, abs) _fullpath((abs), (rel), PATH_MAX)
#else
#include <stdlib.h>
#endif

#include "target_specific/elf_gen.h"
#include "expr.h"
#include "utils.h"
#include "target_specific/win_gen.h"


/// a counter for generating unique IDs for if/while labels and their relocations
size_t label_cnt = 0;

/// output buffer for compilation of all functions
ByteSeg code_output_funcs;

/// output buffer for globals initialization code
ByteSeg code_output_globals;

/// pointer to current output buffer
ByteSeg* code_output;

/// list of relocations to be patched
RelocationList relocations;

/// list of labes used in patching relocations
LabelList labels;

/// registry of all compiled functions, used for function calls and entry point
static FunctionsRegistry functions_registry;

/// list of unresolved call sites to be patched after everything is compiled
FunctionCallPatchList function_patch_list;

/// registry of all declared global variables and their offsets
GlobalsRegistry globals_registry;

/// global address patches for regular function bodies
GlobalPatchList global_patch_list;

/// global address patches for the global initializer code (__global function)
GlobalPatchList global_patch_list_initializer;

/// source imports tracking to prevent duplicate compilation and name collisions
static StringViewList import_table;

/// string literal constants (written into .rodata), used by runtime errors only
static StringViewList string_consts;

/// relocations for string literal constants
static StringConstAddrRelocationList string_consts_relocations;

// list of pointers to be freed after compilation but are out of normal allocations
void** content_ptrs;
size_t content_ptrs_len = 0;
size_t content_ptrs_cap = 4;

// source map for the file currently being compiled
// used for printing errors
SourceMap source_map;

// next free offset in the BSS segment
// used for globals
static size_t bss_alloc_next = 0;

// stack frame for function compilation
// is reset after each function is compiled
static StackFrame frame = {
    .next_offset = 4,
    .peek = 0
};

// stack frame used for globals initialization (__global function)
// needed because global declarations supports full expressions
StackFrame global_frame = {
    .next_offset = 4,
    .peek = 0
};

// list of local variables
// looked up in reverse to allow variable shadowing
static VarEntry* var_table;
static size_t var_table_cap;
static size_t var_table_length = 0;

// label ID for the end of the current function
// used to patch return statements
static size_t function_end_id;

static bool function_inline_candidate = true;

size_t alloc_bss_space(const uint8_t byte_size) {
    if (bss_alloc_next == 0) {
        bss_alloc_next = byte_size;
    }
    const size_t ret = bss_alloc_next;
    bss_alloc_next += byte_size;
    return ret;
}

StackFrame* get_frame(void) {
    return &frame;
}

int alloc_stack_slot(StackFrame* f) {
    const int slot = f->next_offset;
    f->next_offset += 4;
    return slot; // rbp - slot
}

int alloc_tmp_stack_slot(StackFrame* f) {
    const int slot = f->next_offset;
    if (f->next_offset + 4 > f->peek) {
        f->peek = f->next_offset + 4;
    }
    return slot;
}

int get_stack_frame_max(const StackFrame* f) {
    if (f->next_offset < f->peek)
        return f->peek;
    return f->next_offset;
}

// returns offset
size_t add_str_const(const StringView sv) {
    SVL_p_push(&string_consts, sv);
    return string_consts.len - 1;
}


void get_stmt(StringViewListView*, bool, CompilerTarget, bool in_inlining);


int lookup_var(const StringView name) {
    for (size_t i = var_table_length; i-- > 0;) {
        // reversed to find the most inner one
        if (SV_pp_cmp_eq(&var_table[i].name, &name))
            return var_table[i].offset;
    }
    // not found — should not happen if the parser is correct
    fprintf(stderr, "[ERROR] <internal> lookup_var | undefined variable: '"SV_format"'\n", SV_v_args(name));
    exit(1);
}

VarEntry* get_var(const StringView name) {
    for (size_t i = var_table_length; i-- > 0;) {
        // reversed to find the most inner one
        if (SV_pp_cmp_eq(&var_table[i].name, &name))
            return &var_table[i];
    }
    // not found — should not happen if the parser is correct
    fprintf(stderr, "[ERROR] <internal> lookup_var | undefined variable: '"SV_format"'\n", SV_v_args(name));
    exit(1);
}

bool exists_var(const StringView name) {
    // printf("exists_var('%.*s') - var_table_length = %li\n", (int)name.len, name.start, var_table_length);
    if (var_table_length == 0) {
        return false;
    }
    for (size_t i = var_table_length; i-- > 0;) {
        // reversed to find the most inner one
        if (SV_pp_cmp_eq(&var_table[i].name, &name))
            return true;
    }
    return false;
}

int declare_var(const StringView name, const VarMaterialization vm, const int vm_value) {

    if (var_table_length + 1 > var_table_cap) {
        var_table_cap *= 2;
        VarEntry* new_array = realloc(var_table, sizeof(VarEntry) * var_table_cap);
        assert(new_array != NULL);
        var_table = new_array;
    }

    switch (vm) {
        case VM_RUNTIME: {
            const int slot = alloc_stack_slot(&frame);
            var_table[var_table_length++] = (VarEntry){.name = name, .offset = slot, .vm = VM_RUNTIME, .vm_value = 0};
            return slot;
        }
        case VM_CONST: {
            var_table[var_table_length++] = (VarEntry){.name = name, .offset = 0, .vm = VM_CONST, .vm_value = vm_value};
            return -1;
        }
    }
    fprintf(stderr, "[ERROR] <internal> in declare_var | switch invalid path\n");
    exit(1);
}


void materialize_const_var(VarEntry* var, const bool include_value) {
    if (var->vm != VM_CONST) return;
    const int slot = alloc_stack_slot(&frame);

    if (include_value) {
        emit_mov_slot_imm32(code_output, slot, var->vm_value);
    }

    var->offset = slot;
    var->vm = VM_RUNTIME;
    var->vm_value = 0;
}

void materialize_const_variables(void) {
    if (!compiler_flags.constant_variable_resolution) return;

    for (size_t n=0; n < var_table_length; n++ ) {
        VarEntry* var = &var_table[n];

        if (var->vm == VM_CONST) {
            materialize_const_var(var, true);
        }
    }
}

// tokenization function
// needed only in compile(), therefore file-local
StringViewList p_tokenize(const StringView* sv) {
    StringViewList ot = SVL_new();
    size_t i = 0;
    while (i < sv->len) {
        if (sv->start[i] == ' ' || sv->start[i] == '\n' || sv->start[i] == '\t') {
            i++;
            continue;
        }
        if (sv->start[i] == '#') {
            while (i < sv->len && sv->start[i] != '\n') i++;
            continue;
        }
        if (is_operator(sv->start[i]) || sv->start[i] == '(' ||
            sv->start[i] == ')' || sv->start[i] == ';' || sv->start[i] == ',' ||
            sv->start[i] == ':') {
            SVL_p_push(&ot, SV_lrslice_from_SV(sv, i, 1));
            i++;
            continue;
        }
        if (sv->start[i] == '"') {
            const size_t start = i;
            i++;
            while (i < sv->len && sv->start[i] != '"' && sv->start[i] != '\n' && sv->start[i] != 0) {
                if (sv->start[i] == '\\') i++;
                i++;
            }
            i++;
            SVL_p_push(&ot, SV_lrslice_from_SV(sv, start, i - start));
            continue;
        }
        size_t start = i;
        while (i < sv->len &&
               sv->start[i] != ' ' && sv->start[i] != '\t' && sv->start[i] != '\n' &&
               !is_operator(sv->start[i]) &&
               sv->start[i] != '(' && sv->start[i] != ')' &&
               sv->start[i] != ';' && sv->start[i] != ':' && sv->start[i] != ',' &&
               sv->start[i] != '"') {
            i++;
        }
        SVL_p_push(&ot, SV_lrslice_from_SV(sv, start, i - start));
    }
    return ot;
}

bool is_stmt_next(StringViewListView* view) {
    if (view->len < 1) return false;
    const StringView s1 = SVLV_inspect_back(view);
    if (SV_pv_cmp_eq(&s1, "int", 3)) return true;
    if (SV_pv_cmp_eq(&s1, "set", 3)) return true;
    if (SV_pv_cmp_eq(&s1, "if", 2)) return true;
    if (SV_pv_cmp_eq(&s1, "while", 5)) return true;
    if (SV_pv_cmp_eq(&s1, "print", 5)) return true;
    if (SV_pv_cmp_eq(&s1, "call", 4)) return true;

    return false;
}

bool is_stmt_next_return(StringViewListView* view) {
    if (view->len < 1) return false;
    const StringView s1 = SVLV_inspect_back(view);
    if (SV_pv_cmp_eq(&s1, "return", 6)) return true;

    return false;
}


void get_semicolon(StringViewListView* view) {
    const StringView last_token = view->array[-1]; // based on language syntax, this should never cause problems
    const StringView semicolon = SVLV_consume_one(view);
    if (!SV_pv_cmp_eq(&semicolon, ";", 1)) {
        const char* error_pos = last_token.start + last_token.len;
        while (*error_pos == ' ' || *error_pos == '\t') error_pos++;
        srcmap_error(&source_map, error_pos, "expected ';'");
        exit(1);
    }
}

void get_return_stmt(StringViewListView* view, const bool should_return_value, const CompilerTarget target) {
    const StringView ret_keyword = SVLV_consume_one(view);
    if (!SV_pv_cmp_eq(&ret_keyword, "return", 6)) {
        fprintf(stderr, "[ERROR] <internal> should not happen | get_return_stmt and no return token\n");
        exit(1);
    }

    const StringView semi_or_val = SVLV_inspect_back(view);
    if (SV_pv_cmp_eq(&semi_or_val, ";", 1)) {
        if (should_return_value) {
            srcmap_error(&source_map, semi_or_val.start, "unexpected ';' for int function, expected return value");
            exit(1);
        }
        SVLV_consume_one(view);
    } else {
        if (!should_return_value) {
            srcmap_error(&source_map, semi_or_val.start, "expected ';' for void function, got '"SV_format"'", SV_v_args(semi_or_val));
            exit(1);
        }
        const Loc val = get_int_expr(view, target, true);
        emit_mov_eax(code_output, val, &global_patch_list, target);

        get_semicolon(view);

    }

    // JMP fn_end_<id> (placeholder)
    BS_write(code_output, 0xE9);
    const size_t jmp_patch = BS_get_cursor(code_output);
    const size_t jmp_end = jmp_patch + 4;
    BS_write_array(code_output, 4, (uint8_t[]){0x00, 0x00, 0x00, 0x00});
    RL_push(&relocations, (Relocation){
                .patch_pos = jmp_patch,
                .patch_size = 4,
                .inst_end = jmp_end,
                .id = function_end_id,
                .kind = REL_END,
            });
}


void get_stmt_block(StringViewListView* view, const bool should_return_value, const CompilerTarget target, const bool in_inlining) {
    while (view->len > 0) {
        if (is_stmt_next_return(view)) {
            get_return_stmt(view, should_return_value, target);
            return;
        }
        if (!is_stmt_next(view)) break;
        get_stmt(view, should_return_value, target, in_inlining);
    }
}


void get_int_var_dec(StringViewListView* view, const CompilerTarget target) {
    const StringView var_name = SVLV_consume_one(view);


    const StringView assign = SVLV_consume_one(view);
    if (!SV_pv_cmp_eq(&assign, ":", 1)) {
        // error
        srcmap_error(&source_map, assign.start, "expected ':' in variable declaration, got '"SV_format"'", SV_v_args(assign));
        exit(1);
    }

    const Loc last = get_int_expr(view, target, true);
    if (compiler_flags.constant_variable_resolution && last.kind == LOC_IMMEDIATE) {
        declare_var(var_name, VM_CONST, last.value);

    } else {
        const int slot = declare_var(var_name, VM_RUNTIME, 0);

        if (last.kind == LOC_IMMEDIATE) {
            emit_mov_slot_imm32(code_output, slot, last.value);
        } else {
            // move last into eax
            emit_mov_eax(code_output, last, &global_patch_list, target);

            // move to slot from eax
            emit_mov_slot_eax(code_output, slot);
        }
    }

    get_semicolon(view);
}

void get_int_var_set(StringViewListView* view, const CompilerTarget target) {
    const StringView var_name = SVLV_consume_one(view);
    int slot = -1;
    // Global global;
    bool is_local = false;
    if (exists_var(var_name)) {
        is_local = true;
        slot = lookup_var(var_name);
    } else if (GR_has_global(&globals_registry,var_name)) {
    } else {
        srcmap_error(&source_map, var_name.start, "undefined variable '"SV_format"'", SV_v_args(var_name));
        exit(1);
    }

    const StringView assign = SVLV_consume_one(view);
    if (!SV_pv_cmp_eq(&assign, ":", 1)) {
        // error
        srcmap_error(&source_map, assign.start, "expected ':' in variable change, got '"SV_format"'", SV_v_args(assign));
        exit(1);
    }

    const Loc last = get_int_expr(view, target, true);

    if (compiler_flags.constant_variable_resolution) {
        if (is_local) {
            VarEntry* var = get_var(var_name);
            if (var->vm == VM_CONST) {
                if (last.kind == LOC_IMMEDIATE) {
                    var->vm_value = last.value;
                    get_semicolon(view);
                    return;
                }

                materialize_const_var(var, true);
            }
        }
    }


    // move last into eax
    emit_mov_eax(code_output, last, &global_patch_list, target);

    if (is_local) {
        emit_mov_slot_eax(code_output, slot);
    } else {
        switch (target) {
        case F_Elf64: {
            const size_t pos = BS_get_cursor(code_output) + 3;
            uint8_t store[] = {
                0x89, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00, // mov [addr32], eax
            };
            BS_write_array(code_output, sizeof(store), store);


            size_t globals_index = GR_lookup_global_index(&globals_registry, var_name);

            GPL_register_patch(&global_patch_list, (GlobalPatch){
                                   .index = globals_index,
                                   .offset = pos,
                                   .relative = false,
                                   .bit_size = 4
                               });
            break;
        }
        case F_Win64: {
            const size_t pos = BS_get_cursor(code_output) + 2;
            uint8_t store[] = {
                0x48, 0xB9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // mov rcx, imm64
                0x89, 0x01, // mov [rcx], eax
            };
            BS_write_array(code_output, sizeof(store), store);

            size_t globals_index = GR_lookup_global_index(&globals_registry, var_name);

            GPL_register_patch(&global_patch_list, (GlobalPatch){
                                   .index = globals_index,
                                   .offset = pos,
                                   .relative = false,
                                   .bit_size = 8
                               });

            break;
        }
    }
    }


    get_semicolon(view);
}

void get_stmt_block_discard(StringViewListView* view) {
    int depth = 1;
    while (1) {
        const StringView tok = SVLV_inspect_back(view); // peek, don't consume
        if (depth == 1 && SV_pv_cmp_eq(&tok, "end", 3)) return; // leave it for caller
        if (depth == 1 && SV_pv_cmp_eq(&tok, "else", 4)) return; // same for else
        SVLV_consume_one(view);
        if (SV_pv_cmp_eq(&tok, "if", 2))   depth++;
        if (SV_pv_cmp_eq(&tok, "while", 5)) depth++;
        if (SV_pv_cmp_eq(&tok, "end", 3))  depth--;
    }
}


void get_if_conditional(StringViewListView* view, const bool should_return_value, const CompilerTarget target, const bool in_inlining) {
    const Loc cond = get_int_expr(view, target, true);

    const bool do_constant_branch_evaluation = compiler_flags.constant_branch_evaluation && cond.kind == LOC_IMMEDIATE;

    const StringView then = SVLV_consume_one(view);
    if (!SV_pv_cmp_eq(&then, "then", 4)) {
        srcmap_error(&source_map, then.start, "expected 'then' after expression in if conditional, got '"SV_format"'", SV_v_args(then));
        exit(1);
    }

    if (do_constant_branch_evaluation) {
        const bool cond_true = cond.value != 0;


        if (cond_true) {
            // compile then-block normally
            // get_stmt_block(view, should_return_value, target);
            // const int frame_next_offset_snapshot = get_frame()->next_offset;
            const size_t var_table_length_snapshot = var_table_length;

            get_stmt_block(view, should_return_value, target, in_inlining);

            // get_frame()->next_offset = frame_next_offset_snapshot;
            var_table_length = var_table_length_snapshot;

            // skip else-block if present (consume tokens without emitting)
            const StringView else_keyword = SVLV_inspect_back(view);
            if (SV_pv_cmp_eq(&else_keyword, "else", 4)) {
                SVLV_consume_one(view);
                get_stmt_block_discard(view); // consume tokens, emit nothing
            }
        } else {
            // skip then-block
            get_stmt_block_discard(view);

            const StringView else_keyword = SVLV_inspect_back(view);
            if (SV_pv_cmp_eq(&else_keyword, "else", 4)) {
                SVLV_consume_one(view);
                // get_stmt_block(view, should_return_value, target); // compile else normally
                // const int frame_next_offset_snapshot = get_frame()->next_offset;
                const size_t var_table_length_snapshot = var_table_length;

                get_stmt_block(view, should_return_value, target, in_inlining);

                // get_frame()->next_offset = frame_next_offset_snapshot;
                var_table_length = var_table_length_snapshot;
            }
            // no else: nothing emitted at all
        }

        const StringView end = SVLV_consume_one(view);
        if (!SV_pv_cmp_eq(&end, "end", 3)) {
            srcmap_error(&source_map, end.start, "expected 'end' after if conditional, got '"SV_format"'", SV_v_args(end));
            exit(1);
        }
        return;
    }

    materialize_const_variables();

    const size_t id = label_cnt++;

    // TEST eax, eax
    emit_mov_eax(code_output, cond, &global_patch_list, target);
    BS_write_array(code_output, 2, (uint8_t[]){0x85, 0xC0});

    // JE else_<id> (placeholder)
    BS_write_array(code_output, 2, (uint8_t[]){0x0F, 0x84});
    const size_t je_patch = BS_get_cursor(code_output);
    const size_t je_end = je_patch + 4;
    BS_write_array(code_output, 4, (uint8_t[]){0x00, 0x00, 0x00, 0x00});
    RL_push(&relocations, (Relocation){
                .patch_pos = je_patch,
                .patch_size = 4,
                .inst_end = je_end,
                .id = id,
                .kind = REL_ELSE,
            });

    // int frame_next_offset_snapshot = get_frame()->next_offset;
    size_t var_table_length_snapshot = var_table_length;

    get_stmt_block(view, should_return_value, target, in_inlining);

    // get_frame()->next_offset = frame_next_offset_snapshot;
    var_table_length = var_table_length_snapshot;

    const StringView else_keyword = SVLV_inspect_back(view);
    if (SV_pv_cmp_eq(&else_keyword, "else", 4)) {
        SVLV_consume_one(view);

        // JMP end_<id> (placeholder)
        BS_write(code_output, 0xE9);
        const size_t jmp_patch = BS_get_cursor(code_output);
        const size_t jmp_end = jmp_patch + 4;
        BS_write_array(code_output, 4, (uint8_t[]){0x00, 0x00, 0x00, 0x00});
        RL_push(&relocations, (Relocation){
                    .patch_pos = jmp_patch,
                    .patch_size = 4,
                    .inst_end = jmp_end,
                    .id = id,
                    .kind = REL_END,
                });

        // define else_<id>
        LL_push(&labels, (Label){.offset = BS_get_cursor(code_output), .id = id, .kind = REL_ELSE});


        // frame_next_offset_snapshot = get_frame()->next_offset;
        var_table_length_snapshot = var_table_length;

        get_stmt_block(view, should_return_value, target, in_inlining);

        // get_frame()->next_offset = frame_next_offset_snapshot;
        var_table_length = var_table_length_snapshot;
    } else {
        // no else — JE jumps directly to end, define else_<id> here as same as end
        LL_push(&labels, (Label){.offset = BS_get_cursor(code_output), .id = id, .kind = REL_ELSE});
    }

    // define end_<id>
    LL_push(&labels, (Label){.offset = BS_get_cursor(code_output), .id = id, .kind = REL_END});

    const StringView end = SVLV_consume_one(view);
    if (!SV_pv_cmp_eq(&end, "end", 3)) {
        srcmap_error(&source_map, end.start, "expected 'end' after if conditional, got '"SV_format"'", SV_v_args(end));
        exit(1);
    }
}

void get_while_conditional(StringViewListView* view, const bool should_return_value, const CompilerTarget target, const bool in_inlining) {

    const size_t id = label_cnt++;

    const size_t loop_cursor = BS_get_cursor(code_output);

    const Loc cond = get_int_expr(view, target, true);

    const StringView do_keyword = SVLV_consume_one(view);
    if (!SV_pv_cmp_eq(&do_keyword, "do", 2)) {
        srcmap_error(&source_map, do_keyword.start, "expected 'do' after expression in while conditional, got '"SV_format"'", SV_v_args(do_keyword));
        exit(1);
    }

    if (compiler_flags.constant_branch_evaluation && cond.kind == LOC_IMMEDIATE && cond.value == 0) {
        // discard `while 0 do ... end`
        get_stmt_block_discard(view);
        const StringView end = SVLV_consume_one(view);
        if (!SV_pv_cmp_eq(&end, "end", 3)) {
            srcmap_error(&source_map, end.start, "expected 'end' after while conditional, got '"SV_format"'", SV_v_args(end));
            exit(1);
        }
        return;
    }

    // define loop_<id> (loop back target)
    LL_push(&labels, (Label){.offset = loop_cursor, .id = id, .kind = REL_LOOP});


    // TEST eax, eax
    emit_mov_eax(code_output, cond, &global_patch_list, target);
    BS_write_array(code_output, 2, (uint8_t[]){0x85, 0xC0});

    // JE end_<id> (placeholder)
    BS_write_array(code_output, 2, (uint8_t[]){0x0F, 0x84});
    const size_t je_patch = BS_get_cursor(code_output);
    const size_t je_end = je_patch + 4;
    BS_write_array(code_output, 4, (uint8_t[]){0x00, 0x00, 0x00, 0x00});
    RL_push(&relocations, (Relocation){
                .patch_pos = je_patch,
                .patch_size = 4,
                .inst_end = je_end,
                .id = id,
                .kind = REL_END,
            });

    // body
    // const int frame_next_offset_snapshot = get_frame()->next_offset;
    const size_t var_table_length_snapshot = var_table_length;

    get_stmt_block(view, should_return_value, target, in_inlining);

    // get_frame()->next_offset = frame_next_offset_snapshot;
    var_table_length = var_table_length_snapshot;

    // JMP loop_<id> (placeholder)
    BS_write(code_output, 0xE9);
    const size_t jmp_patch = BS_get_cursor(code_output);
    const size_t jmp_end = jmp_patch + 4;
    BS_write_array(code_output, 4, (uint8_t[]){0x00, 0x00, 0x00, 0x00});
    RL_push(&relocations, (Relocation){
                .patch_pos = jmp_patch,
                .patch_size = 4,
                .inst_end = jmp_end,
                .id = id,
                .kind = REL_LOOP,
            });

    // define end_<id>
    LL_push(&labels, (Label){.offset = BS_get_cursor(code_output), .id = id, .kind = REL_END});

    const StringView end = SVLV_consume_one(view);
    if (!SV_pv_cmp_eq(&end, "end", 3)) {
        srcmap_error(&source_map, end.start, "expected 'end' after while conditional, got '"SV_format"'", SV_v_args(end));
        exit(1);
    }
}


void get_print_stmt(StringViewListView* view, const CompilerTarget target) {
    while (view->len > 0) {
        const StringView inspect = SVLV_inspect_back(view);
        if (inspect.start[0] == ';') {
            break;
        }

        if (inspect.start[0] == '"') {
            StringView str = SVLV_consume_one(view);
            str.start++;
            str.len-=2;
            // printf("print stmt: to print string: "SV_format"\n", SV_v_args(inspect));

            // movabs rax, addr
            BS_write(code_output, 0x48);
            BS_write(code_output, 0xB8);
            const size_t addr_cursor = BS_get_cursor(code_output);
            uint8_t pad[8] = {0};
            BS_write_array(code_output, sizeof(pad), pad);

            const size_t str_index = add_str_const(str);
            SCARL_push(&string_consts_relocations, (StringConstAddrRelocation){
                   .const_index = str_index,
                   .patch_offset = addr_cursor, // offset for string in .rodata
                   .bit_size = 8,
               });

            // mov ebx, LEN
            BS_write(code_output, 0xBB);
            const uint32_t str_len = (uint32_t)str.len;
            BS_write(code_output, (str_len >> 0) & 0xFF);
            BS_write(code_output, (str_len >> 8) & 0xFF);
            BS_write(code_output, (str_len >> 16) & 0xFF);
            BS_write(code_output, (str_len >> 24) & 0xFF);
        } else {
            const Loc expr = get_int_expr(view, target, true);

            // puts address of char into RAX and calls __print
            switch (expr.kind) {
                case LOC_IMMEDIATE:
                case LOC_STACK_SLOT:
                case LOC_GLOBAL: {
                    emit_mov_eax(code_output, expr, &global_patch_list, target);
                    const int slot = alloc_tmp_stack_slot(&frame);

                    emit_mov_slot_eax(code_output, slot);

                    // lea rax, [rbp - offset]
                    emit_lea_rax_slot(code_output, slot);
                    break;
                }
                case LOC_VAR: {
                    const int slot = lookup_var(expr.var);

                    // lea rax, [rbp - offset]
                    emit_lea_rax_slot(code_output, slot);

                    break;
                }
            }


            // LEN
            // mov ebx, 0x1
            BS_write_array(code_output, 5, (uint8_t[]){0xBB, 0x01, 0x00, 0x00, 0x00});
        }

        // str addr is in rax
        // call <__print>
        const size_t pos = BS_get_cursor(code_output) + 1;
        uint8_t call[] = {
            0xe8, 0x00, 0x00, 0x00, 0x00, //          call <>
        };
        BS_write_array(code_output, sizeof(call), call);
        FCPL_register_patch(&function_patch_list, (FunctionCallPatch){
            .name = SV_from_string_len("__print", 7),
            .offset = pos,
            .relative = true,
            .bit_size = 4,
            .is_local = true,
        });

    }
    get_semicolon(view);
}

#define MAX_ARGS 6

void get_inlined_function_call_stmt_intern(StringViewListView* restrict view, const CompilerTarget target, const Function f, const StringView* restrict f_name) {
    // save local variables tail
    const size_t var_table_length_snapshot = var_table_length;

    const FunctionInlineInst inl = *f.inlining;

    // printf("[DEBUG] call of function '"SV_format"' was inlined\n", SV_p_args(f_name));

    // get and prepare args
    uint8_t expected_args = f.arg_count;
    if (expected_args > MAX_ARGS) {
        srcmap_error(&source_map, f_name->start, "function '"SV_format"' declared with %i arguments, but only at most %i are supported", SV_p_args(f_name), f.arg_count, MAX_ARGS);
        exit(1);
    }

    size_t i = 0;
    while (expected_args > 0) {
        StringView inspect = SVLV_inspect_back(view);
        if (SV_pv_cmp_eq(&inspect, ")", 1)) {
            srcmap_error(&source_map, f_name->start, "unexpected ')' in function '"SV_format"' call, expected another %u argument%c", SV_p_args(f_name), expected_args, expected_args > 1 ? 's' : ' ');
            exit(1);
        }


        const Loc arg_val = get_int_expr(view, target, true);
        if (arg_val.kind == LOC_IMMEDIATE && compiler_flags.constant_variable_resolution) {
            declare_var(inl.args.array[i], VM_CONST, arg_val.value);
        } else {
            emit_mov_eax(code_output, arg_val, &global_patch_list, target);
            const int slot = declare_var(inl.args.array[i], VM_RUNTIME, 0);

            // mov [rbp - slot], eax
            emit_mov_slot_eax(code_output, slot);
        }


        expected_args--;
        if (expected_args > 0) {
            StringView comma = SVLV_consume_one(view);
            if (!SV_pv_cmp_eq(&comma, ",", 1)) {
                srcmap_error(&source_map, comma.start, "expected ',' function call args, got '"SV_format"'", SV_v_args(comma));
                exit(1);
            }
        }
        i += 1;
    }

    const StringView rbracket = SVLV_consume_one(view);
    if (!SV_pv_cmp_eq(&rbracket, ")", 1)) {
        srcmap_error(&source_map, rbracket.start, "in function '"SV_format"' call, expected ')' function call after args, got '"SV_format"'", SV_p_args(f_name), SV_v_args(rbracket));
        exit(1);
    }


    StringViewListView block_view = inl.inlining_block_start;

    const size_t original_function_end_id = function_end_id;

    function_end_id = label_cnt++;

    // compile inline part
    get_stmt_block(&block_view, f.returns_value, target, true);

    // define fn_end_<id>
    LL_push(&labels, (Label){.offset = BS_get_cursor(code_output), .id = function_end_id, .kind = REL_END});

    function_end_id = original_function_end_id;

    // restore local variables
    var_table_length = var_table_length_snapshot;

    // retrieve return value
    if (f.returns_value) {
        const StringView semi_or_into = SVLV_inspect_back(view);
        if (SV_pv_cmp_eq(&semi_or_into, ";", 1)) {
            get_semicolon(view);
        } else if (SV_pv_cmp_eq(&semi_or_into, "into", 4)) {
            SVLV_consume_one(view); // into
            const StringView into_var = SVLV_consume_one(view);

            VarEntry* var = get_var(into_var);
            materialize_const_var(var, false);

            const int slot = lookup_var(into_var);

            // MOV [rbp - slot], eax
            emit_mov_slot_eax(code_output, slot);

            get_semicolon(view);
        } else {
            srcmap_error(&source_map, semi_or_into.start, "expected either ';' or 'into' after function call, got '"SV_format"'", SV_v_args(semi_or_into));
            exit(1);
        }
    } else {
        get_semicolon(view);
    }

}

void get_function_call_stmt(StringViewListView* restrict view, const CompilerTarget target, const StringView* restrict call_kw) {
    const StringView f_name = SVLV_consume_one(view);
    if (!FR_has_function(&functions_registry, f_name)) {
        srcmap_error(&source_map, f_name.start, "function '"SV_format"' not found", SV_v_args(f_name));
        exit(1);
    }
    const Function f = FR_lookup_function(&functions_registry, f_name);

    const StringView lbracket = SVLV_consume_one(view);
    if (!SV_pv_cmp_eq(&lbracket, "(", 1)) {
        srcmap_error(&source_map, lbracket.start, "expected '(' after function name in function call, got '"SV_format"'", SV_v_args(lbracket));
        exit(1);
    }

    if (compiler_flags.function_inlining && f.can_be_inlined && !f.is_just_predef) {
        get_inlined_function_call_stmt_intern(view, target, f, &f_name);
        return;
    }

    // the function calling this can longer be a inlining candidate
    function_inline_candidate = false;


    uint8_t expected_args = f.arg_count;
    if (expected_args > MAX_ARGS) {
        srcmap_error(&source_map, f_name.start, "function '"SV_format"' declared with %i arguments, but only at most %i are supported", SV_v_args(f_name), f.arg_count, MAX_ARGS);
        exit(1);
    }
    int* args_items = malloc(sizeof(int) * f.arg_count);

    while (expected_args > 0) {
        StringView inspect = SVLV_inspect_back(view);
        if (SV_pv_cmp_eq(&inspect, ")", 1)) {
            srcmap_error(&source_map, f_name.start, "unexpected ')' in function '"SV_format"' call, expected another %u argument%c",SV_v_args(f_name), expected_args, expected_args > 1 ? 's' : ' ');
            exit(1);
        }
        const int slot = alloc_stack_slot(&frame); // alloc_tmp_stack_slot
        args_items[f.arg_count - expected_args] = slot;

        const Loc last = get_int_expr(view, target, true);

        if (last.kind == LOC_IMMEDIATE) {
             emit_mov_slot_imm32(code_output, slot, last.value);
        } else {
            // move last into eax
            emit_mov_eax(code_output, last, &global_patch_list, target);

            // MOV [rbp - slot], eax
            emit_mov_slot_eax(code_output, slot);
        }

        expected_args--;
        if (expected_args > 0) {
            StringView comma = SVLV_consume_one(view);
            if (!SV_pv_cmp_eq(&comma, ",", 1)) {
                srcmap_error(&source_map, comma.start, "expected ',' function call args, got '"SV_format"'", SV_v_args(comma));
                exit(1);
            }
        }
    }

    const StringView rbracket = SVLV_consume_one(view);
    if (!SV_pv_cmp_eq(&rbracket, ")", 1)) {
        srcmap_error(&source_map, rbracket.start, "in function '"SV_format"' call, expected ')' function call after args, got '"SV_format"'", SV_v_args(f_name), SV_v_args(rbracket));
        exit(1);
    }

    // edi, esi, edx, ecx, r8d, r9d
    static const uint8_t arg_regs_modrm[] = {0x7D, 0x75, 0x55, 0x4D, 0x00, 0x00};
    // No REX prefix for edi/esi/edx/ecx; REX.R (0x44) needed for r8d/r9d
    static const uint8_t arg_regs_r8d_modrm[] = {0x45, 0x4D};

    for (int i = 0; i < f.arg_count; i++) {
        const int slot = args_items[i];

        if (i < 4) {
            // MOV edi/esi/edx/ecx, [rbp - slot]
            BS_write(code_output, 0x8B);

            if (slot <= 128) {
                BS_write(code_output, arg_regs_modrm[i]);
                BS_write(code_output, (uint8_t)(-slot));
            } else {
                const int32_t disp = -slot;
                BS_write(code_output, arg_regs_modrm[i] + 0x40); // Convert disp8 ModRM to disp32
                BS_write(code_output, (uint8_t)(disp & 0xFF));
                BS_write(code_output, (uint8_t)((disp >> 8) & 0xFF));
                BS_write(code_output, (uint8_t)((disp >> 16) & 0xFF));
                BS_write(code_output, (uint8_t)((disp >> 24) & 0xFF));
            }
        } else {
            // MOV r8d/r9d, [rbp - slot]
            BS_write(code_output, 0x44); // REX.R prefix
            BS_write(code_output, 0x8B);

            if (slot <= 128) {
                BS_write(code_output, arg_regs_r8d_modrm[i - 4]);
                BS_write(code_output, (uint8_t)(-slot));
            } else {
                int32_t disp = -slot;
                BS_write(code_output, arg_regs_r8d_modrm[i - 4] + 0x40); // Convert disp8 ModRM to disp32
                BS_write(code_output, (uint8_t)(disp & 0xFF));
                BS_write(code_output, (uint8_t)((disp >> 8) & 0xFF));
                BS_write(code_output, (uint8_t)((disp >> 16) & 0xFF));
                BS_write(code_output, (uint8_t)((disp >> 24) & 0xFF));
            }
        }
    }

    free(args_items);


    const size_t pos = BS_get_cursor(code_output) + 1;
    uint8_t call[] = {
        0xe8, 0x00, 0x00, 0x00, 0x00, //          call <>
    };
    BS_write_array(code_output, sizeof(call), call);
    FCPL_register_patch(&function_patch_list, (FunctionCallPatch){
                           .name = f.name,
                           .offset = pos,
                           .relative = true,
                           .bit_size = 4,
                           .is_local = true,
    });


    if (f.returns_value) {
        const StringView semi_or_into = SVLV_inspect_back(view);
        if (SV_pv_cmp_eq(&semi_or_into, ";", 1)) {
            get_semicolon(view);
            srcmap_warn(&source_map, call_kw->start, "discarding return value of function");
        } else if (SV_pv_cmp_eq(&semi_or_into, "into", 4)) {
            SVLV_consume_one(view); // into
            const StringView into_var = SVLV_consume_one(view);

            VarEntry* var = get_var(into_var);
            materialize_const_var(var, false);

            const int slot = lookup_var(into_var);

            // MOV [rbp - slot], eax
            emit_mov_slot_eax(code_output, slot);

            get_semicolon(view);
        } else {
            srcmap_error(&source_map, semi_or_into.start, "expected either ';' or 'into' after function call, got '"SV_format"'", SV_v_args(semi_or_into));
            exit(1);
        }
    } else {
        get_semicolon(view);
    }
}

void get_stmt(StringViewListView* view, const bool should_return_value, const CompilerTarget target, const bool in_inlining) {
    const StringView s1 = SVLV_consume_one(view);
    if (SV_pv_cmp_eq(&s1, "int", 3)) {
        get_int_var_dec(view, target);
    } else if (SV_pv_cmp_eq(&s1, "set", 3)) {
        get_int_var_set(view, target);
    } else if (SV_pv_cmp_eq(&s1, "if", 2)) {
        get_if_conditional(view, should_return_value, target, in_inlining);
    } else if (SV_pv_cmp_eq(&s1, "while", 5)) {
        materialize_const_variables();
        get_while_conditional(view, should_return_value, target, in_inlining);
    } else if (SV_pv_cmp_eq(&s1, "print", 5)) {
        get_print_stmt(view, target);
    } else if (SV_pv_cmp_eq(&s1, "call", 4)) {
        get_function_call_stmt(view, target, &s1);
    } else {
        srcmap_error(&source_map, s1.start, "unknown start of statement");
        exit(1);
    }
}

void get_function(StringViewListView* view, const CompilerTarget target) {
    const size_t f_start_cursor = BS_get_cursor(code_output);

    create_frame();

    function_end_id = label_cnt++;
    function_inline_candidate = true;


    const StringView ret_type = SVLV_consume_one(view);
    bool returns_value = false;
    if (SV_pv_cmp_eq(&ret_type, "int", 3)) {
        returns_value = true;
    } else if (SV_pv_cmp_eq(&ret_type, "void", 4)) {
    } else {
        srcmap_error(&source_map, ret_type.start, "expected either 'int' or 'void' as return type, got '"SV_format"'", SV_v_args(ret_type));
        exit(1);
    }

    const StringView f_name = SVLV_consume_one(view);

    bool has_predef = false;
    if (FR_has_function(&functions_registry, f_name)) {
        const Function f = FR_lookup_function(&functions_registry, f_name);
        if (!f.is_just_predef) {
            srcmap_error(&source_map, f_name.start, "function with name '"SV_format"' already exists", SV_v_args(f_name));
            exit(1);
        }
        has_predef = true;
    }

    if (SV_pv_cmp_eq(&f_name, "main", 4)) {
        function_inline_candidate = false;
    }

    const StringView f_args_start = SVLV_consume_one(view);
    if (!SV_pv_cmp_eq(&f_args_start, "(", 1)) {
        srcmap_error(&source_map, f_args_start.start, "expected '(', got '"SV_format"'", SV_v_args(f_args_start));
        exit(1);
    }

    StringViewList f_args = SVL_new();
    while (true) {
        StringView s = SVLV_consume_one(view);
        if (SV_pv_cmp_eq(&s, ")", 1)) {
            break;
        }
        SVL_p_push(&f_args, s);

        StringView comma = SVLV_consume_one(view);
        if (SV_pv_cmp_eq(&comma, ")", 1)) {
            break;
        } else if (SV_pv_cmp_eq(&comma, ",", 1)) {
            // ok
        } else {
            srcmap_error(&source_map, comma.start, "expected ',' or ')', got '"SV_format"'", SV_v_args(comma));
            exit(1);
        }
    }

    // temp write
    {
        const Function f = (Function){
            .name = f_name,
            .arg_count = f_args.len,
            .offset = f_start_cursor,
            .code_size = 0,
            .returns_value = returns_value,
            .is_just_predef = true,
        };
        if(has_predef) {
            FR_overwrite_function(&functions_registry, f);
        } else {
            FR_register_function(&functions_registry, f);
        }
    }



    // emit stack preparation for frame
    uint8_t prologue[] = {
        0x55, // push rbp
        0x48, 0x89, 0xE5, // mov rbp, rsp
        // 0x48, 0x83, 0xEC, 0xFF // sub rsp, N
        0x48, 0x81, 0xEC, 0x00, 0x00, 0x00, 0x00, // sub rsp, N
    };
    BS_write_array(code_output, sizeof(prologue), prologue);
    const size_t prologue_frame_override_pos = BS_get_cursor(code_output) - 4;


    static const uint8_t param_store_modrm[] = {0x7D, 0x75, 0x55, 0x4D, 0x45, 0x4D}; // rdi,rsi,rdx,rcx,r8,r9

    for (size_t i = 0; i < f_args.len; i++) {
        const int slot = declare_var(f_args.array[i], VM_RUNTIME, 0); // same as local var

        // MOV [rbp - slot], rdi/rsi/...

        // REX prefix for r8, r9
        if (i == 4 || i == 5) {
            BS_write(code_output, 0x44);
        }
        BS_write(code_output, 0x89);

        // BS_write(code_output, param_store_modrm[i]);
        // BS_write(code_output, (uint8_t) (-slot));
        if (slot <= 128) {
            // 8-bit displacement path
            BS_write(code_output, param_store_modrm[i]);
            BS_write(code_output, (uint8_t)(-slot));
        } else { // should be unnecessary, but just in case
            // 32-bit displacement path
            const int32_t disp = -slot;

            BS_write(code_output, param_store_modrm[i] + 0x40); // Convert disp8 ModRM to disp32

            // Write 32-bit negative displacement (Little-Endian)
            BS_write(code_output, (uint8_t)(disp & 0xFF));
            BS_write(code_output, (uint8_t)((disp >> 8) & 0xFF));
            BS_write(code_output, (uint8_t)((disp >> 16) & 0xFF));
            BS_write(code_output, (uint8_t)((disp >> 24) & 0xFF));
        }
    }

    const uint8_t args_count = f_args.len;


    const StringViewListView f_block = *view;
    get_stmt_block(view, returns_value, target, false);

    const StringView f_end = SVLV_consume_one(view);
    if (!SV_pv_cmp_eq(&f_end, "end", 3)) {
        srcmap_error(&source_map, f_end.start, "expected 'end', got '"SV_format"'", SV_v_args(f_end));
        exit(1);
    }

    // define fn_end_<id>
    LL_push(&labels, (Label){.offset = BS_get_cursor(code_output), .id = function_end_id, .kind = REL_END});

    const size_t cursor_snap = BS_get_cursor(code_output);
    BS_set_cursor(code_output, prologue_frame_override_pos);
    // BS_write(code_output, (uint8_t) ((get_stack_frame_max(get_frame()) + 15) & ~15));
    const uint32_t off_val = (get_stack_frame_max(get_frame()) + 15) & ~15;
    BS_write(code_output, (off_val >>  0) & 0xFF);
    BS_write(code_output, (off_val >>  8) & 0xFF);
    BS_write(code_output, (off_val >> 16) & 0xFF);
    BS_write(code_output, (off_val >> 24) & 0xFF);
    BS_set_cursor(code_output, cursor_snap);


    // function_end_id

    uint8_t epilogue_add[] = { 0x48, 0x81, 0xC4 };
    BS_write_array(code_output, sizeof(epilogue_add), epilogue_add);
    BS_write(code_output, (off_val >>  0) & 0xFF);
    BS_write(code_output, (off_val >>  8) & 0xFF);
    BS_write(code_output, (off_val >> 16) & 0xFF);
    BS_write(code_output, (off_val >> 24) & 0xFF);
    uint8_t epilogue[] = {
        // 0x48, 0x83, 0xC4, (uint8_t) ((get_stack_frame_max(get_frame()) + 15) & ~15), // add rsp, N
        0x5D, // pop rbp
        0xC3, // ret
    };
    BS_write_array(code_output, sizeof(epilogue), epilogue);

    const size_t f_end_cursor = BS_get_cursor(code_output);


    if (function_inline_candidate && compiler_flags.function_inlining) {
        FunctionInlineInst* fii = malloc(sizeof(FunctionInlineInst));
        assert(fii != NULL);

        if (content_ptrs_len + 2 > content_ptrs_cap) {
            content_ptrs_cap *= 2;
            void** new_array = realloc(content_ptrs, content_ptrs_cap * sizeof(void*));
            assert(new_array != NULL);
            content_ptrs = new_array;
        }
        content_ptrs[content_ptrs_len++] = (void*)fii;
        content_ptrs[content_ptrs_len++] = (void*)f_args.array;

        fii->args = f_args;
        fii->inlining_block_start = f_block;

        FR_overwrite_function(&functions_registry, (Function){
                              .name = f_name,
                              .arg_count = args_count,
                              .offset = f_start_cursor,
                              .code_size = f_end_cursor - f_start_cursor,
                              .returns_value = returns_value,
                              .can_be_inlined = true,
                              .inlining = fii,
                              .is_just_predef = false,
        });
    } else {
        SVL_p_free(&f_args);
        FR_overwrite_function(&functions_registry, (Function){
                              .name = f_name,
                              .arg_count = args_count,
                              .offset = f_start_cursor,
                              .code_size = f_end_cursor - f_start_cursor,
                              .returns_value = returns_value,
                              .can_be_inlined = false,
                              .inlining = NULL,
                              .is_just_predef = false,
        });
    }

    resolve_frame();
    // printf("[INFO] compiled function '%.*s'\n", (int)f_name.len, f_name.start);
}


void create_frame(void) {
    relocations = RL_new();
    assert(relocations.array != NULL);

    labels = LL_new();
    assert(labels.array != NULL);

    // reset frame
    frame = (StackFrame){.next_offset = 4};

    // reset var registry
    var_table_length = 0;
    var_table_cap = 4;
    var_table = malloc(sizeof(VarEntry) * var_table_cap);
}

void resolve_frame(void) {
    var_table_length = 0;
    resolve_relocations(code_output, &relocations, &labels);
    free(var_table);
    LL_free(&labels);
    RL_free(&relocations);
}

bool is_include_next(StringViewListView* list) {
    if (SVLV_is_empty(list)) return false;

    const StringView sv = SVLV_inspect_back(list);
    if (SV_pv_cmp_eq(&sv, "include", 7)) return true;
    return false;
}

bool is_quoted_string(const StringView* sv) {
    return sv->len >= 2 && sv->start[0] == '"' && sv->start[sv->len - 1] == '"';
}

void compile(StringViewListView*, const StringView* current_source_file, CompilerTarget, StringView original_text);

void resolve_import(StringViewListView* list, const StringView* current_source_file, const CompilerTarget target) {
    SVLV_consume_one(list); // include KW
    const StringView path = SVLV_consume_one(list);
    if (!is_quoted_string(&path)) {
        srcmap_error(&source_map, path.start, "include excepted quoted string");
        exit(1);
    }

    get_semicolon(list);

    // parsing import stmt finished at this point, source_map is later overwritten by compile call

    StringView path2 = path;
    path2.start++;
    path2.len -= 2;

    const char* path_c = SV_to_c_string(&path2);
    const char* source_path_c = SV_to_c_string(current_source_file);

    char resolved_path_raw[PATH_MAX];

    resolve_import_path(source_path_c, path_c, resolved_path_raw);

    const StringView resolved_path = SV_from_string(resolved_path_raw);

    for (size_t n = 0; n < import_table.len; n++) {
        if (SV_pp_cmp_eq(&import_table.array[n], &resolved_path)) {
            return;
        }
    }

    SVL_p_push(&import_table, resolved_path);

    size_t file_size;
    char* content_raw = read_file(resolved_path_raw, &file_size);
    const StringView content = SV_from_string_len(content_raw, file_size);

    StringViewList svl = p_tokenize(&content);
    StringViewListView svlv = SVLV_from_SVL(&svl);

    free((void *) path_c);
    free((void *) source_path_c);

    compile(&svlv, &resolved_path, target, content);

    SVL_p_free(&svl);

    if (content_ptrs_len + 1 > content_ptrs_cap) {
        content_ptrs_cap *= 2;
        void** new_array = realloc(content_ptrs, content_ptrs_cap * sizeof(void*));
        assert(new_array != NULL);
        content_ptrs = new_array;
    }
    content_ptrs[content_ptrs_len++] = content_raw;
}

bool is_global_next(StringViewListView* list) {
    if (SVLV_is_empty(list)) return false;

    const StringView sv = SVLV_inspect_back(list);
    if (SV_pv_cmp_eq(&sv, "global", 6)) return true;
    return false;
}

bool is_predef_next(StringViewListView* list) {
    if (SVLV_is_empty(list)) return false;

    const StringView sv = SVLV_inspect_back(list);
    if (SV_pv_cmp_eq(&sv, "declare", 7)) return true;
    return false;
}

void set_code_target_buffer_globals(void) {
    code_output = &code_output_globals;
}

void set_code_target_buffer_funcs(void) {
    code_output = &code_output_funcs;
}


void get_global_var(StringViewListView* view, const CompilerTarget target) {
    set_code_target_buffer_globals();
    SVLV_consume_one(view); // for `global`
    const StringView type = SVLV_consume_one(view);
    if (!SV_pv_cmp_eq(&type, "int", 3)) {
        srcmap_error(&source_map, type.start, "globals can only be of type 'int', got '"SV_format"'", SV_v_args(type));
        exit(1);
    }
    StringView name = SVLV_consume_one(view);
    StringView assign = SVLV_consume_one(view);
    if (!SV_pv_cmp_eq(&assign, ":", 1)) {
        srcmap_error(&source_map, assign.start, "expected ':' after name in global variable declaration, got '"SV_format"'", SV_v_args(assign));
        exit(1);
    }
    // printf("in GLOBAL: var_table_length = %lu\n", var_table_length);
    const Loc last = get_int_expr(view, target, false); // this generates instructions

    // move last into eax
    emit_mov_eax(code_output, last, &global_patch_list_initializer, target);

    switch (target) {
        case F_Elf64: {
            const size_t pos = BS_get_cursor(code_output) + 3;
            uint8_t store[] = {
                0x89, 0x04, 0x25, 0x00, 0x00, 0x00, 0x00, // mov [addr32], eax
            };
            BS_write_array(code_output, sizeof(store), store);


            if (GR_has_global(&globals_registry, name)) {
                srcmap_error(&source_map, name.start, "redeclaration of global variable '"SV_format"'", SV_v_args(name));
                exit(1);
            }

            size_t globals_index = GR_register_global(&globals_registry, (Global){
                                                          .name = name,
                                                          .bss_offset = (int)alloc_bss_space(4)
                                                      });

            GPL_register_patch(&global_patch_list_initializer, (GlobalPatch){
                                   .index = globals_index,
                                   .offset = pos,
                                   .relative = false,
                                   .bit_size = 4
                               });
            break;
        }
        case F_Win64: {
            const size_t pos = BS_get_cursor(code_output) + 2;
            uint8_t store[] = {
                0x48, 0xB9, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // mov rcx, imm64
                0x89, 0x01, // mov [rcx], eax
            };
            BS_write_array(code_output, sizeof(store), store);

            if (GR_has_global(&globals_registry, name)) {
                srcmap_error(&source_map, name.start, "redeclaration of global variable '"SV_format"'", SV_v_args(name));
                exit(1);
            }

            const size_t globals_index = GR_register_global(&globals_registry, (Global){
                .name = name,
                .bss_offset = (int)alloc_bss_space(4)
            });

            GPL_register_patch(&global_patch_list_initializer, (GlobalPatch){
                .index = globals_index,
                .offset = pos,
                .relative = false,
                .bit_size = 8
            });

            break;
        }
    }

    get_semicolon(view);

    set_code_target_buffer_funcs();
}

void get_function_predef(StringViewListView* view) {
    const size_t f_start_cursor = BS_get_cursor(code_output);
    SVLV_consume_one(view); // declare
    const StringView ret_type = SVLV_consume_one(view);
    bool returns_value = false;
    if (SV_pv_cmp_eq(&ret_type, "int", 3)) {
        returns_value = true;
    } else if (SV_pv_cmp_eq(&ret_type, "void", 4)) {
    } else {
        srcmap_error(&source_map, ret_type.start, "expected either 'int' or 'void' as return type, got '"SV_format"'", SV_v_args(ret_type));
        exit(1);
    }

    const StringView f_name = SVLV_consume_one(view);

    if (FR_has_function(&functions_registry, f_name)) {
        srcmap_error(&source_map, f_name.start, "function with name '"SV_format"' already exists", SV_v_args(f_name));
        exit(1);
    }

    const StringView f_args_start = SVLV_consume_one(view);
    if (!SV_pv_cmp_eq(&f_args_start, "(", 1)) {
        srcmap_error(&source_map, f_args_start.start, "expected '(', got '"SV_format"'", SV_v_args(f_args_start));
        exit(1);
    }

    // StringViewList f_args = SVL_new();
    uint32_t arg_count = 0;
    while (true) {
        StringView s = SVLV_consume_one(view);
        if (SV_pv_cmp_eq(&s, ")", 1)) {
            break;
        }
        arg_count += 1;

        StringView comma = SVLV_consume_one(view);
        if (SV_pv_cmp_eq(&comma, ")", 1)) {
            break;
        } else if (SV_pv_cmp_eq(&comma, ",", 1)) {
            // ok
        } else {
            srcmap_error(&source_map, comma.start, "expected ',' or ')', got '"SV_format"'", SV_v_args(comma));
            exit(1);
        }
    }

    FR_register_function(&functions_registry, (Function){
        .name = f_name,
        .arg_count = arg_count,
        .offset = f_start_cursor,
        .code_size = 0,
        .returns_value = returns_value,
        .is_just_predef = true,
    });



    get_semicolon(view);
}

void compile(StringViewListView* list, const StringView* current_source_file, const CompilerTarget target, const StringView original_text) {

    source_map = (SourceMap) {
        .src_start = original_text.start,
        .src_len = original_text.len,
        .filename = *current_source_file,
    };
    const SourceMap scp = source_map;
    var_table_length = 0;

    while (is_include_next(list)) {
        source_map = scp;
        resolve_import(list, current_source_file, target);
    }

    source_map = scp;


    while (list->len > 0) {
        if (is_global_next(list)) {
            get_global_var(list, target);
        } else if (is_predef_next(list)){
            get_function_predef(list);
        } else {
            get_function(list, target);
        }
    }
}

void process_elf64(const StringView* input, const StringView* current_source_file, const char* output_filepath);

void process_win64(const StringView* input, const StringView* current_source_file, const char* output_filepath);

void process(const StringView* restrict input, const StringView* restrict current_source_file, const char* restrict output_filepath, const CompilerTarget target) {
    code_output_funcs = BS_new();
    code_output_globals = BS_new();
    code_output = &code_output_funcs;
    assert(code_output_funcs.array != NULL);
    assert(code_output_globals.array != NULL);

    bss_alloc_next = 0;
    label_cnt = 0;

    globals_registry = GR_new();
    global_patch_list = GPL_new();
    global_patch_list_initializer = GPL_new();

    content_ptrs = malloc(sizeof(char *) * content_ptrs_cap);
    content_ptrs_len = 0;

    switch (target) {
        case F_Elf64:
            process_elf64(input, current_source_file, output_filepath);
            break;
        case F_Win64:
            process_win64(input, current_source_file, output_filepath);
            break;
    }

    BS_free(&code_output_funcs);
    BS_free(&code_output_globals);

    GR_free(&globals_registry);
    GPL_free(&global_patch_list);
    GPL_free(&global_patch_list_initializer);

    for (size_t n = 0; n < content_ptrs_len; n++) {
        free(content_ptrs[n]);
    }
    free(content_ptrs);
}

void process_elf64(const StringView* restrict input, const StringView* restrict current_source_file, const char* restrict output_filepath) {
    const uint64_t code_load_addr = 0x400000;
    const uint64_t str_consts_load_addr = 0x800000;
    const uint64_t bss_load_addr = 0x600000;


    functions_registry = FR_new();
    function_patch_list = FCPL_new();

    import_table = SVL_new();

    SVL_p_push(&import_table, *current_source_file);


    string_consts = SVL_new();
    string_consts_relocations = SCARL_new();


    StringViewList svl = p_tokenize(input);

    StringViewListView svlv = SVLV_from_SVL(&svl);


    size_t entry_point_patch_cursor = BS_get_cursor(code_output) + 6;
    size_t globals_patch_cursor = BS_get_cursor(code_output) + 1;
    uint8_t entry_point[] = {
        0xe8, 0x00, 0x00, 0x00, 0x00, //          call   5 <_globals+0x5>
        0xe8, 0x00, 0x00, 0x00, 0x00, //          call   5 <_main+0x5> // main
        0x48, 0x89, 0xc7, //          mov    rdi,rax
        0x48, 0xc7, 0xc0, 0x3c, 0x00, 0x00, 0x00, //         mov    rax,0x3c
        0x0f, 0x05, //                   syscall
    };
    BS_write_array(code_output, sizeof(entry_point), entry_point);
    FCPL_register_patch(&function_patch_list, (FunctionCallPatch){
                           .name = SV_from_string_len("main", 4),
                           .offset = entry_point_patch_cursor,
                           .relative = true,
                           .bit_size = 4,
        .is_local = true,
                       });
    FCPL_register_patch(&function_patch_list, (FunctionCallPatch){
                           .name = SV_from_string_len("__global", 8),
                           .offset = globals_patch_cursor,
                           .relative = true,
                           .bit_size = 4,
        .is_local = true,
                       });

    const StringView runtime_error_str = SV_from_string_len("runtime error: ", 15);
    const size_t runtime_error_str_index = add_str_const(runtime_error_str);

    const StringView runtime_error__div_zero_str = SV_from_string_len("division by zero\n", 17);
    const size_t runtime_error__div_zero_str_index = add_str_const(runtime_error__div_zero_str);

    uint8_t rt_zero_div_handler[] = {
        0x48, 0xc7, 0xc0, 0x01, 0x00, 0x00, 0x00, // mov    rax,0x1
        0x48, 0xc7, 0xc7, 0x01, 0x00, 0x00, 0x00, // mov    rdi,0x1
        0x48, 0xc7, 0xc6, 0x00, 0x00, 0x00, 0x00, // mov    rsi,0x00000000 ; addr
        0x48, 0xc7, 0xc2, (uint8_t) (runtime_error_str.len), 0x00, 0x00, 0x00, // mov    rdx,10 ; len
        0x0f, 0x05, // syscall ; write

        0x48, 0xc7, 0xc0, 0x01, 0x00, 0x00, 0x00, // mov    rax,0x1
        0x48, 0xc7, 0xc7, 0x01, 0x00, 0x00, 0x00, // mov    rdi,0x1
        0x48, 0xc7, 0xc6, 0x00, 0x00, 0x00, 0x00, // mov    rsi,0x00000000 ; addr
        0x48, 0xc7, 0xc2, (uint8_t) (runtime_error__div_zero_str.len), 0x00, 0x00, 0x00, // mov    rdx,10 ; len
        0x0f, 0x05, // syscall ; write

        0x48, 0xC7, 0xC7, 0x01, 0x00, 0x00, 0x00, // mov    rdi, 1 ; exit code
        0x48, 0xC7, 0xC0, 0x3C, 0x00, 0x00, 0x00, // mov    rax, 60 ; exit syscall number
        0x0F, 0x05 // syscall
    };
    BS_write_array(code_output, sizeof(rt_zero_div_handler), rt_zero_div_handler);
    SCARL_push(&string_consts_relocations, (StringConstAddrRelocation){
                   .const_index = runtime_error_str_index,
                   .patch_offset = sizeof(entry_point) + 7 + 7 + 3, // offset for string in .rodata
                   .bit_size = 4,
               });
    SCARL_push(&string_consts_relocations, (StringConstAddrRelocation){
                   .const_index = runtime_error__div_zero_str_index,
                   .patch_offset = sizeof(entry_point) + 7 + 7 + 7 + 7 + 2 + 7 + 7 + 3, // offset for string in .rodata
                   .bit_size = 4,
               });


    // __print
    // expects addr of char in RAX
    uint8_t print_char_util[] = {
        0x48, 0x89, 0xC6, // mov rsi, rax
        0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00, // mov rax, 1  (sys_write)
        0x48, 0xC7, 0xC7, 0x01, 0x00, 0x00, 0x00, // mov rdi, 1  (stdout)

        0x48, 0x89, 0xDA, // mov    rdx,rbx (len)
        // 0x48, 0xC7, 0xC2, 0x01, 0x00, 0x00, 0x00, // mov rdx, 1  (len)
        0x0F, 0x05, // syscall
        0xC3, // ret
    };
    BS_write_array(code_output, sizeof(print_char_util), print_char_util);




    // emit stack preparation for _global frame
    uint8_t globals_prologue[] = {
        0x55, // push rbp
        0x48, 0x89, 0xE5, // mov rbp, rsp
        // 0x48, 0x83, 0xEC, 0xFF // sub rsp, N
        0x48, 0x81, 0xEC, 0x00, 0x00, 0x00, 0x00, // sub rsp, N
    };
    BS_write_array(&code_output_globals, sizeof(globals_prologue), globals_prologue);
    const size_t globals_frame_override_pos = BS_get_cursor(&code_output_globals) - 4;


    compile(&svlv, current_source_file, F_Elf64, *input);

    const size_t globals_init_cursor_snap = BS_get_cursor(&code_output_globals);
    BS_set_cursor(&code_output_globals, globals_frame_override_pos);
    // BS_write(&code_output_globals, (uint8_t) ((global_frame.next_offset + 15) & ~15));
    const uint32_t off_val = (get_stack_frame_max(get_frame()) + 15) & ~15;
    BS_write(&code_output_globals, (off_val >>  0) & 0xFF);
    BS_write(&code_output_globals, (off_val >>  8) & 0xFF);
    BS_write(&code_output_globals, (off_val >> 16) & 0xFF);
    BS_write(&code_output_globals, (off_val >> 24) & 0xFF);

    BS_set_cursor(&code_output_globals, globals_init_cursor_snap);


    uint8_t globals_epilogue_add[] = { 0x48, 0x81, 0xC4 };
    BS_write_array(&code_output_globals, sizeof(globals_epilogue_add), globals_epilogue_add);
    BS_write(&code_output_globals, (off_val >>  0) & 0xFF);
    BS_write(&code_output_globals, (off_val >>  8) & 0xFF);
    BS_write(&code_output_globals, (off_val >> 16) & 0xFF);
    BS_write(&code_output_globals, (off_val >> 24) & 0xFF);
    uint8_t globals_epilogue[] = {
        // 0x48, 0x83, 0xC4, (uint8_t) ((global_frame.next_offset + 15) & ~15), // add rsp, N
        0x5D, // pop rbp
        0xC3, // ret
    };
    BS_write_array(&code_output_globals, sizeof(globals_epilogue), globals_epilogue);


    SVL_p_free(&svl);


    resolve_globals(code_output_funcs.array, bss_load_addr, &globals_registry, &global_patch_list);
    resolve_globals(code_output_globals.array, bss_load_addr, &globals_registry, &global_patch_list_initializer);

    size_t global_inicializer_cursor = BS_get_cursor(code_output);

    // BS_write(&code_output_globals, 0xC3); // ret
    BS_write_array(code_output, code_output_globals.len, code_output_globals.array);


    if (!FR_has_function(&functions_registry, SV_from_string_len("main", 4))) {
        fprintf(stderr, "[ERROR] no main function found in the program\n");
        exit(1);
    }
    Function main_fn = FR_lookup_function(&functions_registry, SV_from_string_len("main", 4));
    if (main_fn.arg_count != 0) {
        fprintf(stderr, "[ERROR] main function should have not arguments, but %i were provided\n", main_fn.arg_count);
        exit(1);
    }
    if (main_fn.returns_value == false) {
        fprintf(stderr, "[ERROR] main function should return int, but declared in the program as 'void'\n");
        exit(1);
    }


    FR_register_function(&functions_registry, (Function){
                             .name = SV_from_string_len("__rt_exception__zero_div", 24),
                             .offset = sizeof(entry_point),
                             .code_size = sizeof(rt_zero_div_handler),
                             .returns_value = false,
                             .arg_count = 0,
        .is_just_predef = false,
                         });

    FR_register_function(&functions_registry, (Function){
                             .name = SV_from_string_len("__print", 7),
                             .offset = sizeof(entry_point) + sizeof(rt_zero_div_handler),
                             .code_size = sizeof(print_char_util),
                             .returns_value = false,
                             .arg_count = 0,
        .is_just_predef = false,
                         });


    FR_register_function(&functions_registry, (Function){
                             .name = SV_from_string_len("__global", 8),
                             .offset = global_inicializer_cursor,
                             .code_size = code_output_globals.len,
                             .returns_value = false,
                             .arg_count = 0,
        .is_just_predef = false,
                         });

    resolve_function_calls(code_output->array, code_output_funcs.len - code_output_globals.len, &functions_registry, &function_patch_list);
    FunctionsRegistry new_fr = FR_new();
    FR_register_function(&new_fr, (Function){
                             .name = SV_from_string_len("_start", 6),
                             .offset = 0,
                             .code_size = sizeof(entry_point),
                             .returns_value = false,
                             .arg_count = 0,
        .is_just_predef = false,
                         });

    for (size_t n = 0; n < functions_registry.len; n++) {
        FR_register_function(&new_fr, functions_registry.array[n]);
    }

    size_t total_code_size = code_output->len;


    SCARL_resolve(&string_consts_relocations, code_output->array, str_consts_load_addr, &string_consts);

    write_elf64(output_filepath, code_output->array, total_code_size, code_load_addr, &new_fr, str_consts_load_addr,
                &string_consts, bss_load_addr, bss_alloc_next);


    FR_free(&functions_registry);
    FR_free(&new_fr);
    FCPL_free(&function_patch_list);
    SVL_p_free(&import_table);
    SVL_p_free(&string_consts);
    SCARL_free(&string_consts_relocations);
}


void process_win64(const StringView* restrict input, const StringView* restrict current_source_file, const char* restrict output_filepath) {
    functions_registry = FR_new();
    function_patch_list = FCPL_new();

    import_table = SVL_new();

    SVL_p_push(&import_table, *current_source_file);


    string_consts = SVL_new();
    string_consts_relocations = SCARL_new();

    const uint64_t code_load_addr = 0x140000000;


    StringViewList svl = p_tokenize(input);

    StringViewListView svlv = SVLV_from_SVL(&svl);

    StringViewList importing_funcs = SVL_new();
    SVL_p_push(&importing_funcs, SV_from_string_len("GetStdHandle", 12));
    SVL_p_push(&importing_funcs, SV_from_string_len("WriteFile", 9));
    SVL_p_push(&importing_funcs, SV_from_string_len("ExitProcess", 11));


    size_t entry_point_patch_cursor = BS_get_cursor(code_output) + 6;
    size_t globals_patch_cursor = BS_get_cursor(code_output) + 1;
    uint8_t entry_point[] = {
        0xe8, 0x00, 0x00, 0x00, 0x00, //          call   5 <_globals+0x5>
        0xe8, 0x00, 0x00, 0x00, 0x00, //          call   5 <_main+0x5>

        0x48, 0x83, 0xEC, 0x28, // sub    rsp,0x28

        0x89, 0xC1, // mov ecx, eax
        // 0xB9, 0x00, 0x00, 0x00, 0x00, // mov    ecx,0x0
        0x48, 0xB8, 0x10, 0x10, 0x00, 0x40, 0x01, 0x00, 0x00, 0x00, // movabs rax, 0x140001010 (0x140001000+8+8)
        0xFF, 0x10, // call   QWORD PTR [rax]
    };
    BS_write_array(code_output, sizeof(entry_point), entry_point);
    FCPL_register_patch(&function_patch_list, (FunctionCallPatch){
                           .name = SV_from_string_len("main", 4),
                           .offset = entry_point_patch_cursor,
                           .relative = true,
                           .bit_size = 4,
                            .is_local = true,
                       });
    FCPL_register_patch(&function_patch_list, (FunctionCallPatch){
                           .name = SV_from_string_len("__global", 8),
                           .offset = globals_patch_cursor,
                           .relative = true,
                           .bit_size = 4,
        .is_local = true,
                       });

    StringView runtime_error_str = SV_from_string_len("runtime error: ", 15);
    size_t runtime_error_str_index = add_str_const(runtime_error_str);

    StringView runtime_error__div_zero_str = SV_from_string_len("division by zero\n", 17);
    size_t runtime_error__div_zero_str_index = add_str_const(runtime_error__div_zero_str);

    uint8_t rt_zero_div_handler[] = {
        0x48, 0x83, 0xEC, 0x38, // sub    rsp,0x38

        /*
            48 c7 c1 f5 ff ff ff    mov    rcx,0xfffffffffffffff5
            48 b8 00 10 00 40 01    movabs rax,0x140001000
            00 00 00
            ff 10                   call   QWORD PTR [rax]
            49 89 c4                mov    r12,rax; get std handle
        */
        0x48, 0xC7, 0xC1, 0xF5, 0xFF, 0xFF, 0xFF, 0x48, 0xB8, 0x00, 0x10, 0x00, 0x40, 0x01, 0x00, 0x00,
        0x00, 0xFF, 0x10, 0x49, 0x89, 0xC4,



        // PRINT
        // movabs rdx, 0x00000000 -> patched by const_addr
        0x48, 0xBA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /*
        mov    QWORD PTR [rsp+0x20],0x0
        lea    r9,[rsp+0x28]
        mov    rcx,r12
        mov    r8, len
        movabs rax,0x140001008
        call   QWORD PTR [rax]
         */
        0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00,
        0x4C, 0x8D, 0x4C, 0x24, 0x28,
        0x4C, 0x89, 0xE1,
        0x49, 0xc7, 0xc0, (uint8_t) (runtime_error_str.len), 0x00, 0x00, 0x00,
        0x48, 0xB8, 0x08, 0x10, 0x00, 0x40, 0x01, 0x00, 0x00, 0x00,
        0xFF, 0x10,



        // PRINT
        // movabs rdx, 0x00000000 -> patched by const_addr
        0x48, 0xBA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        /*
        mov    QWORD PTR [rsp+0x20],0x0
        lea    r9,[rsp+0x28]
        mov    rcx,r12
        mov    r8, len
        movabs rax,0x140001008
        call   QWORD PTR [rax]
         */
        0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00,
        0x4C, 0x8D, 0x4C, 0x24, 0x28,
        0x4C, 0x89, 0xE1,
        0x49, 0xc7, 0xc0, (uint8_t) (runtime_error__div_zero_str.len), 0x00, 0x00, 0x00,
        0x48, 0xB8, 0x08, 0x10, 0x00, 0x40, 0x01, 0x00, 0x00, 0x00,
        0xFF, 0x10,


        0x48, 0x83, 0xEC, 0x28, // sub    rsp,0x28
        // EXIT
        // 0x89, 0xC1, // mov ecx, eax
        0xB9, 0x01, 0x00, 0x00, 0x00, // mov    ecx,0x1 ; exit code 1
        0x48, 0xB8, 0x10, 0x10, 0x00, 0x40, 0x01, 0x00, 0x00, 0x00, // movabs rax, 0x140001010 (0x140001000+8+8)
        0xFF, 0x10, // call   QWORD PTR [rax]
    };
    BS_write_array(code_output, sizeof(rt_zero_div_handler), rt_zero_div_handler);
    SCARL_push(&string_consts_relocations, (StringConstAddrRelocation){
                   .const_index = runtime_error_str_index,
                   .patch_offset = sizeof(entry_point) + 28,
                   .bit_size = 8,
               });
    SCARL_push(&string_consts_relocations, (StringConstAddrRelocation){
                   .const_index = runtime_error__div_zero_str_index,
                   .patch_offset = sizeof(entry_point) + 28+46,
                   .bit_size = 8,
               });



    // __print
    // expects addr of char in RAX
    uint8_t print_char_util[] = {
        0x55,                   // push    rbp
        0x48, 0x89, 0xE5,       // mov     rbp, rsp
        0x48, 0x83, 0xEC, 0x48, // sub     rsp, 0x48


        0x49, 0x89, 0xC5, // mov r13, rax (save ptr)
        0x49, 0x89, 0xDE, // mov r14, rbx (save len)


        // GetStdHandle()
        0x48, 0xC7, 0xC1, 0xF5, 0xFF, 0xFF, 0xFF,                   // mov    rcx,0xfffffffffffffff5
        0x48, 0xB8, 0x00, 0x10, 0x00, 0x40, 0x01, 0x00, 0x00, 0x00, // movabs rax,0x140001000
        0xFF, 0x10,                                                 // call   QWORD PTR [rax]
        0x49, 0x89, 0xC4,                                           // mov    r12, rax


        // WriteFile()
        0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00,       // mov    QWORD PTR [rsp+0x20],0x0
        0x4C, 0x8D, 0x4C, 0x24, 0x28,                               // lea    r9,[rsp+0x28]
        0x4c, 0x89, 0xe1,                                           // mov    rcx,r12 ; restore std hadle

        0x4D, 0x89, 0xF0, // mov    r8,r14 (len)

        0x48, 0xB8, 0x08, 0x10, 0x00, 0x40, 0x01, 0x00, 0x00, 0x00, // movabs rax,0x140001008
        0x4C, 0x89, 0xEA,                                           // mov rdx, r13 ; restore char ptr
        0xFF, 0x10,                                                 // call   QWORD PTR [rax]

        0x48, 0x83, 0xC4, 0x48, // add rsp, 0x48
        0x5D, // pop  rbp
        0xC3, // ret
    };
    BS_write_array(code_output, sizeof(print_char_util), print_char_util);


    // emit stack preparation for _global frame
    uint8_t globals_prologue[] = {
        0x55, // push rbp
        0x48, 0x89, 0xE5, // mov rbp, rsp
        0x48, 0x83, 0xEC, 0xFF // sub rsp, N
    };
    BS_write_array(&code_output_globals, 8, globals_prologue);
    size_t globals_frame_override_pos = BS_get_cursor(&code_output_globals) - 1;


    compile(&svlv, current_source_file, F_Win64, *input);


    size_t globals_init_cursor_snap = BS_get_cursor(&code_output_globals);
    BS_set_cursor(&code_output_globals, globals_frame_override_pos);
    BS_write(&code_output_globals, (uint8_t) ((global_frame.next_offset + 15) & ~15));
    BS_set_cursor(&code_output_globals, globals_init_cursor_snap);

    uint8_t globals_epilogue[] = {
        0x48, 0x83, 0xC4, (uint8_t) ((global_frame.next_offset + 15) & ~15), // add rsp, N
        0x5D, // pop rbp
        0xC3, // ret
    };
    BS_write_array(&code_output_globals, sizeof(globals_epilogue), globals_epilogue);


    SVL_p_free(&svl);



    if (!FR_has_function(&functions_registry, SV_from_string_len("main", 4))) {
        fprintf(stderr, "[ERROR] no main function found in the program\n");
        exit(1);
    }
    const Function main_fn = FR_lookup_function(&functions_registry, SV_from_string_len("main", 4));
    if (main_fn.arg_count != 0) {
        fprintf(stderr, "[ERROR] main function should have not arguments, but %i were provided\n", main_fn.arg_count);
        exit(1);
    }
    if (main_fn.returns_value == false) {
        fprintf(stderr, "[ERROR] main function should return int, but declared in the program as 'void'\n");
        exit(1);
    }




    FR_register_function(&functions_registry, (Function){
                             .name = SV_from_string_len("__rt_exception__zero_div", 24),
                             .offset = sizeof(entry_point),
                             .code_size = sizeof(rt_zero_div_handler), // 0
                             .returns_value = false,
                             .arg_count = 0,
        .is_just_predef = false,
                         });

    FR_register_function(&functions_registry, (Function){
                             .name = SV_from_string_len("__print", 7),
                             .offset = sizeof(entry_point) + sizeof(rt_zero_div_handler),
                             .code_size = sizeof(print_char_util),
                             .returns_value = false,
                             .arg_count = 0,
        .is_just_predef = false,
                         });

    FR_register_function(&functions_registry, (Function){
                             .name = SV_from_string_len("__global", 8),
                             .offset = BS_get_cursor(code_output),
                             .code_size = code_output_globals.len,
                             .returns_value = false,
                             .arg_count = 0,
        .is_just_predef = false,
                         });

    BS_write_array(code_output, code_output_globals.len, code_output_globals.array);
    size_t bss_init_code_len = code_output_globals.len;

    resolve_function_calls(code_output->array, /*code_load_addr*/ code_output_funcs.len - code_output_globals.len, &functions_registry, &function_patch_list);
    FunctionsRegistry new_fr = FR_new();
    FR_register_function(&new_fr, (Function){
                             .name = SV_from_string_len("_start", 6),
                             .offset = 0,
                             .code_size = sizeof(entry_point),
                             .returns_value = false,
                             .arg_count = 0,
        .is_just_predef = false,
                         });

    for (size_t n = 0; n < functions_registry.len; n++) {
        FR_register_function(&new_fr, functions_registry.array[n]);
    }



    const uint8_t number_of_section = 4;

    write_win64(output_filepath, code_output->array, code_output_funcs.len, code_load_addr, &new_fr, &string_consts, &string_consts_relocations,
                number_of_section, importing_funcs, bss_alloc_next, bss_init_code_len);


    FR_free(&functions_registry);
    FR_free(&new_fr);
    FCPL_free(&function_patch_list);
    SVL_p_free(&import_table);
    SVL_p_free(&string_consts);
    SCARL_free(&string_consts_relocations);
}
