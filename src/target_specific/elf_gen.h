//
// Created by frantisek on 26. 5. 2026.
//

#ifndef SIMPLECOMPILERINC_2_ELF_GEN_H
#define SIMPLECOMPILERINC_2_ELF_GEN_H
#include <stdint.h>
#include <stddef.h>
#include "../emit.h"

void write_elf64(
    const char *path,
    const uint8_t *code, size_t code_len, uint64_t load_addr,
    FunctionsRegistry* fr,
    uint64_t str_consts_load_addr, StringViewList* string_consts,
    uint64_t bss_load_addr, size_t bss_size
);


#endif //SIMPLECOMPILERINC_2_ELF_GEN_H