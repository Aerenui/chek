//
// Created by frantisek on 28. 5. 2026.
//

#include "win_gen.h"

#include <stdio.h>
#include <string.h>

void write8(FILE* f, const uint8_t val) {
    fwrite(&val, 1, 1, f);
}

void write16(FILE* f, const uint16_t val) {
    fwrite(&val, 2, 1, f);
}

void write32(FILE* f, const uint32_t val) {
    fwrite(&val, 4, 1, f);
}

void write64(FILE* f, const uint64_t val) {
    fwrite(&val, 8, 1, f);
}

/* Round value up to alignment. */
static uint32_t align_to(uint32_t value, uint32_t alignment) {
    uint32_t remainder = value % alignment;

    if (remainder == 0) {
        return value;
    }

    return value + (alignment - remainder);
}

/* Helper — writes exactly 8 bytes for section name */
static void write_name8(FILE* f, const char* name) {
    char buf[8] = {0};
    strncpy(buf, name, 8);
    fwrite(buf, 1, 8, f);
}


void write_win64(const char* path,
                 const uint8_t* code, size_t code_len, uint64_t load_addr,
                 FunctionsRegistry* fr,
                 uint64_t str_consts_load_addr, StringViewList* string_consts,
                 uint32_t num_sections, StringViewList importing_funcs,
                 uint64_t import_table_load_addr
) {





    // StringViewList importing_funcs = SVL_new();
    // SVL_p_push(&importing_funcs, SV_from_string_len("GetStdHandle", 12));
    // SVL_p_push(&importing_funcs, SV_from_string_len("WriteFile", 9));
    // SVL_p_push(&importing_funcs, SV_from_string_len("ExitProcess", 11));

    size_t name_table_sz = 0;
    for (size_t i = 0; i < importing_funcs.len; i++) {
        size_t entry_sz = 2 + importing_funcs.array[i].len + 1; // hint + name + null
        entry_sz = (entry_sz + 1) & ~1;                         // round up to even
        name_table_sz += entry_sz;
    }

    uint32_t num_imports = (uint32_t)importing_funcs.len;





    size_t string_consts_size = 1;
    for (size_t n=0; n < string_consts->len; n++) {
        string_consts_size += string_consts->array[n].len + 1; // +1 for NULL
    }


    uint64_t string_consts_size2 = 1;
    uint8_t* string_consts_array = calloc(1, string_consts_size);
    for (size_t n=0; n < string_consts->len; n++) {
        memcpy(string_consts_array+string_consts_size2, string_consts->array[n].start, string_consts->array[n].len);
        string_consts_size2 += string_consts->array[n].len + 1; // +1 for NULL
    }




    FILE* f = fopen(path, "wb");

    static const uint8_t dos_program[] = {
        0x8c, 0xc8, /* mov    %cs,%ax        */
        0x8e, 0xd8, /* mov    %ax,%ds        */
        0xba, 0x10, 0x00, /* mov    $16,%dx        */
        0xb4, 0x09, /* mov    $0x9,%ah       */
        0xcd, 0x21, /* int    $0x21          */
        0xb8, 0x01, 0x4c, /* mov    $0x4c01,%ax    */
        0xcd, 0x21, /* int    $0x21          */
        'H', 'e', 'l', 'l', 'o', ',', /* ($-terminated string) */
        ' ', 'D', 'O', 'S', ' ', 'w',
        'o', 'r', 'l', 'd', '!', '$'
    };

    uint32_t dos_stub_sz = DOS_HDR_SZ + sizeof(dos_program);
    uint32_t pe_offset = align_to(dos_stub_sz, 8);

    write8(f, 'M'); /* Magic number (2 bytes) */
    write8(f, 'Z');
    write16(f, dos_stub_sz % 512); /* Last page size */
    write16(f, align_to(dos_stub_sz, 512) / 512); /* Pages in file */
    write16(f, 0); /* Relocations */
    write16(f, DOS_HDR_SZ / 16); /* Size of header in paragraphs */
    write16(f, 0); /* Minimum extra paragraphs needed */
    write16(f, 1); /* Maximum extra paragraphs needed */
    write16(f, 0); /* Initial (relative) SS value */
    write16(f, 0); /* Initial SP value */
    write16(f, 0); /* Checksum */
    write16(f, 0); /* Initial IP value */
    write16(f, 0); /* Initial (relative) CS value */
    write16(f, DOS_HDR_SZ); /* File address of relocation table */
    write16(f, 0); /* Overlay number */
    write16(f, 0); /* Reserved0 */
    write16(f, 0); /* Reserved1 */
    write16(f, 0); /* Reserved2 */
    write16(f, 0); /* Reserved3 */
    write16(f, 0); /* OEM id */
    write16(f, 0); /* OEM info */
    for (int i = 0; i < 10; i++)
        write16(f, 0); /* Reserved (10 words) */
    write32(f, pe_offset); /* File offset of PE header. */


    /* DOS Program. */
    for (size_t i = 0; i < sizeof(dos_program); i++) {
        write8(f, dos_program[i]);
    }


    /* PE signature. */
    fseek(f, pe_offset, SEEK_SET);
    write8(f, 'P');
    write8(f, 'E');
    write8(f, 0);
    write8(f, 0);

    // uint32_t num_sections = 4;

    /* COFF header changed for 64-bit */
    write16(f, 0x8664); /* CHANGE: Machine: IMAGE_FILE_MACHINE_AMD64 (was 0x14c) */
    write16(f, num_sections); /* NumberOfSections */
    write32(f, 0); /* TimeDateStamp */
    write32(f, 0); /* PointerToSymbolTable */
    write32(f, 0); /* NumberOfSymbols */
    write16(f, 240); /* CHANGE: SizeOfOptionalHeader: PE32+ uses 240 bytes (was 224) */
    write16(f, 0x0022); /* CHANGE: Characteristics: Executable, Large Address Aware (was 0x103) */


    // uint32_t headers_sz = pe_offset + PE_HDR_SZ + num_sections * SEC_HDR_SZ;
    //
    // uint32_t idata_rva    = align_to(headers_sz, SEC_ALIGN);
    // uint32_t idata_offset = align_to(headers_sz, FILE_ALIGN);
    // // idata_sz computed after you know your imports
    //
    // uint32_t rdata_rva    = align_to(idata_rva + idata_sz, SEC_ALIGN);
    // uint32_t rdata_offset = align_to(idata_offset + idata_sz, FILE_ALIGN);
    // uint32_t rdata_sz     = string_consts_size;
    //
    // uint32_t text_rva    = align_to(rdata_rva + rdata_sz, SEC_ALIGN);
    // uint32_t text_offset = align_to(rdata_offset + rdata_sz, FILE_ALIGN);
    // uint32_t text_sz     = code_len;
    //
    //
    //
    // uint32_t iat_rva             = idata_rva;
    // uint32_t iat_sz              = (num_imports + 1) * IAT_ENTRY_SZ;
    // uint32_t import_dir_table_rva = iat_rva + iat_sz;
    // uint32_t import_dir_table_sz  = 2 * IMPORT_DIR_ENTRY_SZ;
    // uint32_t import_lookup_table_rva = import_dir_table_rva + import_dir_table_sz;
    // uint32_t name_table_rva      = import_lookup_table_rva + (num_imports + 1) * IMPORT_LOOKUP_TBL_ENTRY_SZ;
    // uint32_t dll_name_rva        = name_table_rva + name_table_sz;
    // uint32_t dll_name_sz         = 16; /* "KERNEL32.DLL\0" = 13 bytes, padded to 16 */
    // uint32_t idata_sz            = dll_name_rva + dll_name_sz - idata_rva;
    //
    // uint32_t bss_rva = align_to(idata_rva + idata_sz, SEC_ALIGN);
    // uint32_t bss_sz  = 0;// 4096;

    // uint64_t image_base = 0x140000000;
    //
    // uint32_t text_rva     = (uint32_t)(load_addr - image_base);
    // uint32_t rdata_rva    = (uint32_t)(str_consts_load_addr - image_base);
    // uint32_t idata_rva    = (uint32_t)(import_table_load_addr - image_base);
    //
    // uint32_t headers_sz = pe_offset + PE_HDR_SZ + num_sections * SEC_HDR_SZ;
    //
    // // uint32_t idata_rva    = import_table_load_addr; // align_to(headers_sz, SEC_ALIGN);
    // uint32_t idata_offset = align_to(headers_sz, FILE_ALIGN);
    //
    // // compute idata layout first
    // uint32_t iat_rva                 = idata_rva;
    // uint32_t iat_sz                  = (num_imports + 1) * IAT_ENTRY_SZ;
    // uint32_t import_dir_table_rva    = iat_rva + iat_sz;
    // uint32_t import_dir_table_sz     = 2 * IMPORT_DIR_ENTRY_SZ;
    // uint32_t import_lookup_table_rva = import_dir_table_rva + import_dir_table_sz;
    // uint32_t name_table_rva          = import_lookup_table_rva + (num_imports + 1) * IMPORT_LOOKUP_TBL_ENTRY_SZ;
    // uint32_t dll_name_sz             = 16;
    // uint32_t dll_name_rva            = name_table_rva + name_table_sz;
    // uint32_t idata_sz                = dll_name_rva + dll_name_sz - idata_rva;
    //
    // // now rdata and text can follow
    // // uint32_t rdata_rva    = str_consts_load_addr; //align_to(idata_rva + idata_sz, SEC_ALIGN);
    // uint32_t rdata_offset = align_to(idata_offset + idata_sz, FILE_ALIGN);
    // uint32_t rdata_sz     = string_consts_size;
    //
    // // uint32_t text_rva    =  load_addr; // align_to(rdata_rva + rdata_sz, SEC_ALIGN);
    // uint32_t text_offset = align_to(rdata_offset + rdata_sz, FILE_ALIGN);
    // uint32_t text_sz     = code_len;
    //
    // uint32_t bss_rva = align_to(text_rva + text_sz, SEC_ALIGN);
    // uint32_t bss_sz  = 0;

    uint32_t import_dir_table_sz = 2 * IMPORT_DIR_ENTRY_SZ;

    // 1. Calculate the size of your headers
    uint32_t headers_sz = pe_offset + PE_HDR_SZ + num_sections * SEC_HDR_SZ;

    // 2. Pre-calculate exact internal .idata sizes
    uint32_t iat_sz       = (num_imports + 1) * IAT_ENTRY_SZ;
    uint32_t dir_sz       = 2 * IMPORT_DIR_ENTRY_SZ;
    uint32_t ilt_sz       = (num_imports + 1) * IMPORT_LOOKUP_TBL_ENTRY_SZ;
    uint32_t dll_name_sz  = 16;
    uint32_t idata_sz     = iat_sz + dir_sz + ilt_sz + name_table_sz + dll_name_sz;

    // 3. Map RVAs (Virtual Memory) - .idata comes first!
    uint32_t idata_rva = align_to(headers_sz, SEC_ALIGN);

    // NOW you know your exact IAT addresses!
    // Absolute address of first import = image_base + idata_rva
    // Absolute address of second import = image_base + idata_rva + 8

    uint32_t text_rva  = align_to(idata_rva + idata_sz, SEC_ALIGN);
    uint32_t text_sz   = code_len;

    uint32_t rdata_rva = align_to(text_rva + text_sz, SEC_ALIGN);
    uint32_t rdata_sz  = string_consts_size;

    // uint32_t image_sz  = align_to(rdata_rva + rdata_sz, SEC_ALIGN);

    // 4. Map File Offsets (Physical Disk)
    uint32_t idata_offset = align_to(headers_sz, FILE_ALIGN);
    uint32_t text_offset  = align_to(idata_offset + idata_sz, FILE_ALIGN);
    uint32_t rdata_offset = align_to(text_offset + text_sz, FILE_ALIGN);

    // 5. Compute internal .idata layout using the computed idata_rva
    uint32_t iat_rva                 = idata_rva;
    uint32_t import_dir_table_rva    = iat_rva + iat_sz;
    uint32_t import_lookup_table_rva = import_dir_table_rva + dir_sz;
    uint32_t name_table_rva          = import_lookup_table_rva + ilt_sz;
    uint32_t dll_name_rva            = name_table_rva + name_table_sz;


    uint32_t bss_rva = align_to(rdata_rva + rdata_sz, SEC_ALIGN);
    uint32_t bss_sz  = 0;

    uint32_t image_sz = align_to(bss_rva + bss_sz, SEC_ALIGN);


    /* Optional header, part 1: standard fields */
    write16(f, 0x20b); /* Magic: PE32+ */
    write8(f, 0); /* MajorLinkerVersion */
    write8(f, 0); /* MinorLinkerVersion */
    write32(f, text_sz); /* SizeOfCode */
    write32(f, rdata_sz + idata_sz); /* SizeOfInitializedData */
    write32(f, bss_sz); /* SizeOfUninitializedData */
    write32(f, text_rva); /* AddressOfEntryPoint */
    write32(f, text_rva); /* BaseOfCode */

    /* Optional header, part 2: Windows-specific fields */
    write64(f, load_addr);
    write32(f, SEC_ALIGN); /* SectionAlignment */
    write32(f, FILE_ALIGN); /* FileAlignment */
    write16(f, 6); /* MajorOperatingSystemVersion */ // 6 -> Vista, safe for anything newer
    write16(f, 0); /* MinorOperatingSystemVersion*/
    write16(f, 0); /* MajorImageVersion */
    write16(f, 0); /* MinorImageVersion */
    write16(f, 6); /* MajorSubsystemVersion */ // 6 -> Vista, safe for anything newer
    write16(f, 0); /* MinorSubsystemVersion */
    write32(f, 0); /* Win32VersionValue*/
    write32(f, image_sz); /* SizeOfImage */
    write32(f, align_to(headers_sz, FILE_ALIGN)); /* SizeOfHeaders */
    write32(f, 0); /* Checksum */
    write16(f, 3); /* Subsystem: Console */
    write16(f, 0x0100); /* DllCharacteristics */
    write64(f, 0x100000); /* SizeOfStackReserve */
    write64(f, 0x1000);   /* SizeOfStackCommit */
    write64(f, 0x100000); /* SizeOfHeapReserve */
    write64(f, 0x1000);   /* SizeOfHeapCommit */
    write32(f, 0); /* LoadFlags */
    write32(f, 16); /* NumberOfRvaAndSizes */

    /* Optional header, part 3: data directories. */
    write32(f, 0);
    write32(f, 0); /* Export Table. */
    write32(f, import_dir_table_rva); /* Import Table. */
    write32(f, import_dir_table_sz);
    write32(f, 0);
    write32(f, 0); /* Resource Table. */
    write32(f, 0);
    write32(f, 0); /* Exception Table. */
    write32(f, 0);
    write32(f, 0); /* Certificate Table. */
    write32(f, 0);
    write32(f, 0); /* Base Relocation Table. */
    write32(f, 0);
    write32(f, 0); /* Debug. */
    write32(f, 0);
    write32(f, 0); /* Architecture. */
    write32(f, 0);
    write32(f, 0); /* Global Ptr. */
    write32(f, 0);
    write32(f, 0); /* TLS Table. */
    write32(f, 0);
    write32(f, 0); /* Load Config Table. */
    write32(f, 0);
    write32(f, 0); /* Bound Import. */
    write32(f, iat_rva); /* Import Address Table. */
    write32(f, iat_sz);
    write32(f, 0);
    write32(f, 0); /* Delay Import Descriptor. */
    write32(f, 0);
    write32(f, 0); /* CLR Runtime Header. */
    write32(f, 0);
    write32(f, 0); /* (Reserved). */






    // /* --- Section headers (PE IMAGE_SECTION_HEADER, 40 bytes each) --- */
    //
    // write_name8(f, ".text");
    // write32(f, text_sz);
    // write32(f, text_rva);
    // write32(f, align_to(text_sz, FILE_ALIGN));
    // write32(f, text_offset);
    // write32(f, 0); write32(f, 0);
    // write16(f, 0); write16(f, 0);
    // write32(f, 0x60000020); /* CODE | EXECUTE | READ */
    //
    // write_name8(f, ".rdata");
    // write32(f, rdata_sz);
    // write32(f, rdata_rva);
    // write32(f, align_to(rdata_sz, FILE_ALIGN));
    // write32(f, rdata_offset);
    // write32(f, 0); write32(f, 0);
    // write16(f, 0); write16(f, 0);
    // write32(f, 0x40000040); /* INITIALIZED_DATA | READ */
    //
    // write_name8(f, ".idata");
    // write32(f, idata_sz);
    // write32(f, idata_rva);
    // write32(f, align_to(idata_sz, FILE_ALIGN));
    // write32(f, idata_offset);
    // write32(f, 0); write32(f, 0);
    // write16(f, 0); write16(f, 0);
    // write32(f, 0xC0000040); /* INITIALIZED_DATA | READ | WRITE */
    //
    // write_name8(f, ".bss");
    // write32(f, bss_sz);
    // write32(f, bss_rva);
    // write32(f, 0);          /* SizeOfRawData = 0 for BSS */
    // write32(f, 0);          /* PointerToRawData = 0 */
    // write32(f, 0); write32(f, 0);
    // write16(f, 0); write16(f, 0);
    // write32(f, 0xC0000080); /* UNINITIALIZED_DATA | READ | WRITE */

    /* --- Section headers (PE IMAGE_SECTION_HEADER, 40 bytes each) --- */

    // 1. .idata is now first
    write_name8(f, ".idata");
    write32(f, idata_sz);
    write32(f, idata_rva);
    write32(f, align_to(idata_sz, FILE_ALIGN));
    write32(f, idata_offset);
    write32(f, 0); write32(f, 0);
    write16(f, 0); write16(f, 0);
    write32(f, 0xC0000040); /* INITIALIZED_DATA | READ | WRITE */

    // 2. .text comes second
    write_name8(f, ".text");
    write32(f, text_sz);
    write32(f, text_rva);
    write32(f, align_to(text_sz, FILE_ALIGN));
    write32(f, text_offset);
    write32(f, 0); write32(f, 0);
    write16(f, 0); write16(f, 0);
    write32(f, 0x60000020); /* CODE | EXECUTE | READ */

    // 3. .rdata comes third
    write_name8(f, ".rdata");
    write32(f, rdata_sz);
    write32(f, rdata_rva);
    write32(f, align_to(rdata_sz, FILE_ALIGN));
    write32(f, rdata_offset);
    write32(f, 0); write32(f, 0);
    write16(f, 0); write16(f, 0);
    write32(f, 0x40000040); /* INITIALIZED_DATA | READ */


    // write_name8(f, ".bss");
    // write32(f, bss_sz);
    // write32(f, bss_rva);
    // write32(f, 0);          /* SizeOfRawData = 0 for BSS */
    // write32(f, 0);          /* PointerToRawData = 0 */
    // write32(f, 0); write32(f, 0);
    // write16(f, 0); write16(f, 0);
    // write32(f, 0xC0000080); /* UNINITIALIZED_DATA | READ | WRITE */





    /* Precompute RVA of each hint/name entry within the name table */
    uint32_t* hint_name_rvas = malloc(num_imports * sizeof(uint32_t));
    uint32_t nt_offset = 0;
    for (uint32_t i = 0; i < num_imports; i++) {
        hint_name_rvas[i] = name_table_rva + nt_offset;
        size_t esz = 2 + importing_funcs.array[i].len + 1;
        esz = (esz + 1) & ~1;
        nt_offset += (uint32_t)esz;
    }

    // printf("idata_offset = 0x%x\n", idata_offset);
    /* --- .idata --- */
    fseek(f, idata_offset, SEEK_SET);

    /* IAT — patched by loader with real VAs */
    for (uint32_t i = 0; i < num_imports; i++)
        write64(f, (uint64_t)hint_name_rvas[i]);
    write64(f, 0);

    /* Import Directory Table: one real entry + null sentinel */
    write32(f, import_lookup_table_rva); /* OriginalFirstThunk */
    write32(f, 0);                        /* TimeDateStamp */
    write32(f, 0);                        /* ForwarderChain */
    write32(f, dll_name_rva);
    write32(f, iat_rva);                  /* FirstThunk */
    /* null entry */
    for (int i = 0; i < 5; i++) write32(f, 0);

    /* ILT — same layout as IAT, not patched */
    for (uint32_t i = 0; i < num_imports; i++)
        write64(f, (uint64_t)hint_name_rvas[i]);
    write64(f, 0);

    /* Hint/Name Table */
    for (uint32_t i = 0; i < num_imports; i++) {
        write16(f, 0); /* hint — 0 is safe, loader ignores it if name matches */
        fwrite(importing_funcs.array[i].start, 1, importing_funcs.array[i].len, f);
        write8(f, 0);
        size_t written = 2 + importing_funcs.array[i].len + 1;
        if (written & 1) write8(f, 0); /* pad to even */
    }

    /* DLL name */
    fwrite("KERNEL32.DLL\0\0\0", 1, dll_name_sz, f);


    // printf("text_offset = 0x%x\n", text_offset);
    /* --- .text --- */
    fseek(f, text_offset, SEEK_SET);
    fwrite(code, 1, code_len, f);

    // printf("rdata_offset = 0x%x\n", rdata_offset);
    /* --- .rdata --- */
    fseek(f, rdata_offset, SEEK_SET);
    fwrite(string_consts_array, 1, string_consts_size, f);

    fseek(f, rdata_offset+0x512, SEEK_SET);
    write8(f, 0);

    free(hint_name_rvas);
    free(string_consts_array);
    SVL_p_free(&importing_funcs);


    fflush(f);

    fclose(f);
}
