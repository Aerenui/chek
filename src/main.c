#include "main.h"
#include <stdint.h>
#include <stdio.h>
#include <linux/limits.h>

#include "utils.h"
#include "compiler.h"

StringViewList SVL_from_args(int, char*[]);

int main(int argc, char* argv[]) {
    StringViewList args = SVL_from_args(argc, argv);

    const char* output_name = "out"; // default
    StringView input_src_path = {0};

    // Parse arguments manually
    size_t i = 1;
    while (i < args.len) {
        StringView arg = args.array[i];
        if (SV__pv_cmp_eq(&arg, "-o", 2)) {
            if (i + 1 >= args.len) {
                fprintf(stderr, "[ERROR] -o requires an argument\n");
                SVL_p_free(&args);
                return 1;
            }
            output_name = args.array[i + 1].start;
            i += 2;
        } else {
            input_src_path = arg;
            i++;
        }
    }

    if (input_src_path.start == NULL) {
        fprintf(stderr, "[ERROR] expected one argument\n");
        printf("[USAGE] %.*s [-o <out>] <src>\n",
               (int)args.array[0].len, args.array[0].start);
        SVL_p_free(&args);
        return 1;
    }

    size_t file_size;
    char* content = read_file(input_src_path.start, &file_size);

    StringView src_contents = SV_from_string_len(content, file_size);


    char resolved_path_raw[PATH_MAX];
    if (realpath(args.array[1].start, resolved_path_raw) == NULL) {
        perror("realpath");
        exit(1);
    }

    SVL_p_free(&args);

    StringView full_path = SV_from_string(resolved_path_raw);

    process(&src_contents, &full_path, output_name);

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
