//
// Created by frantisek on 25. 5. 2026.
//

#include "expr.h"

#include <assert.h>
#include <stdio.h>

#include "compiler.h"
#include "utils.h"


bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int precedence(char op) {
    if (op == '&' || op == '|') return 0;
    if (op == '<' || op == '>' || op == '=' || op == '!') return 1;
    if (op == '+' || op == '-') return 2;
    if (op == '*' || op == '/' || op == '%') return 3;

    fprintf(stderr, "[ERROR] <intern> precedence ( '%c' ) {%i}\n", op, op);
    exit(1);
}

bool is_operator(char c) {
    return c == '+' || c == '-' ||
           c == '*' || c == '/' ||
           c == '<' || c == '>' ||
           c == '=' || c == '%' ||
           c == '&' || c == '|' || c == '!';
}

/*char* format_operator(char c) {
    if (c == '+') return "add";
    if (c == '-') return "sub";
    if (c == '*') return "mul";
    if (c == '/') return "div";
    fprintf(stderr, "[ERROR] unknown operator '%c'\n", c);
    exit(1);
}*/


Loc/*StringView*/ get_int_expr(StringViewListView* view, CompilerTarget target, bool is_local) {
    StringViewList ot = SVL_new();
    StringViewList operator_stack = SVL_new();
    bool expect_operator = false;

    /*while (!SVLV_is_empty(view)) {
        StringView nw = SVLV_inspect_back(view);
        {
            // StringView inspect = SVLV_inspect_back(view);
            char fc = nw.start[0];
            if (is_digit(fc) || is_operator(fc) || is_alpha(fc) || fc == '(' || fc == ')'); else {
                break;
            }
            if (fc == ';') break;
        }

        // StringView nw = SVLV_consume_one(view);

        // --- GRAMMAR VALIDATION CHECK ---
        if (expect_operator) {
            // We are expecting an operator or a closing parenthesis.
            // If we get a variable, int, or opening parenthesis, the expression has broken/ended.
            if (is_digit(nw.start[0]) || is_alpha(nw.start[0]) || (nw.start[0] == '(' && nw.len == 1)) {
                break; // Early exit! Stop processing further tokens.
            }
        } else {
            // We are expecting a value or an opening parenthesis.
            // If we get an operator or a closing parenthesis, it's malformed or stopped.
            if ((is_operator(nw.start[0]) || nw.start[0] == ')') && nw.len == 1) {
                break; // Early exit!
            }
        }

        nw = SVLV_consume_one(view);

        // --- SHUNTING-YARD PROCESSING ---
        if (is_digit(nw.start[0]) || is_alpha(nw.start[0])) {
            SVL_p_push(&ot, nw);
            expect_operator = true; // Next, we look for an operator (e.g., '+')
        } else if (nw.start[0] == '(' && nw.len == 1) {
            SVL_p_push(&operator_stack, nw);
            expect_operator = false; // Inside '(', we look for a new value
        } else if (nw.start[0] == ')' && nw.len == 1) {
            while (operator_stack.len > 0 && SVL_p_inspect_back(&operator_stack)->start[0] != '(') {
                StringView val = SVL_p_pop(&operator_stack);
                SVL_p_push(&ot, val);
            }
            if (operator_stack.len > 0) {
                SVL_p_pop(&operator_stack); // Remove '('
            }
            expect_operator = true; // After ')', we expect an operator
        } else if (is_operator(nw.start[0]) && nw.len == 1) {
            char current_op = nw.start[0];
            while (operator_stack.len > 0 && is_operator(SVL_p_inspect_back(&operator_stack)->start[0]) && precedence(
                       SVL_p_inspect_back(&operator_stack)->start[0]) >= precedence(current_op)) {
                StringView val = SVL_p_pop(&operator_stack);
                SVL_p_push(&ot, val);
            }
            SVL_p_push(&operator_stack, nw);
            expect_operator = false; // After an operator, we expect a value
        }
    }*/
    int paren_depth = 0;

    while (!SVLV_is_empty(view)) {
        StringView nw = SVLV_inspect_back(view);
        {
            char fc = nw.start[0];
            if (is_digit(fc) || is_operator(fc) || is_alpha(fc) || fc == '(' || fc == ')');
            else {
                break;
            }
            if (fc == ';') break;
            if (fc == ')' && nw.len == 1 && paren_depth == 0) break;
        }

        if (expect_operator) {
            if (is_digit(nw.start[0]) || is_alpha(nw.start[0]) || (nw.start[0] == '(' && nw.len == 1)) {
                break;
            }
        } else {
            if (is_operator(nw.start[0]) && nw.len == 1) {
                break;
            }
        }

        nw = SVLV_consume_one(view);

        if (is_digit(nw.start[0]) || is_alpha(nw.start[0])) {
            SVL_p_push(&ot, nw);
            expect_operator = true;
        } else if (nw.start[0] == '(' && nw.len == 1) {
            paren_depth++;
            SVL_p_push(&operator_stack, nw);
            expect_operator = false;
        } else if (nw.start[0] == ')' && nw.len == 1) {
            paren_depth--;
            while (operator_stack.len > 0 && SVL_p_inspect_back(&operator_stack)->start[0] != '(') {
                StringView val = SVL_p_pop(&operator_stack);
                SVL_p_push(&ot, val);
            }
            if (operator_stack.len > 0) {
                SVL_p_pop(&operator_stack);
            }
            expect_operator = true;
        } else if (is_operator(nw.start[0]) && nw.len == 1) {
            char current_op = nw.start[0];
            while (operator_stack.len > 0 && is_operator(SVL_p_inspect_back(&operator_stack)->start[0]) && precedence(
                       SVL_p_inspect_back(&operator_stack)->start[0]) >= precedence(current_op)) {
                StringView val = SVL_p_pop(&operator_stack);
                SVL_p_push(&ot, val);
            }
            SVL_p_push(&operator_stack, nw);
            expect_operator = false;
        }
    }

    // Flush remaining operators in the stack for the valid portion parsed so far
    while (operator_stack.len > 0) {
        StringView val = SVL_p_pop(&operator_stack);
        if (val.start[0] != '(') {
            SVL_p_push(&ot, val);
        }
    }


    LocStack stack2 = LS_new();

    for (size_t i = 0; i < ot.len; i++) {
        StringView sv = ot.array[i];
        // printf("  i=%zu sv='%.*s' stack_size=%zu\n", i, (int)sv.len, sv.start, stack2.len);
        GlobalPatchList* globals_patch_ptr = is_local ? &global_patch_list : &global_patch_list_inicializer;

        if (is_operator(sv.start[0])) {
            Loc s2 = LS_pop(&stack2);
            Loc s1 = LS_pop(&stack2);

            emit_mov_eax(code_output, s1, globals_patch_ptr, target);

            if (sv.start[0] == '+') emit_op_eax(code_output, 0x03, s2, globals_patch_ptr, target);
            else if (sv.start[0] == '-') emit_op_eax(code_output, 0x2B, s2, globals_patch_ptr, target);
            else if (sv.start[0] == '*') emit_imul_eax(code_output, s2, globals_patch_ptr, target);
            else if (sv.start[0] == '&') emit_op_eax(code_output, 0x23, s2, globals_patch_ptr, target); // AND
            else if (sv.start[0] == '|') emit_op_eax(code_output, 0x0B, s2, globals_patch_ptr, target); // OR
            else if (sv.start[0] == '<' || sv.start[0] == '>' || sv.start[0] == '=' || sv.start[0] == '!') {
                // 1. Do the comparison FIRST while s1 is still naturally in eax.
                switch (s2.kind) {
                    case LOC_IMMEDIATE:
                        BS_write(code_output, 0x81); // CMP eax, imm32
                        BS_write(code_output, 0xF8);
                        BS_write(code_output, (s2.value >> 0) & 0xFF);
                        BS_write(code_output, (s2.value >> 8) & 0xFF);
                        BS_write(code_output, (s2.value >> 16) & 0xFF);
                        BS_write(code_output, (s2.value >> 24) & 0xFF);
                        break;
                    case LOC_VAR:
                    case LOC_STACK_SLOT: {
                        int slot = (s2.kind == LOC_VAR) ? lookup_var(s2.var) : s2.offset;
                        BS_write(code_output, 0x3B); // CMP eax, [rbp-slot]
                        BS_write(code_output, 0x45);
                        BS_write(code_output, (uint8_t) (-slot));
                        break;
                    }
                    case LOC_GLOBAL: {
                        switch (target) {
                            case F_Elf64: {
                                size_t pos = BS_get_cursor(code_output) + 3;
                                BS_write(code_output, 0x8B); // MOV ecx, [addr32]
                                BS_write(code_output, 0x0C);
                                BS_write(code_output, 0x25);
                                BS_write(code_output, 0x00);
                                BS_write(code_output, 0x00);
                                BS_write(code_output, 0x00);
                                BS_write(code_output, 0x00);
                                GPL_register_patch(globals_patch_ptr, (GlobalPatch){
                                                       .index = s2.offset,
                                                       .offset = pos,
                                                       .bit_size = 4,
                                                   });
                                break;
                            }
                            case F_Win64: {
                                size_t pos = BS_get_cursor(code_output) + 2;
                                BS_write(code_output, 0x48);
                                BS_write(code_output, 0xB9); // MOV rcx, imm64
                                BS_write(code_output, 0x00);
                                BS_write(code_output, 0x00);
                                BS_write(code_output, 0x00);
                                BS_write(code_output, 0x00);
                                BS_write(code_output, 0x00);
                                BS_write(code_output, 0x00);
                                BS_write(code_output, 0x00);
                                BS_write(code_output, 0x00);
                                BS_write(code_output, 0x8B);
                                BS_write(code_output, 0x09); // MOV ecx, [rcx]
                                GPL_register_patch(globals_patch_ptr, (GlobalPatch){
                                                       .index = s2.offset,
                                                       .offset = pos,
                                                       .bit_size = 8,
                                                   });
                                break;
                            }
                        }
                        BS_write(code_output, 0x3B); // CMP eax, ecx
                        BS_write(code_output, 0xC1);
                        break;
                    }
                }

                // 2. Load your truth values. These MOV instructions do NOT affect EFLAGS.
                // MOV eax, 1
                BS_write(code_output, 0xB8);
                BS_write(code_output, 0x01);
                BS_write(code_output, 0x00);
                BS_write(code_output, 0x00);
                BS_write(code_output, 0x00);

                // MOV ecx, 0
                BS_write(code_output, 0xB9);
                BS_write(code_output, 0x00);
                BS_write(code_output, 0x00);
                BS_write(code_output, 0x00);
                BS_write(code_output, 0x00);

                // 3. Conditionally move 0 into eax if the condition is false.
                // The flags from the CMP we ran up top are still valid!
                BS_write(code_output, 0x0F);
                // switch (sv.start[0]) {
                //     case '<': BS_write(code_output, 0x43);
                //         break; // CMOVAE (not less)
                //     case '>': BS_write(code_output, 0x46);
                //         break; // CMOVBE (not greater)
                //     case '=': BS_write(code_output, 0x45);
                //         break; // CMOVNE (not equal)
                //     case '!': BS_write(code_output, 0x44);
                //         break; // CMOVE (not-not-equal, i.e. equal → false)
                //     default: {
                //         fprintf(stderr, "[ERROR] <internal> invalid switch path\n");
                //         exit(1);
                //     }
                // }
                switch (sv.start[0]) {
                    case '<': BS_write(code_output, 0x4D); break; // CMOVGE (not signed-less)
                    case '>': BS_write(code_output, 0x4E); break; // CMOVLE (not signed-greater)
                    case '=': BS_write(code_output, 0x45); break; // CMOVNE (not equal)
                    case '!': BS_write(code_output, 0x44); break; // CMOVE  (not not-equal)
                    default:
                        fprintf(stderr, "[ERROR] <internal> invalid switch path\n");
                        exit(1);
                }
                BS_write(code_output, 0xC1); // ModRM: eax, ecx
            } else if (sv.start[0] == '/' || sv.start[0] == '%') {
                // idiv cannot take an immediate or global — spill s2 to stack if needed
                int s2_slot = 0;
                switch (s2.kind) {
                    case LOC_IMMEDIATE: {
                        if (is_local)
                            s2_slot = alloc_tmp_stack_slot(get_frame());
                        else {
                            s2_slot = global_frame.next_offset;
                            global_frame.next_offset += 4;
                        }
                        BS_write(code_output, 0xC7);
                        BS_write(code_output, 0x45);
                        BS_write(code_output, (uint8_t) (-s2_slot));
                        BS_write(code_output, (s2.value >> 0) & 0xFF);
                        BS_write(code_output, (s2.value >> 8) & 0xFF);
                        BS_write(code_output, (s2.value >> 16) & 0xFF);
                        BS_write(code_output, (s2.value >> 24) & 0xFF);
                        break;
                    }
                    case LOC_VAR:
                        s2_slot = lookup_var(s2.var);
                        break;
                    case LOC_STACK_SLOT:
                        s2_slot = s2.offset;
                        break;
                    case LOC_GLOBAL: {
                        if (is_local)
                            s2_slot = alloc_tmp_stack_slot(get_frame());
                        else {
                            s2_slot = global_frame.next_offset;
                            global_frame.next_offset += 4;
                        }
                        switch (target) {
                            case F_Elf64: {
                                size_t pos = BS_get_cursor(code_output) + 3;
                                BS_write(code_output, 0x8B); // MOV ecx, [addr32]
                                BS_write(code_output, 0x0C);
                                BS_write(code_output, 0x25);
                                BS_write(code_output, 0x00);
                                BS_write(code_output, 0x00);
                                BS_write(code_output, 0x00);
                                BS_write(code_output, 0x00);
                                GPL_register_patch(globals_patch_ptr, (GlobalPatch){
                                                       .index = s2.offset,
                                                       .offset = pos,
                                                       .bit_size = 4,
                                                   });
                                break;
                            }
                            case F_Win64: {
                                size_t pos = BS_get_cursor(code_output) + 2;
                                BS_write(code_output, 0x48);
                                BS_write(code_output, 0xB9); // MOV rcx, imm64
                                BS_write(code_output, 0x00);
                                BS_write(code_output, 0x00);
                                BS_write(code_output, 0x00);
                                BS_write(code_output, 0x00);
                                BS_write(code_output, 0x00);
                                BS_write(code_output, 0x00);
                                BS_write(code_output, 0x00);
                                BS_write(code_output, 0x00);
                                BS_write(code_output, 0x8B);
                                BS_write(code_output, 0x09); // MOV ecx, [rcx]
                                GPL_register_patch(globals_patch_ptr, (GlobalPatch){
                                                       .index = s2.offset,
                                                       .offset = pos,
                                                       .bit_size = 8,
                                                   });
                                break;
                            }
                        }
                        BS_write(code_output, 0x89); // MOV [rbp - s2_slot], ecx
                        BS_write(code_output, 0x4D);
                        BS_write(code_output, (uint8_t) (-s2_slot));
                        break;
                    }
                }

                // RUNTIME ZERO DIV CHECK

                // cmp dword ptr [rbp - s2_slot], 0
                BS_write(code_output, 0x83);
                BS_write(code_output, 0x7D);
                BS_write(code_output, (uint8_t) (-s2_slot));
                BS_write(code_output, 0x00);

                // jnz +5  (skip the call)
                BS_write(code_output, 0x75);
                BS_write(code_output, 0x05);

                // call <__rt_exception__zero_div>  (5 bytes, needs patching like your entry_point)
                size_t divz_patch_cursor = BS_get_cursor(code_output) + 1;
                uint8_t call_divz[] = {0xe8, 0x00, 0x00, 0x00, 0x00};
                BS_write_array(code_output, sizeof(call_divz), call_divz);

                FCPL_register_pach(&function_patch_list, (FunctionCallPatch){
                                       .name = SV_from_string_len("__rt_exception__zero_div", 24),
                                       .offset = divz_patch_cursor,
                                       .relative = true,
                                       .bit_size = 4,
                                       .is_local = is_local,
                                   });

                // END OF RUNTIME ZERO DIV CHECK


                // cdq — sign-extend eax into edx:eax
                BS_write(code_output, 0x99);

                // idiv dword ptr [rbp - s2_slot]
                BS_write(code_output, 0xF7);
                BS_write(code_output, 0x7D);
                BS_write(code_output, (uint8_t) (-s2_slot));

                // for '%', result is in edx — move it to eax
                if (sv.start[0] == '%') {
                    BS_write(code_output, 0x89); // mov eax, edx
                    BS_write(code_output, 0xD0);
                }
            }


            int slot; // = alloc_stack_slot(get_frame());
            if (is_local)
                slot = alloc_stack_slot(get_frame());
            else {
                slot = global_frame.next_offset;
                global_frame.next_offset += 4;
            }
            // MOV [rbp - slot], eax
            BS_write(code_output, 0x89);
            BS_write(code_output, 0x45);
            BS_write(code_output, (uint8_t) (-slot));

            LS_push(&stack2, (Loc){.kind = LOC_STACK_SLOT, .offset = slot});
        } else if (is_alpha(sv.start[0])) {
            if (exists_var(sv)) {
                // variable — defer load, just record it
                LS_push(&stack2, (Loc){.kind = LOC_VAR, .var = sv});
            } else if (GR_has_global(&globals_registry, sv)) {
                size_t global_index = GR_lookup_global_index(&globals_registry, sv);
                LS_push(&stack2, (Loc){.kind = LOC_GLOBAL, .offset = global_index});
            } else {
                fprintf(stderr, "[ERROR] unknown variable '%.*s'\n", (int) sv.len, sv.start);
                exit(1);
            }
        } else {
            // numeric literal
            LS_push(&stack2, (Loc){.kind = LOC_IMMEDIATE, .value = atoi(sv.start)});
        }
    }

    Loc last = LS_pop(&stack2);
    if (stack2.len > 0) {
        fprintf(stderr, "[ERROR] <internal> expr-eval stack in not empty after processing\n");
        exit(1);
    }

    LS_free(&stack2);
    SVL_p_free(&ot);
    SVL_p_free(&operator_stack);
    return last;
}
