#include "compiler.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "elf_gen.h"
#include "expr.h"
#include "utils.h"

size_t tmp_cnt = 0;
size_t label_cnt = 0;
ByteSeg code_output;
RelocationList relocations;
LabelList labels;

static FunctionsRegistry functions_registry;
static FunctionCallPatchList function_patch_list;

void get_stmt(StringViewListView*, bool);

bool is_operator(char);

static StackFrame frame = {
    .next_offset = 4,
    .peek = 0
};

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

int get_stack_frame_max(StackFrame* f) {
    if (f->next_offset < f->peek)
        return f->peek;
    return f->next_offset;
}

static VarEntry* var_table;
static size_t var_table_cap;
static size_t var_table_length = 0;

static size_t function_end_id;

int lookup_var(StringView name) {
    for (size_t i = 0; i < var_table_length; i++) {
        if (SV__pp_cmp_eq(&var_table[i].name, &name))
            return var_table[i].offset;
    }
    // not found — should not happen if the parser is correct
    fprintf(stderr, "[ERROR] <internal> lookup_var | undefined variable: %.*s\n", (int) name.len, name.start);
    exit(1);
}

int declare_var(StringView name) {
    int slot = alloc_stack_slot(&frame);
    if (var_table_length+1 > var_table_cap) {
        var_table_cap *= 2;
        VarEntry* new_array = realloc(var_table, sizeof(VarEntry)*var_table_cap);
        assert(new_array != NULL);
        var_table = new_array;
    }
    var_table[var_table_length++] = (VarEntry){.name = name, .offset = slot};
    return slot;
}


StringViewList p_tokenize(const StringView* sv) {
    StringViewList ot = SVL_new();
    size_t i = 0;
    while (i < sv->len) {
        // skip whitespace
        if (sv->start[i] == ' ' || sv->start[i] == '\n' || sv->start[i] == '\t') {
            i++;
            continue;
        }
        // single-char tokens: operators, parens, semicolons
        if (is_operator(sv->start[i]) || sv->start[i] == '(' ||
            // sv->start[i] == '=' || sv->start[i] == '<' || sv->start[i] == '>' ||
            sv->start[i] == ')' || sv->start[i] == ';' || sv->start[i] == ',' ||
            sv->start[i] == ':') {
            SVL_p_push(&ot, SV_lrslice_from_SV(sv, i, 1));
            i++;
            continue;
        }
        // multi-char tokens: identifiers and integer literals
        size_t start = i;
        while (i < sv->len &&
               sv->start[i] != ' ' && sv->start[i] != '\t' && sv->start[i] != '\n' &&
               !is_operator(sv->start[i]) &&
               sv->start[i] != '(' && sv->start[i] != ')' &&
               sv->start[i] != ';' && sv->start[i] != ':' && sv->start[i] != ',') {
            // && sv->start[i] != '='
            i++;
        }
        SVL_p_push(&ot, SV_lrslice_from_SV(sv, start, i - start));
    }
    return ot;
}

bool is_stmt_next(StringViewListView* view) {
    if (view->len < 1) return false;
    StringView s1 = SVLV_inspect_back(view);
    if (SV__pv_cmp_eq(&s1, "int", 3)) return true;
    if (SV__pv_cmp_eq(&s1, "set", 3)) return true;
    if (SV__pv_cmp_eq(&s1, "if", 2)) return true;
    if (SV__pv_cmp_eq(&s1, "while", 5)) return true;
    if (SV__pv_cmp_eq(&s1, "print", 5)) return true;
    if (SV__pv_cmp_eq(&s1, "call", 4)) return true;

    return false;
}

bool is_stmt_next_return(StringViewListView* view) {
    if (view->len < 1) return false;
    StringView s1 = SVLV_inspect_back(view);
    if (SV__pv_cmp_eq(&s1, "return", 6)) return true;

    return false;
}


void get_semicolon(StringViewListView* view) {
    StringView semicolon = SVLV_consume_one(view);
    if (!SV__pv_cmp_eq(&semicolon, ";", 1)) {
        // error
        fprintf(stderr, "[ERROR] expected ';', got '%.*s'\n", (int) semicolon.len, semicolon.start);
        exit(1);
    }
}

void get_return_stmt(StringViewListView* view, bool should_return_value) {
    StringView ret_keyword = SVLV_consume_one(view);
    if (!SV__pv_cmp_eq(&ret_keyword, "return", 6)) {
        fprintf(stderr, "[ERROR] <internal> should not happen | get_return_stmt and no return token\n");
        exit(1);
    }

    StringView semi_or_val = SVLV_inspect_back(view);
    if (SV__pv_cmp_eq(&semi_or_val, ";", 1)) {
        if (should_return_value) {
            fprintf(stderr, "[ERROR] unexpected ';' for int function, expected value\n");
            exit(1);
        }
        SVLV_consume_one(view);
    } else {
        if (!should_return_value) {
            fprintf(stderr, "[ERROR] expected ';' for void function, got '%.*s'\n", (int)semi_or_val.len, semi_or_val.start);
            exit(1);
        }
        Loc val = get_int_expr(view);
        get_semicolon(view);
        emit_mov_eax(&code_output, val);
    }

    // JMP fn_end_<id> (placeholder)
    BS_write(&code_output, 0xE9);
    size_t jmp_patch = BS_get_cursor(&code_output);
    size_t jmp_end   = jmp_patch + 4;
    BS_write_array(&code_output, 4, (uint8_t[]){0x00, 0x00, 0x00, 0x00});
    RL_push(&relocations, (Relocation){
        .patch_pos  = jmp_patch,
        .patch_size = 4,
        .inst_end   = jmp_end,
        .id         = function_end_id,
        .kind       = REL_END,
    });
}


void get_stmt_block(StringViewListView* view, bool should_return_value) {
    while (view->len > 0) {
        if (is_stmt_next_return(view)) {
            get_return_stmt(view, should_return_value);
            return;
        }
        if (!is_stmt_next(view)) break;
        get_stmt(view, should_return_value);
    }
}





void get_int_var_dec(StringViewListView* view) {
    StringView var_name = SVLV_consume_one(view);
    int slot = declare_var(var_name);

    StringView assign = SVLV_consume_one(view);
    if (!SV__pv_cmp_eq(&assign, ":", 1)) {
        // error
        fprintf(stderr, "[ERROR] expected ':' in variable declaration, got '%.*s'\n", (int) assign.len, assign.start);
        exit(1);
    }
    // printf("  %%var_%.*s = alloca i32, align 4\n", (int) var_name.len, var_name.start);

    Loc last = get_int_expr(view);
    // printf("  store i32 %.*s, ptr %%var_%.*s, align 4\n", (int) last.len, last.start, (int) var_name.len, var_name.start);

    // move last into eax
    emit_mov_eax(&code_output, last);

    // MOV [rbp - slot], eax
    BS_write(&code_output, 0x89);
    BS_write(&code_output, 0x45);
    BS_write(&code_output, (uint8_t) (-slot));

    get_semicolon(view);
}

void get_int_var_set(StringViewListView* view) {
    StringView var_name = SVLV_consume_one(view);
    int slot = lookup_var(var_name);
    StringView assign = SVLV_consume_one(view);
    if (!SV__pv_cmp_eq(&assign, ":", 1)) {
        // error
        fprintf(stderr, "[ERROR] expected ':' in variable change, got '%.*s'\n", (int) assign.len, assign.start);
        exit(1);
    }

    Loc last = get_int_expr(view);
    // printf("  store i32 %.*s, ptr %%var_%.*s, align 4\n", (int) last.len, last.start, (int) var_name.len, var_name.start);


    // move last into eax
    emit_mov_eax(&code_output, last);

    // MOV [rbp - slot], eax
    BS_write(&code_output, 0x89);
    BS_write(&code_output, 0x45);
    BS_write(&code_output, (uint8_t) (-slot));

    get_semicolon(view);
}

/*void get_if_conditional(StringViewListView* view) {
    const Loc cond = get_int_expr(view);

    StringView then = SVLV_consume_one(view);
    if (!SV__pv_cmp_eq(&then, "then", 4)) {
        fprintf(stderr, "[ERROR] expected 'then' after expression in if conditional, got '%.*s'\n", (int) then.len,
                then.start);
        exit(1);
    }

    const size_t tmp = tmp_cnt;
    tmp_cnt++;
    // printf("  %%cond_%lu = icmp ne i32 %.*s, 0\n", tmp, (int) cond.len, cond.start);
    // printf("  br i1 %%cond_%lu, label %%if_then_%lu, label %%if_else_%lu\n", tmp, tmp, tmp);
    // printf("if_then_%lu:\n", tmp);

    get_stmt_block(view);

    // printf("  br label %%if_end_%lu\n", tmp);
    // printf("if_else_%lu:\n", tmp);

    StringView else_keyword = SVLV_inspect_back(view);
    if (SV__pv_cmp_eq(&else_keyword, "else", 4)) {
        SVLV_consume_one(view);
        get_stmt_block(view);
    }

    // printf("  br label %%if_end_%lu\n", tmp);
    // printf("if_end_%lu:\n", tmp);

    StringView end = SVLV_consume_one(view);
    if (!SV__pv_cmp_eq(&end, "end", 3)) {
        fprintf(stderr, "[ERROR] expected 'end' after if conditional, got '%.*s'\n", (int) end.len, end.start);
        exit(1);
    }
}*/

void get_if_conditional(StringViewListView* view, bool should_return_value) {
    const Loc cond = get_int_expr(view);

    StringView then = SVLV_consume_one(view);
    if (!SV__pv_cmp_eq(&then, "then", 4)) {
        fprintf(stderr, "[ERROR] expected 'then' after expression in if conditional, got '%.*s'\n", (int) then.len, then.start);
        exit(1);
    }

    const size_t id = label_cnt++;

    // TEST eax, eax
    emit_mov_eax(&code_output, cond);
    BS_write_array(&code_output, 2, (uint8_t[]){0x85, 0xC0});

    // JE else_<id> (placeholder)
    BS_write_array(&code_output, 2, (uint8_t[]){0x0F, 0x84});
    size_t je_patch = BS_get_cursor(&code_output);
    size_t je_end   = je_patch + 4;
    BS_write_array(&code_output, 4, (uint8_t[]){0x00, 0x00, 0x00, 0x00});
    RL_push(&relocations, (Relocation){
        .patch_pos  = je_patch,
        .patch_size = 4,
        .inst_end   = je_end,
        .id         = id,
        .kind       = REL_ELSE,
    });

    // then-block
    get_stmt_block(view, should_return_value);

    StringView else_keyword = SVLV_inspect_back(view);
    if (SV__pv_cmp_eq(&else_keyword, "else", 4)) {
        SVLV_consume_one(view);

        // JMP end_<id> (placeholder)
        BS_write(&code_output, 0xE9);
        size_t jmp_patch = BS_get_cursor(&code_output);
        size_t jmp_end   = jmp_patch + 4;
        BS_write_array(&code_output, 4, (uint8_t[]){0x00, 0x00, 0x00, 0x00});
        RL_push(&relocations, (Relocation){
            .patch_pos  = jmp_patch,
            .patch_size = 4,
            .inst_end   = jmp_end,
            .id         = id,
            .kind       = REL_END,
        });

        // define else_<id>
        LL_push(&labels, (Label){ .offset = BS_get_cursor(&code_output), .id = id, .kind = REL_ELSE });

        get_stmt_block(view, should_return_value);
    } else {
        // no else — JE jumps directly to end, define else_<id> here as same as end
        LL_push(&labels, (Label){ .offset = BS_get_cursor(&code_output), .id = id, .kind = REL_ELSE });
    }

    // define end_<id>
    LL_push(&labels, (Label){ .offset = BS_get_cursor(&code_output), .id = id, .kind = REL_END });

    StringView end = SVLV_consume_one(view);
    if (!SV__pv_cmp_eq(&end, "end", 3)) {
        fprintf(stderr, "[ERROR] expected 'end' after if conditional, got '%.*s'\n", (int) end.len, end.start);
        exit(1);
    }
}

void get_while_conditional(StringViewListView* view, bool should_return_value) {
    const size_t id = label_cnt++;

    // define loop_<id> (loop back target)
    LL_push(&labels, (Label){ .offset = BS_get_cursor(&code_output), .id = id, .kind = REL_LOOP });

    const Loc cond = get_int_expr(view);

    StringView do_keyword = SVLV_consume_one(view);
    if (!SV__pv_cmp_eq(&do_keyword, "do", 2)) {
        fprintf(stderr, "[ERROR] expected 'do' after expression in while conditional, got '%.*s'\n", (int) do_keyword.len, do_keyword.start);
        exit(1);
    }

    // TEST eax, eax
    emit_mov_eax(&code_output, cond);
    BS_write_array(&code_output, 2, (uint8_t[]){0x85, 0xC0});

    // JE end_<id> (placeholder)
    BS_write_array(&code_output, 2, (uint8_t[]){0x0F, 0x84});
    size_t je_patch = BS_get_cursor(&code_output);
    size_t je_end   = je_patch + 4;
    BS_write_array(&code_output, 4, (uint8_t[]){0x00, 0x00, 0x00, 0x00});
    RL_push(&relocations, (Relocation){
        .patch_pos  = je_patch,
        .patch_size = 4,
        .inst_end   = je_end,
        .id         = id,
        .kind       = REL_END,
    });

    // body
    get_stmt_block(view, should_return_value);

    // JMP loop_<id> (placeholder)
    BS_write(&code_output, 0xE9);
    size_t jmp_patch = BS_get_cursor(&code_output);
    size_t jmp_end   = jmp_patch + 4;
    BS_write_array(&code_output, 4, (uint8_t[]){0x00, 0x00, 0x00, 0x00});
    RL_push(&relocations, (Relocation){
        .patch_pos  = jmp_patch,
        .patch_size = 4,
        .inst_end   = jmp_end,
        .id         = id,
        .kind       = REL_LOOP,
    });

    // define end_<id>
    LL_push(&labels, (Label){ .offset = BS_get_cursor(&code_output), .id = id, .kind = REL_END });

    StringView end = SVLV_consume_one(view);
    if (!SV__pv_cmp_eq(&end, "end", 3)) {
        fprintf(stderr, "[ERROR] expected 'end' after while conditional, got '%.*s'\n", (int) end.len, end.start);
        exit(1);
    }
}

// messses stack
void get_print_stmt(StringViewListView* view) {
    while (view->len > 0) {
        StringView inspect = SVLV_inspect_back(view);
        if (inspect.start[0] == ';') {
            break;
        }
        Loc expr = get_int_expr(view);
        /*
        %7 = load i32, ptr %c, align 4
        call void @a(i32 noundef %7)
         */
        // printf("  call void @print_num(i32 noundef %.*s)\n", (int)expr.len, expr.start);
        switch (expr.kind) {
            case LOC_IMMEDIATE:
            case LOC_STACK_SLOT: {
                emit_mov_eax(&code_output, expr);

                // mov [rbp - offset], eax
                int offset = alloc_tmp_stack_slot(&frame);
                // int offset = frame.next_offset;
                // frame.next_offset += 4;

                BS_write_array(&code_output, 3, (uint8_t[]){ 0x89, 0x45, (uint8_t)(-offset) });

                // mov rax, 1  (sys_write)
                // mov rdi, 1  (stdout)
                BS_write_array(&code_output, 14, (uint8_t[]){ 0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00, 0x48, 0xC7, 0xC7, 0x01, 0x00, 0x00, 0x00 });

                // lea rsi, [rbp - offset]
                BS_write(&code_output, 0x48);
                BS_write(&code_output, 0x8D);
                BS_write(&code_output, 0x75);
                BS_write(&code_output, (uint8_t)(-offset));

                // mov rdx, 1  +  syscall
                BS_write_array(&code_output, 9, (uint8_t[]){ 0x48, 0xC7, 0xC2, 0x01, 0x00, 0x00, 0x00, 0x0F, 0x05 });
                break;
            }
            case LOC_VAR: {
                // // If it's already a variable in memory, you don't need 'push' or 'eax'!
                // // You can use LEA directly on the variable's memory address.
                // emit("mov rax, 1");          // sys_write
                // emit("mov rdi, 1");          // stdout
                //
                // // LEA gets the pointer directly to the variable
                // emit("lea rsi, [rip + %s]", expr.var_name);
                //
                // emit("mov rdx, 1");          // 1 byte
                // emit("syscall");

                // LEA eax, [rbp - offset]

                // 0:  48 c7 c0 01 00 00 00    mov    rax,0x1
                // 7:  48 c7 c7 01 00 00 00    mov    rdi,0x1
                BS_write_array(&code_output, 14, (uint8_t[]){ 0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00, 0x48, 0xC7, 0xC7, 0x01, 0x00, 0x00, 0x00 });

                int slot = lookup_var(expr.var);
                BS_write(&code_output, 0x48);             // REX.W prefix
                BS_write(&code_output, 0x8D);             // Opcode for LEA
                BS_write(&code_output, 0x75);             // ModR/M byte for rsi, [rbp + disp8]
                BS_write(&code_output, (uint8_t) (-slot)); // 8-bit negative displacement

                // 15: 48 c7 c2 01 00 00 00    mov    rdx,0x1
                // 1c: 0f 05                   syscall
                BS_write_array(&code_output, 9, (uint8_t[]){ 0x48, 0xC7, 0xC2, 0x01, 0x00, 0x00, 0x00, 0x0F, 0x05 });
                break;
            }
        }
    }
    get_semicolon(view);
}

void get_function_call_stmt(StringViewListView* view) {
    StringView f_name = SVLV_consume_one(view);
    if (!FR_has_function(&functions_registry, f_name)) {
        fprintf(stderr, "[ERROR] function '%.*s' not found\n", (int)f_name.len, f_name.start);
        exit(1);
    }
    Function f = FR_lookup_function(&functions_registry, f_name);

    StringView lbracket = SVLV_consume_one(view);
    if (!SV__pv_cmp_eq(&lbracket, "(", 1)) {
        fprintf(stderr, "[ERROR] expected '(' after function name in function call, got '%.*s'\n", (int)lbracket.len, lbracket.start);
        exit(1);
    }


    uint8_t expected_args = f.arg_count;
    int* args_items = malloc(sizeof(int)*f.arg_count);

    while (expected_args > 0) {
        StringView inspect = SVLV_inspect_back(view);
        if (SV__pv_cmp_eq(&inspect, ")", 1)) {
            fprintf(stderr, "[ERROR] unexpected ')' function call, expected another %u arguments\n", expected_args);
            exit(1);
        }
        int slot = alloc_stack_slot(&frame); // alloc_tmp_stack_slot
        args_items[f.arg_count-expected_args] = slot;

        Loc last = get_int_expr(view);
        // printf("function <%.*s>: arg <%.*s>\n" (int)f_name.len, f_name.start, );
        // printf("  store i32 %.*s, ptr %%var_%.*s, align 4\n", (int) last.len, last.start, (int) var_name.len, var_name.start);

        // move last into eax
        emit_mov_eax(&code_output, last);

        // MOV [rbp - slot], eax
        BS_write(&code_output, 0x89);
        BS_write(&code_output, 0x45);
        BS_write(&code_output, (uint8_t) (-slot));
        expected_args--;
        if (expected_args > 0) {
            StringView comma = SVLV_consume_one(view);
            if (!SV__pv_cmp_eq(&comma, ",", 1)) {
                fprintf(stderr, "[ERROR] expected ',' function call args, got '%.*s'\n", (int)comma.len, comma.start);
                exit(1);
            }
        }
    }

    StringView rbracket = SVLV_consume_one(view);
    if (!SV__pv_cmp_eq(&rbracket, ")", 1)) {
        fprintf(stderr, "[ERROR] function <%.*s>: expected ')' function call after args, got '%.*s'\n", (int)f_name.len, f_name.start ,(int)rbracket.len, rbracket.start);
        exit(1);
    }

    // rdi, rsi, rdx, rcx, r8, r9
    static const uint8_t arg_regs_modrm[] = { 0x7D, 0x75, 0x55, 0x4D, 0x00, 0x00 };
    // REX.W prefix needed for r8/r9 (extended registers)
    static const uint8_t arg_regs_rex[]   = { 0x48, 0x48, 0x48, 0x48, 0x4C, 0x4C };
    // opcode: MOV reg, [rbp - disp8]  ->  8B /r
    // for r8/r9 the ModR/M is 0x45/0x4D with REX.R set (via 0x4C)
    static const uint8_t arg_regs_r8_modrm[] = { 0x45, 0x4D };

    for (int i = 0; i < f.arg_count; i++) {
        int slot = args_items[i];
        if (i < 4) {
            // MOV rdi/rsi/rdx/rcx, [rbp - slot]
            // 48 8B ModRM disp8
            BS_write(&code_output, arg_regs_rex[i]);
            BS_write(&code_output, 0x8B);
            BS_write(&code_output, arg_regs_modrm[i]);
            BS_write(&code_output, (uint8_t)(-slot));
        } else {
            // MOV r8/r9, [rbp - slot]
            // 4C 8B ModRM disp8
            BS_write(&code_output, 0x4C);
            BS_write(&code_output, 0x8B);
            BS_write(&code_output, arg_regs_r8_modrm[i - 4]);
            BS_write(&code_output, (uint8_t)(-slot));
        }
        // free_tmp_stack_slot(&frame, slot);
    }

    free(args_items);



    size_t pos = BS_get_cursor(&code_output) + 1;
    uint8_t call[] = {
        0xe8, 0x00, 0x00, 0x00, 0x00, //          call <>
    };
    BS_write_array(&code_output, sizeof(call), call);
    FCPL_register_pach(&function_patch_list, (FunctionCallPatch) {
        .name = f.name,
        .offset = pos,
        .relative = true,
        .bit_size = 4,
    });


    if (f.returns_value) {
        StringView semi_or_into = SVLV_inspect_back(view);
        if (SV__pv_cmp_eq(&semi_or_into, ";", 1)) {
            get_semicolon(view);
            printf("[warning] discarding return value of function <>\n");
        } else if (SV__pv_cmp_eq(&semi_or_into, "into", 4)) {
            SVLV_consume_one(view); // into
            StringView into_var = SVLV_consume_one(view);
            int slot = lookup_var(into_var);
            // MOV [rbp - slot], eax
            BS_write(&code_output, 0x89);
            BS_write(&code_output, 0x45);
            BS_write(&code_output, (uint8_t) (-slot));

            get_semicolon(view);
        } else {
            fprintf(stderr, "[ERROR] expected either ';' or 'into' after function call, got '%.*s'\n", (int)semi_or_into.len ,semi_or_into.start);
            exit(1);
        }
    } else {
        get_semicolon(view);
    }


}

void get_stmt(StringViewListView* view, bool should_return_value) {
    StringView s1 = SVLV_consume_one(view);
    if (SV__pv_cmp_eq(&s1, "int", 3)) {
        get_int_var_dec(view);
    } else if (SV__pv_cmp_eq(&s1, "set", 3)) {
        get_int_var_set(view);
    } else if (SV__pv_cmp_eq(&s1, "if", 2)) {
        get_if_conditional(view, should_return_value);
    } else if (SV__pv_cmp_eq(&s1, "while", 5)) {
        get_while_conditional(view, should_return_value);
    } else if (SV__pv_cmp_eq(&s1, "print", 5)) {
        get_print_stmt(view);
    } else if (SV__pv_cmp_eq(&s1, "call", 4)) {
        get_function_call_stmt(view);
    } else {
        printf("STMT: unknown\n");
    }
}

void get_function(StringViewListView* view) {
    size_t f_start_cursor = BS_get_cursor(&code_output);

    create_frame();

    function_end_id = label_cnt++;


    StringView ret_type = SVLV_consume_one(view);
    bool returns_value = false;
    if (SV__pv_cmp_eq(&ret_type, "int", 3)) {
        returns_value = true;
    } else if (SV__pv_cmp_eq(&ret_type, "void", 4)) {

    } else {
        fprintf(stderr, "[ERROR] unexpected token '%.*s'\n", (int)ret_type.len, ret_type.start);
        exit(1);
    }

    StringView f_name = SVLV_consume_one(view);

    StringView f_args_start = SVLV_consume_one(view);
    if (!SV__pv_cmp_eq(&f_args_start, "(", 1)) {
        fprintf(stderr, "[ERROR] expected '(', got '%.*s'\n", (int)f_args_start.len, f_args_start.start);
        exit(1);
    }

    StringViewList f_args = SVL_new();
    while (true) {
        StringView s = SVLV_consume_one(view);
        if (SV__pv_cmp_eq(&s, ")", 1)) {
            break;
        }
        SVL_p_push(&f_args, s);

        StringView comma = SVLV_consume_one(view);
        if (SV__pv_cmp_eq(&comma, ")", 1)) {
            break;
        } else if (SV__pv_cmp_eq(&comma, ",", 1)) {
            // ok
        } else {
            fprintf(stderr, "[ERROR] expected comma or ')', got '%.*s'\n", (int)comma.len, comma.start);
            exit(1);
        }
    }

    // temp write
    FR_register_function(&functions_registry, (Function) {
        .name = f_name,
        .arg_count = f_args.len,
        .offset = f_start_cursor,
        .code_size = 0,
        .returns_value = returns_value
    });


    // todo: emit stack preparation for frame
    uint8_t prologue[] = {
        0x55,                               // push rbp
        0x48, 0x89, 0xE5,                   // mov rbp, rsp
        0x48, 0x83, 0xEC, 0xFF              // sub rsp, N
    };
    BS_write_array(&code_output, 8, prologue);
    size_t prologue_frame_override_pos = BS_get_cursor(&code_output) - 1;
    // (uint8_t)((get_frame()->next_offset + 15) & ~15)

    // todo: read arguments into variables into stack

    // static const uint8_t param_store_rex[]   = { 0x48, 0x48, 0x48, 0x48, 0x4C, 0x4C };
    static const uint8_t param_store_modrm[] = { 0x7D, 0x75, 0x55, 0x4D, 0x45, 0x4D }; // rdi,rsi,rdx,rcx,r8,r9

    for (size_t i = 0; i < f_args.len; i++) {
        int slot = declare_var(f_args.array[i]);  // same as local var
        printf("in function <%.*s>, param '%.*s'\n", (int)f_name.len, f_name.start, (int)f_args.array[i].len, f_args.array[i].start);

        // MOV [rbp - slot], rdi/rsi/...
        // BS_write(&code_output, param_store_rex[i]); // no rex.w, just 32-bit
        BS_write(&code_output, 0x89);
        BS_write(&code_output, param_store_modrm[i]);
        BS_write(&code_output, (uint8_t)(-slot));
    }


    get_stmt_block(view, returns_value);



    StringView f_end = SVLV_consume_one(view);
    if (!SV__pv_cmp_eq(&f_end, "end", 3)) {
        fprintf(stderr, "[ERROR] expected 'end', got '%.*s'\n", (int)f_end.len, f_end.start);
        exit(1);
    }

    // define fn_end_<id>
    LL_push(&labels, (Label){ .offset = BS_get_cursor(&code_output), .id = function_end_id, .kind = REL_END });

    size_t cursor_snap = BS_get_cursor(&code_output);
    BS_set_cursor(&code_output, prologue_frame_override_pos);
    BS_write(&code_output, (uint8_t)((get_stack_frame_max(get_frame()) + 15) & ~15));
    BS_set_cursor(&code_output, cursor_snap);


    // function_end_id

    uint8_t epilogue[] = {
        0x48, 0x83, 0xC4, (uint8_t)((get_stack_frame_max(get_frame()) + 15) & ~15), // add rsp, N
        0x5D,                                       // pop rbp
        0xC3,                                       // ret
    };
    BS_write_array(&code_output, 6, epilogue);

    size_t f_end_cursor = BS_get_cursor(&code_output);

    printf("f_start_cursor = %lu\n", f_start_cursor);

    FR_overwrite_function(&functions_registry, (Function) {
        .name = f_name,
        .arg_count = f_args.len,
        .offset = f_start_cursor,
        .code_size = f_end_cursor - f_start_cursor,
        .returns_value = returns_value
    });


    resolve_frame();
    printf("[INFO] compiled function '%.*s'\n", (int)f_name.len, f_name.start);
}


void create_frame(void) {
    relocations = RL_new();
    assert(relocations.array != NULL);

    labels = LL_new();
    assert(labels.array != NULL);

    // reset frame
    frame = (StackFrame) { .next_offset = 4 };

    // reset var registry
    // static VarEntry var_table[256];
    // static int var_count = 0;
    var_table_length = 0;
    var_table_cap = 4;
    var_table = malloc(sizeof(VarEntry)*var_table_cap);

}

void resolve_frame(void) {
    resolve_relocations(&code_output, &relocations, &labels);

    LL_free(&labels);
    RL_free(&relocations);
}

void process(const StringView* input) {
    code_output = BS_new();
    assert(code_output.array != NULL);

    functions_registry = FR_new();
    function_patch_list = FCPL_new();

    // printf("input: '");
    // SV_p_printf(input);
    // printf("'\n");


    // StringViewList svl = SV_p_split_by_char(input, ' ');
    StringViewList svl = p_tokenize(input);

    /*printf("tokens: [");
    for (size_t i = 0; i < svl.len; i++) {
        putc('\'', stdout);
        SV_p_printf(&svl.array[i]);
        if (i + 1 != svl.len)
            printf("', ");
        else
            printf("']\n");
    }*/


    StringViewListView svlv = SVLV_from_SVL(&svl);

    // printf("------------------------------\n");

    size_t entry_point_patch_cursor = BS_get_cursor(&code_output) + 1;
    uint8_t entry_point[] = {
        0xe8, 0x00, 0x00, 0x00, 0x00, //          call   5 <_main+0x5>
        0x48, 0x89, 0xc7,             //          mov    rdi,rax
        0x48, 0xc7, 0xc0, 0x3c, 0x00, 0x00, 0x00,  //         mov    rax,0x3c
        0x0f, 0x05, //                   syscall
    };
    BS_write_array(&code_output, sizeof(entry_point), entry_point);
    FCPL_register_pach(&function_patch_list, (FunctionCallPatch) {
        .name = SV_from_string_len("main", 4),
        .offset = entry_point_patch_cursor,
        .relative = true,
        .bit_size = 4,
    });

    // get_stmt_block(&svlv);
    //
    // // tokens left
    // if (svlv.len != 0) {
    //     fprintf(stderr, "[ERROR] tokens left\n");
    //     SVL_p_free(&svl);
    //     exit(1);
    // }
    while(svlv.len > 0) {
        get_function(&svlv);
    }

    SVL_p_free(&svl);

    // printf("------------------------------\n");
    // printf("        BS-code segs\n");
    // printf("code_output.len=%lu\n", code_output.len);
    // printf("------------------------------\n");

    size_t byte_size = code_output.len;
    // BS_print(&code_output);

    // printf("------------------------------\n");


    uint8_t* data = malloc(byte_size);
    memcpy(data, code_output.array, byte_size);
    /*uint8_t* data_ptr = data;
    for(size_t n = 0; n < code_output.len; n++) {
        memcpy(data_ptr, code_output.array[n].array, code_output.array[n].cursor);
        data_ptr += code_output.array[n].cursor;
    }*/

    const uint64_t load_addr = 0x400000;

    resolve_function_calls(data, load_addr, &functions_registry, &function_patch_list);
    write_elf64("out", data, byte_size, load_addr);

    BS_free(&code_output);
    free(data);
    FR_free(&functions_registry);
    FCPL_free(&function_patch_list);
}
