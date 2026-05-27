#include "main.h"
#include <stdint.h>
#include <stdio.h>
#include <linux/limits.h>

#include "utils.h"
#include "compiler.h"

StringViewList SVL_from_args(int, char*[]);

int main(int argc, char* argv[]) {
    StringViewList args = SVL_from_args(argc, argv);

    if (args.len != 2) {
        fprintf(stderr, "[ERROR] expected one argument\n");
        printf("[USAGE] %.*s <src>\n", (int) args.array[0].len, args.array[0].start);
        return 1;
    }

    // printf("args: ");
    // for (size_t args_iter = 0; args_iter < args.len; args_iter++) {
    //     printf("'");
    //     SV_p_printf(&args.array[args_iter]);
    //     if (args_iter + 1 != args.len)
    //         printf("', ");
    //     else
    //         putc('\'', stdout);
    // }
    // putc('\n', stdout);

    StringView input_src_path = args.array[1];

    size_t file_size;
    // printf("reading input file '%.*s'\n", (int)input_src_path.len, input_src_path.start);
    char* content = read_file(input_src_path.start, &file_size);
    StringView src_contents = SV_from_string_len(content, file_size);




    char resolved_path_raw[PATH_MAX];
    if (realpath(args.array[1].start, resolved_path_raw) == NULL) {
        perror("realpath");
        exit(1);
    }

    SVL_p_free(&args);

    StringView full_path = SV_from_string(resolved_path_raw);

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
    // const StringView sv = SV_from_string("");

    process(&src_contents, &full_path);

    free(content);

    printf("compilation successful\n");
    return 0;
}


StringViewList SVL_from_args(const int argc, char* argv[]) {
    StringViewList ot = SVL_new();

    for (int i = 0; i < argc; i++) {
        SVL_p_push(&ot, SV_from_string(argv[i]));
    }

    return ot;
}
