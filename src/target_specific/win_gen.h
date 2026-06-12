#ifndef SIMPLECOMPILERINC_2_WIN_GEN_H
#define SIMPLECOMPILERINC_2_WIN_GEN_H
#include <stdint.h>

#include "../codegen.h"

#define DOS_HDR_SZ 64

#define FILE_ALIGN                    512

// Note: PE_HDR_SZ refers to the standard IMAGE_FILE_HEADER.
// If you need the full IMAGE_NT_HEADERS64, use 264 (0x108) instead.
#define PE_HDR_SZ                     24
#define OPT_HDR_SZ                    240
#define SEC_HDR_SZ                    40
#define SEC_ALIGN                     4096
#define IMPORT_DIR_ENTRY_SZ           20
#define IAT_ENTRY_SZ                  8
#define IMPORT_LOOKUP_TBL_ENTRY_SZ    8

// Note: NAME_TABLE_ENTRY_SZ is variable length (2 bytes hint + null-terminated string).
// This macro represents the minimum structural overhead (Hint + null terminator).
#define NAME_TABLE_ENTRY_MIN_SZ       3

// void write_win64(const char*, const uint8_t*, size_t, uint64_t, FunctionsRegistry*, uint64_t str_consts_load_addr, StringViewList* string_consts, uint32_t num_sections, StringViewList importing_funcs, uint64_t import_table_load_addr, size_t bss_size);
void write_win64(const char* path,
                 uint8_t* code, size_t code_len, uint64_t load_addr,
                 const FunctionsRegistry* fr,
                 const StringViewList* string_consts, const StringConstAddrRelocationList* string_consts_relocations,
                 uint32_t num_sections, StringViewList importing_funcs,
                 // uint64_t import_table_load_addr,
                 size_t bss_size, size_t bss_init_code_len
);


#endif //SIMPLECOMPILERINC_2_WIN_GEN_H