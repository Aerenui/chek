#include "main.h"
#include <stdint.h>
#include <stdio.h>

#include "utils.h"
#include "compiler.h"

StringViewList SVL_from_args(int, char*[]);

int main(int argc, char* argv[]) {

    StringViewList args = SVL_from_args(argc, argv);

    printf("args: ");
    for (size_t args_iter = 0; args_iter < args.len; args_iter++) {
        printf("'");
        SV_p_printf(&args.array[args_iter]);
        if (args_iter + 1 != args.len)
            printf("', ");
        else
            putc('\'', stdout);
    }
    putc('\n', stdout);

    SVL_p_free(&args);

    /*const StringView sv = SV_from_string("int a: 5 * 3 + 10 + 1 ;\n" \
        "int b: a+5*(1+3);\n" \
        "int c: a = 2;\n" \
        "if c then\n" \
        "  set a: a+1;\n" \
        "end\n" \
        "int d: 5;\n" \
        "print a 10;"
        );*/
    // const StringView sv = SV_from_string("int a: 5 * 3 + 10 + 1; if 1 < 2 then set a: a + 5; else set a: a - 5; end int c: 10;");
    const StringView sv = SV_from_string("int a: 97; if 1 < 2 then set a: a + 2; else set a: a - 1; end print a 10;");
    process(&sv);
    printf("DONE\n");
    return 0;
}



StringViewList SVL_from_args(const int argc, char* argv[]) {
    StringViewList ot = SVL_new();

    for (int i = 0; i < argc; i++) {
        SVL_p_push(&ot, SV_from_string(argv[i]));
    }

    return ot;
}