#include "main.h"
#include <stdint.h>
#include <stdio.h>
// #include <linux/limits.h>
#include <errno.h>
#include <limits.h>
#ifndef PATH_MAX
  #define PATH_MAX 260
#endif

#if defined(_WIN32) || defined(__MINGW32__)
  #include <stdlib.h>
  #define realpath(rel, abs) _fullpath((abs), (rel), PATH_MAX)
#else
  #include <stdlib.h>
#endif

#include "utils.h"
#include "compiler.h"


CompilerFlags compiler_flags;

StringViewList SVL_from_args(int, char*[]);

int main(const int argc, char* argv[]) {
    StringViewList args = SVL_from_args(argc, argv);

    const char* output_name = "out"; // default
    CompilerTarget default_target;
#if defined(_WIN64)
    CompilerTarget target = F_Win64;
    default_target = F_Win64;
    bool has_target_default = true;
#elif defined(__ELF__)
    CompilerTarget target = F_Elf64;
    default_target = F_Elf64;
    bool has_target_default = true;
#else
    CompilerTarget target = F_Elf64;
    default_target = F_Elf64;
    bool has_target_default = false;
#endif
    bool target_set = false;

    StringView input_src_path = {0};

    uint8_t opt_level = 2;

    size_t i = 1;
    while (i < args.len) {
        StringView arg = args.array[i];
        if (SV_pv_cmp_eq(&arg, "-o", 2)) {
            if (i + 1 >= args.len) {
                fprintf(stderr, "[ERROR] -o requires an argument\n");
                SVL_p_free(&args);
                return 1;
            }
            output_name = args.array[i + 1].start;
            i += 2;
        } else if (SV_pv_starts_with(&arg, "-O", 2)) {
            // two cases `-O1` or `-O 1`
            const char* val;
            int val_len;
            if (arg.len == 2) { // case -O 1
                if (i + 1 >= args.len) {
                    fprintf(stderr, "[ERROR] -O requires an argument\n");
                    SVL_p_free(&args);
                    return 1;
                }
                val = args.array[i + 1].start;
                val_len = (int)args.array[i + 1].len;
                i += 2;
            } else {
                val = arg.start + 2;
                val_len = (int)arg.len - 2;
                i += 1;
            }
            char* end;
            errno = 0;
            const long res = strtol(val, &end, 10);
            if (errno != 0 || end == val || *end != '\0' || (res != 0 && res != 1 && res != 2)) {
                fprintf(stderr, "[ERROR] -O optimization level requires 0,1 or 2, got '%.*s'\n", val_len, val);
                SVL_p_free(&args);
                return 1;
            }
            opt_level = (uint8_t)res;

        } else if (SV_pv_cmp_eq(&arg, "-t", 2)) {
            if (i + 1 >= args.len) {
                fprintf(stderr, "[ERROR] -t requires an argument\n");
                SVL_p_free(&args);
                return 1;
            }
            StringView fmt = args.array[i + 1];
            if (SV_pv_cmp_eq(&fmt, "win64", 5)) {
                target = F_Win64;
            } else if (SV_pv_cmp_eq(&fmt, "elf64", 5)) {
                target = F_Elf64;
            } else {
                fprintf(stderr, "[ERROR] -t: unknown target '"SV_format"' (expected win64 or elf64)\n", SV_v_args(fmt));
                SVL_p_free(&args);
                return 1;
            }
            target_set = true;
            i += 2;
        } else {
            input_src_path = arg;
            i++;
        }
    }

    if (input_src_path.start == NULL) {
        fprintf(stderr, "[ERROR] expected one argument\n");
        printf("[USAGE] "SV_format" [-o <out>] [-O 0|1] [-t win64|elf64] <src>\n"
               "\t-o <out>\t output executable path [default: out]\n"
               "\t-O <opt_level>\t level of optimization (0, 1 or 2) [default: 2]\n"
               "\t-t <target>\t target platform (elf64, or win64) %s\n"
               "\t<src>\t\t source file\n",
               // ReSharper disable line CppDFAConstantConditions
               SV_v_args(args.array[0]), has_target_default && default_target == F_Elf64 ? "[default: elf64]" : (has_target_default && default_target == F_Win64 ? "[default: win64]" : ""));
        SVL_p_free(&args);
        return 1;
    }

    // ReSharper disable once CppDFAConstantConditions
    if (!target_set && !has_target_default) {
        fprintf(stderr, "[ERROR] target not set or derived from system, please specify target manually\n");
        exit(1);
    }



    size_t file_size;
    char* content = read_file(input_src_path.start, &file_size);

    const StringView src_contents = SV_from_string_len(content, file_size);


    char resolved_path_raw[PATH_MAX];
    if (realpath(args.array[1].start, resolved_path_raw) == NULL) {
        perror("realpath");
        exit(1);
    }

    SVL_p_free(&args);

    StringView full_path = SV_from_string(resolved_path_raw);

    // printf("[DEBUG] opt_level=%u\n", opt_level);
    switch (opt_level) {
        case 0: {
            compiler_flags = (CompilerFlags) {
                .constants_folding = false,
                .constant_variable_resolution = false,
                .constant_branch_evaluation = false,
                .algebraic_optimization = false,
                .function_inlining = false,
            };
            break;
        }
        case 1: {
            compiler_flags = (CompilerFlags) {
                .constants_folding = true,
                .constant_variable_resolution = false,
                .constant_branch_evaluation = false,
                .algebraic_optimization = true,
                .function_inlining = false,
            };
            break;
        }
        case 2: {
            compiler_flags = (CompilerFlags) {
                .constants_folding = true,
                .constant_variable_resolution = true,
                .constant_branch_evaluation = true,
                .algebraic_optimization = true,
                .function_inlining = true,
            };
            break;
        }
        default: {
            fprintf(stderr, "[ERROR] <internal> invalid opt_level after args parsing, got '%u'\n", opt_level);
        }
    }


    process(&src_contents, &full_path, output_name, target);

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

