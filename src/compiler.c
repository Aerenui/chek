#include "compiler.h"

#include <stdio.h>
#include "utils.h"

static size_t tmp_cnt = 0;

void get_stmt(StringViewListView*);
bool is_operator(char);


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
            sv->start[i] == ')' || sv->start[i] == ';' ||
            sv->start[i] == '=') {
            SVL_p_push(&ot, SV_lrslice_from_SV(sv, i, 1));
            i++;
            continue;
            }
        // multi-char tokens: identifiers and integer literals
        size_t start = i;
        while (i < sv->len &&
               sv->start[i] != ' ' && sv->start[i] != '\t' &&
               !is_operator(sv->start[i]) &&
               sv->start[i] != '(' && sv->start[i] != ')' &&
               sv->start[i] != ';' && sv->start[i] != '=') {
            i++;
               }
        SVL_p_push(&ot, SV_lrslice_from_SV(sv, start, i - start));
    }
    return ot;
}

void process(const StringView* input) {
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
    while (svlv.len > 0)
        get_stmt(&svlv);

    SVL_p_free(&svl);
}



void get_semicolon(StringViewListView* view) {
    StringView semicolon = SVLV_consume_one(view);
    if (!SV__pv_cmp_eq(&semicolon, ";", 1)) {
        // error
        fprintf(stderr, "[ERROR] expected ';', got '%.*s'\n", (int)semicolon.len, semicolon.start);
        exit(1);
    }
}

bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int precedence(char op) {
    if (op == '+' || op == '-') return 1;
    if (op == '*' || op == '/') return 2;
    return 0;
}

bool is_operator(char c) {
    return c == '+' ||  c == '-' ||  c == '*' ||  c == '/';
}

char* format_operator(char c) {
    if (c == '+')
        return "add";
    else if (c == '-')
        return "sub";
    else if (c == '*')
        return "mul";
    else if (c == '/')
        return "div";
    else {
        fprintf(stderr, "[ERROR] unknown operator '%c'\n", c);
        exit(1);
    }
}


void get_int_expr(StringViewListView* view) {
    StringViewList ot = SVL_new();
    StringViewList operator_stack = SVL_new();
    bool expect_operator = false;

    while (!SVLV_is_empty(view)) {
        if (SVLV_inspect_back(view).start[0] == ';') break;
        StringView nw = SVLV_consume_one(view);

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

        // --- SHUNTING-YARD PROCESSING ---
        if (is_digit(nw.start[0]) || is_alpha(nw.start[0])) {
            SVL_p_push(&ot, nw);
            expect_operator = true; // Next, we look for an operator (e.g., '+')
        } else if (nw.start[0] == '(' && nw.len == 1) {
            SVL_p_push(&operator_stack, nw);
            expect_operator = false; // Inside '(', we look for a new value
        } else if (nw.start[0] == ')' && nw.len == 1) {
            while(operator_stack.len > 0 && SVL_p_inspect_back(&operator_stack)->start[0] != '(') {
                StringView val = SVL_p_pop(&operator_stack);
                SVL_p_push(&ot, val);
            }
            if (operator_stack.len > 0) {
                SVL_p_pop(&operator_stack); // Remove '('
            }
            expect_operator = true; // After ')', we expect an operator
        } else if (is_operator(nw.start[0]) && nw.len == 1) {
            char current_op = nw.start[0];
            while(operator_stack.len > 0 && is_operator(SVL_p_inspect_back(&operator_stack)->start[0]) && precedence(SVL_p_inspect_back(&operator_stack)->start[0]) >= precedence(current_op)) {
                StringView val = SVL_p_pop(&operator_stack);
                SVL_p_push(&ot, val);
            }
            SVL_p_push(&operator_stack, nw);
            expect_operator = false; // After an operator, we expect a value
        }
    }

    // Flush remaining operators in the stack for the valid portion parsed so far
    while(operator_stack.len > 0) {
        StringView val = SVL_p_pop(&operator_stack);
        if (val.start[0] != '(') {
            SVL_p_push(&ot, val);
        }
    }

    /*printf("EXPR: [");
    for (size_t i =0; i < ot.len; i++) {
        putc('\'', stdout);
        SV_p_printf(&ot.array[i]);
        if (i + 1 != ot.len)
            printf("', ");
        else
            printf("']\n");
    }*/
    // printf("EXPR: [");

    // %1 = add nsw i32 2, 4
    StringViewList stack = SVL_new();
    for (size_t i =0; i < ot.len; i++) {
        // putc('\'', stdout);
        StringView sv = ot.array[i];
        if (is_operator(sv.start[0])) {

            StringView s2 = SVL_p_pop(&stack);
            StringView s1 = SVL_p_pop(&stack);

            printf("  %%tmp_%lu = %s nsw i32 %.*s, %.*s\n", tmp_cnt, format_operator(sv.start[0]), (int)s1.len, s1.start, (int)s2.len, s2.start);
            char *s3 = calloc(30, 1);
            snprintf(s3, 30, "%%tmp_%lu", tmp_cnt);
            SVL_p_push(&stack, SV_from_string(s3));
            tmp_cnt++;
        } else {
            if (is_alpha(sv.start[0])) {
                printf("  %%tmp_%lu = load i32, ptr %%var_%.*s, align 4\n", tmp_cnt, (int)sv.len, sv.start);
                // char s3[30] = {0};
                char *s3 = calloc(30, 1);
                snprintf(s3, 30, "%%tmp_%lu", tmp_cnt);
                SVL_p_push(&stack, SV_from_string(s3));
                tmp_cnt++;
            } else {
                SVL_p_push(&stack, sv);
            }
        }
    }
    StringView last = SVL_p_pop(&stack);
    printf("  store i32 %.*s", (int)last.len, last.start);

    SVL_p_free(&stack);
    SVL_p_free(&ot);
    SVL_p_free(&operator_stack);
}


void get_int_var_dec(StringViewListView* view) {
    StringView var_name = SVLV_consume_one(view);
    StringView eq = SVLV_consume_one(view);
    if (!SV__pv_cmp_eq(&eq, "=", 1)) {
        // error
        fprintf(stderr, "[ERROR] expected '=' in variable declaration\n");
        exit(1);
    }
    printf("  %%var_%.*s = alloca i32, align 4\n", (int) var_name.len, var_name.start);

    get_int_expr(view);
    printf(", ptr %%var_%.*s, align 4\n", (int) var_name.len, var_name.start);

    get_semicolon(view);
}



void get_stmt(StringViewListView* view) {
    StringView s1 = SVLV_consume_one(view);
    if (SV__pv_cmp_eq(&s1, "int", 3)) {
        get_int_var_dec(view);
    } else if(SV__pv_cmp_eq(&s1, "if", 2)) {
        printf("STMT: conditional\n");
    } else if(SV__pv_cmp_eq(&s1, "print", 5)) {
        printf("STMT: print 1 2 3\n");
    } else {
        printf("STMT: unknown\n");
    }
}