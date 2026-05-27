//
// Created by frantisek on 26. 5. 2026.
//

#ifndef SIMPLECOMPILERINC_2_ELF_GEN_H
#define SIMPLECOMPILERINC_2_ELF_GEN_H
#include <stdint.h>
#include <stddef.h>
#include "emit.h"

void write_elf64(const char*, const uint8_t*, size_t, uint64_t);


#endif //SIMPLECOMPILERINC_2_ELF_GEN_H