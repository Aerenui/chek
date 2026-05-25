#include "compiler.h"

#include <stdio.h>
#include <time.h>

#include "utils.h"

static size_t tmp_cnt = 0;

void get_stmt(StringViewListView *);

bool is_operator(char);


StringViewList p_tokenize(const StringView *sv) {
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

bool is_stmt_next(StringViewListView *view) {
    StringView s1 = SVLV_inspect_back(view);
    if (SV__pv_cmp_eq(&s1, "int", 3)) return true;
    if (SV__pv_cmp_eq(&s1, "if", 2)) return true;
    if (SV__pv_cmp_eq(&s1, "print", 5)) return true;

    return false;
}

void get_stmt_block(StringViewListView *view) {
    while (view->len > 0) {
        if (!is_stmt_next(view)) break;
        get_stmt(view);
    }
}

void process(const StringView *input) {
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

    get_stmt_block(&svlv);

    SVL_p_free(&svl);
}


void get_semicolon(StringViewListView *view) {
    StringView semicolon = SVLV_consume_one(view);
    if (!SV__pv_cmp_eq(&semicolon, ";", 1)) {
        // error
        fprintf(stderr, "[ERROR] expected ';', got '%.*s'\n", (int) semicolon.len, semicolon.start);
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
    if (op == '<' || op == '>' || op == '=') return 0;
    if (op == '+' || op == '-') return 1;
    if (op == '*' || op == '/') return 2;

    fprintf(stderr, "[ERROR] <intern> precedence ( '%c' ) {%i}\n", op, op);
    exit(1);
}

bool is_operator(char c) {
    return c == '+' || c == '-' || c == '*' || c == '/' || c == '<' || c == '>' || c == '=';
}

char *format_operator(char c) {
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


StringView get_int_expr(StringViewListView *view) {
    StringViewList ot = SVL_new();
    StringViewList operator_stack = SVL_new();
    bool expect_operator = false;

    while (!SVLV_is_empty(view)) {
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
    }

    // Flush remaining operators in the stack for the valid portion parsed so far
    while (operator_stack.len > 0) {
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


    StringViewList stack = SVL_new();
    for (size_t i = 0; i < ot.len; i++) {
        // putc('\'', stdout);
        StringView sv = ot.array[i];
        if (is_operator(sv.start[0])) {
            StringView s2 = SVL_p_pop(&stack);
            StringView s1 = SVL_p_pop(&stack);

            if (sv.start[0] == '<' || sv.start[0] == '>' || sv.start[0] == '=') {
                printf("  %%cmp_%lu = icmp eq i32 %.*s, %.*s\n", tmp_cnt, (int) s1.len, s1.start, (int) s2.len,
                       s2.start);
                printf("  %%tmp_%lu = zext i1 %%cmp_%lu to i32\n", tmp_cnt, tmp_cnt);
            } else {
                // +, -, *, /
                printf("  %%tmp_%lu = %s nsw i32 %.*s, %.*s\n", tmp_cnt, format_operator(sv.start[0]), (int) s1.len,
                       s1.start, (int) s2.len, s2.start);
            }
            char *s3 = calloc(30, 1);
            snprintf(s3, 30, "%%tmp_%lu", tmp_cnt);
            SVL_p_push(&stack, SV_from_string(s3));
            tmp_cnt++;
        } else {
            if (is_alpha(sv.start[0])) {
                printf("  %%tmp_%lu = load i32, ptr %%var_%.*s, align 4\n", tmp_cnt, (int) sv.len, sv.start);
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
    // printf("  store i32 %.*s", (int)last.len, last.start);

    SVL_p_free(&stack);
    SVL_p_free(&ot);
    SVL_p_free(&operator_stack);
    return last;
}


void get_int_var_dec(StringViewListView *view) {
    StringView var_name = SVLV_consume_one(view);
    StringView assign = SVLV_consume_one(view);
    if (!SV__pv_cmp_eq(&assign, ":", 1)) {
        // error
        fprintf(stderr, "[ERROR] expected ':' in variable declaration, got '%.*s'\n", (int) assign.len, assign.start);
        exit(1);
    }
    printf("  %%var_%.*s = alloca i32, align 4\n", (int) var_name.len, var_name.start);

    StringView last = get_int_expr(view);
    printf("  store i32 %.*s, ptr %%var_%.*s, align 4\n", (int) last.len, last.start, (int) var_name.len,
           var_name.start);
    // printf(", ptr %%var_%.*s, align 4\n", (int) var_name.len, var_name.start);

    get_semicolon(view);
}

void get_if_conditional(StringViewListView *view) {
    const StringView cond = get_int_expr(view);

    StringView then = SVLV_consume_one(view);
    if (!SV__pv_cmp_eq(&then, "then", 4)) {
        fprintf(stderr, "[ERROR] expected 'then' after expression in if conditional, got '%.*s'\n", (int) then.len, then.start);
        exit(1);
    }

    const size_t tmp = tmp_cnt;
    tmp_cnt++;
    printf("  %%cond_%lu = icmp ne i32 %.*s, 0\n", tmp, (int) cond.len, cond.start);
    printf("  br i1 %%cond_%lu, label %%if_then_%lu, label %%if_else_%lu\n", tmp, tmp, tmp);
    printf("if_then_%lu:\n", tmp);

    get_stmt_block(view);

    printf("  br label %%if_end_%lu\n", tmp);
    printf("if_else_%lu:\n", tmp);

    StringView else_keyword = SVLV_inspect_back(view);
    if (SV__pv_cmp_eq(&else_keyword, "else", 4)) {
        SVLV_consume_one(view);
        get_stmt_block(view);
    }

    printf("  br label %%if_end_%lu\n", tmp);
    printf("if_end_%lu:\n", tmp);

    StringView end = SVLV_consume_one(view);
    if (!SV__pv_cmp_eq(&end, "end", 3)) {
        fprintf(stderr, "[ERROR] expected 'end' after if conditional, got '%.*s'\n", (int) end.len, end.start);
        exit(1);
    }
}


void get_stmt(StringViewListView *view) {
    StringView s1 = SVLV_consume_one(view);
    if (SV__pv_cmp_eq(&s1, "int", 3)) {
        get_int_var_dec(view);
    } else if (SV__pv_cmp_eq(&s1, "if", 2)) {
        // printf("STMT: conditional\n");
        get_if_conditional(view);
        // %cond = icmp ne i32 %val, 0
        //br i1 %cond, label %true, label %false
    } else if (SV__pv_cmp_eq(&s1, "print", 5)) {
        printf("STMT: print 1 2 3\n");
    } else {
        printf("STMT: unknown\n");
    }
}
