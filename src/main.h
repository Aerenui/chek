#ifndef SIMPLECOMPILERINC_2_MAIN_H
#define SIMPLECOMPILERINC_2_MAIN_H
#include <stdbool.h>

typedef enum  { F_Elf64, F_Win64 } CompilerTarget;

typedef struct {
    bool constants_folding;
    bool constant_variable_resolution;
    bool constant_branch_evaluation;
    bool algebraic_optimization;
} CompilerFlags;

// shared state across the compiler
extern CompilerFlags compiler_flags;

#endif //SIMPLECOMPILERINC_2_MAIN_H