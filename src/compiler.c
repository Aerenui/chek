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

void get_stmt(StringViewListView*);

bool is_operator(char);

static StackFrame frame = {.next_offset = 4};

StackFrame* get_frame(void) {
    return &frame;
}

int alloc_stack_slot(StackFrame* f) {
    int slot = f->next_offset;
    f->next_offset += 4;
    return slot; // rbp - slot
}

static VarEntry var_table[256];
static int var_count = 0;

int lookup_var(StringView name) {
    for (int i = 0; i < var_count; i++) {
        if (SV__pp_cmp_eq(&var_table[i].name, &name))
            return var_table[i].offset;
    }
    // not found — should not happen if your parser is correct
    fprintf(stderr, "[ERROR] undefined variable: %.*s\n", (int) name.len, name.start);
    exit(1);
}

int declare_var(StringView name) {
    int slot = alloc_stack_slot(&frame);
    var_table[var_count++] = (VarEntry){.name = name, .offset = slot};
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
            sv->start[i] == ')' || sv->start[i] == ';' ||
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
               sv->start[i] != ';' && sv->start[i] != ':') {
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
    if (SV__pv_cmp_eq(&s1, "print", 5)) return true;

    return false;
}

void get_stmt_block(StringViewListView* view) {
    while (view->len > 0) {
        if (!is_stmt_next(view)) break;
        get_stmt(view);
    }
}


void get_semicolon(StringViewListView* view) {
    StringView semicolon = SVLV_consume_one(view);
    if (!SV__pv_cmp_eq(&semicolon, ";", 1)) {
        // error
        fprintf(stderr, "[ERROR] expected ';', got '%.*s'\n", (int) semicolon.len, semicolon.start);
        exit(1);
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

void get_if_conditional(StringViewListView* view) {
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
    get_stmt_block(view);

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

        get_stmt_block(view);
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
                // // 2. Push to stack so it has a memory address, then use LEA
                // emit("push rax");
                // emit("mov rax, 1");          // sys_write
                // emit("mov rdi, 1");          // stdout
                // emit("lea rsi, [rsp]");      // load address of the char from stack
                // emit("mov rdx, 1");          // 1 byte
                // emit("syscall");
                // emit("pop rax");             // clean up stack
                uint8_t code[] = { 0x50, 0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00, 0x48, 0xC7, 0xC7, 0x01, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x34, 0x24, 0x48, 0xC7, 0xC2, 0x01, 0x00, 0x00, 0x00, 0x0F, 0x05, 0x58 };
                BS_write_array(&code_output, 0x1c, code);
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

void get_stmt(StringViewListView* view) {
    StringView s1 = SVLV_consume_one(view);
    if (SV__pv_cmp_eq(&s1, "int", 3)) {
        get_int_var_dec(view);
    } else if (SV__pv_cmp_eq(&s1, "set", 3)) {
        get_int_var_set(view);
    } else if (SV__pv_cmp_eq(&s1, "if", 2)) {
        get_if_conditional(view);
    } else if (SV__pv_cmp_eq(&s1, "print", 5)) {
        get_print_stmt(view);
        // fprintf(stderr, "[ERROR] unimplemented 'print'\n");
        // exit(1);
    } else {
        printf("STMT: unknown\n");
    }
}


void process(const StringView* input) {
    code_output = BS_new();
    assert(code_output.array != NULL);

    relocations = RL_new();
    assert(relocations.array != NULL);

    labels = LL_new();
    assert(labels.array != NULL);

    printf("input: '");
    SV_p_printf(input);
    printf("'\n");


    // StringViewList svl = SV_p_split_by_char(input, ' ');
    StringViewList svl = p_tokenize(input);

    printf("tokens: [");
    for (size_t i = 0; i < svl.len; i++) {
        putc('\'', stdout);
        SV_p_printf(&svl.array[i]);
        if (i + 1 != svl.len)
            printf("', ");
        else
            printf("']\n");
    }


    StringViewListView svlv = SVLV_from_SVL(&svl);

    // printf("------------------------------\n");

    get_stmt_block(&svlv);

    // tokens left
    if (svlv.len != 0) {
        fprintf(stderr, "[ERROR] tokens left\n");
        SVL_p_free(&svl);
        exit(1);
    }

    SVL_p_free(&svl);

    resolve_relocations(&code_output, &relocations, &labels);

    printf("------------------------------\n");
    printf("        BS-code segs\n");
    printf("code_output.len=%lu\n", code_output.len);
    printf("------------------------------\n");

    size_t byte_size = code_output.len;
    BS_print(&code_output);
    /*for(size_t n = 0; n < code_output.len; n++) {
        // printf("BS %lu\n", n);
        BS_print(&code_output.array[n]);
        byte_size += code_output.array[n].cursor;
    }*/

    printf("------------------------------\n");


    uint8_t* data = malloc(byte_size);
    memcpy(data, code_output.array, byte_size);
    /*uint8_t* data_ptr = data;
    for(size_t n = 0; n < code_output.len; n++) {
        memcpy(data_ptr, code_output.array[n].array, code_output.array[n].cursor);
        data_ptr += code_output.array[n].cursor;
    }*/
    write_elf64("out", data, byte_size);
    free(data);

    BS_free(&code_output);
    LL_free(&labels);
    RL_free(&relocations);
}
