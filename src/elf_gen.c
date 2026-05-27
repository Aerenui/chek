//
// Created by frantisek on 26. 5. 2026.
//

#include "elf_gen.h"


#include <elf.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "compiler.h"

// load address for the executable segment
// #define LOAD_ADDR 0x400000

// congruence rule must hold — p_offset % p_align == p_vaddr % p_align
#define PAGE_SIZE 0x1000
#define CODE_OFFSET PAGE_SIZE  // pad headers into a full page


void write_elf64(const char *path, const uint8_t *code, size_t code_len, uint64_t load_addr, FunctionsRegistry* fr) {
    /*// --- prologue/epilogue ---
    // push rbp; mov rbp, rsp; sub rsp, N  (N = frame.next_offset rounded to 16)
    uint8_t prologue[] = {
        0x55,                               // push rbp
        0x48, 0x89, 0xE5,                   // mov rbp, rsp

        0x48, 0x83, 0xEC, (uint8_t)((get_frame()->next_offset + 15) & ~15), // sub rsp, N
    };
    // mov eax, 0 (exit code); mov rdi, rax; mov rax, 60 (sys_exit); syscall
    uint8_t epilogue[] = {
        0x48, 0x83, 0xC4, (uint8_t)((get_frame()->next_offset + 15) & ~15), // add rsp, N
        0x5D,                                       // pop rbp

        0xB8, 0x00, 0x00, 0x00, 0x00,               // mov eax, 0
        0x48, 0x89, 0xC7,                           // mov rdi, rax  (exit code = 0)
        0x48, 0xC7, 0xC0, 0x3C, 0x00, 0x00, 0x00,   // mov rax, 60 (sys_exit)
        0x0F, 0x05                                  // syscall
    };*/

    // size_t total_code = sizeof(prologue) + code_len + sizeof(epilogue);
    size_t total_code = code_len;
    // uint64_t entry = LOAD_ADDR + CODE_OFFSET;
    // uint64_t entry = LOAD_ADDR;
    uint64_t entry = load_addr;

    char shstrtab[] = "\0.text\0.strtab\0.symtab\0.shstrtab";
    Elf64_Shdr shdrs[5] = {0};

    // [0] null
    shdrs[0] = (Elf64_Shdr){0};

    // [1] .text
    shdrs[1] = (Elf64_Shdr){
        .sh_name      = 1, // offset of ".text" in shstrtab
        .sh_type      = SHT_PROGBITS,
        .sh_flags     = SHF_ALLOC | SHF_EXECINSTR,
        .sh_addr      = load_addr,
        .sh_offset    = CODE_OFFSET,
        .sh_size      = total_code,
        .sh_addralign = 0x1000,
    };

    uint64_t strtab_file_offset = (CODE_OFFSET + total_code);
    strtab_file_offset += strtab_file_offset % PAGE_SIZE; // to full page
    uint64_t strtab_len = 1; // 1 for first byte being NULL

    for (size_t n=0; n < fr->len; n++) {
        strtab_len += fr->array[n].name.len + 1; // +1 for NULL
    }

    uint8_t* strtab = calloc(1, strtab_len);
    Elf64_Sym* symtab = malloc(sizeof(Elf64_Sym) * (fr->len+1));
    symtab[0] = (Elf64_Sym){0};

    size_t strtab_cur = 1;
    for (size_t n=0; n < fr->len; n++) {
        memcpy(strtab + strtab_cur, fr->array[n].name.start, fr->array[n].name.len);
        symtab[n + 1] = (Elf64_Sym) {
            .st_name = strtab_cur,
            .st_info  = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC),
            .st_other = STV_DEFAULT,
            .st_shndx = 1, // index of your text section
            .st_value = load_addr + fr->array[n].offset,
            .st_size  = fr->array[n].code_size,
        };

        strtab_cur += fr->array[n].name.len + 1; // +1 for NULL;
    }

    // [2] .strtab
    shdrs[2] = (Elf64_Shdr){
        .sh_name      = 7, // offset of ".strtab" in shstrtab
        .sh_type      = SHT_STRTAB,
        .sh_offset    = strtab_file_offset,
        .sh_size      = strtab_len,
    };

    uint64_t symtab_file_offset = (1 + strtab_file_offset);
    symtab_file_offset += symtab_file_offset % PAGE_SIZE; // to full page
    uint64_t symtab_len = fr->len+1;
    uint64_t symtab_size = symtab_len * sizeof(Elf64_Sym);

    // [3] .symtab
    shdrs[3] = (Elf64_Shdr){
        .sh_name      = 15, // offset of ".symtab" in shstrtab
        .sh_type      = SHT_SYMTAB,
        .sh_offset    = symtab_file_offset,
        .sh_size      = symtab_size,
        .sh_link      = 2, // index of associated .strtab
        .sh_info      = 1, // index of first global symbol
        .sh_entsize   = sizeof(Elf64_Sym),
    };


    uint64_t shstrtab_file_offset = (1 + symtab_file_offset);
    shstrtab_file_offset += shstrtab_file_offset % PAGE_SIZE; // to full page

    // [4] .shstrtab
    shdrs[4] = (Elf64_Shdr){
        .sh_name      = 23, // offset of ".shstrtab" in shstrtab
        .sh_type      = SHT_STRTAB,
        .sh_offset    = shstrtab_file_offset,
        .sh_size      = sizeof(shstrtab),
    };

    uint64_t section_headers_file_offset = 1 + shstrtab_file_offset;
    // section_headers_file_offset += section_headers_file_offset % PAGE_SIZE; // to full page

    Elf64_Ehdr ehdr = {
        .e_ident = {
            ELFMAG0, ELFMAG1, ELFMAG2, ELFMAG3,
            ELFCLASS64, ELFDATA2LSB, EV_CURRENT, ELFOSABI_NONE,
            0, 0, 0, 0, 0, 0, 0, 0
        },
        .e_type      = ET_EXEC,
        .e_machine   = EM_X86_64,
        .e_version   = EV_CURRENT,
        .e_entry     = entry,
        .e_phoff     = sizeof(Elf64_Ehdr),
        .e_shoff     = section_headers_file_offset,
        .e_flags     = 0,
        .e_ehsize    = sizeof(Elf64_Ehdr),
        .e_phentsize = sizeof(Elf64_Phdr),
        .e_phnum     = 1,
        .e_shentsize = sizeof(Elf64_Shdr),
        .e_shnum     = 5,
        .e_shstrndx  = 4, // SHN_UNDEF,
    };

    Elf64_Phdr phdr = {
        .p_type   = PT_LOAD,
        .p_flags  = PF_R | PF_X,
        .p_offset = CODE_OFFSET,
        .p_vaddr  = load_addr, //LOAD_ADDR,
        .p_paddr  = load_addr, //LOAD_ADDR,
        .p_filesz = total_code,
        .p_memsz  = total_code,
        .p_align = 0x1000,
    };

    uint8_t pad[PAGE_SIZE - sizeof(Elf64_Ehdr) - sizeof(Elf64_Phdr)] = {0};

    FILE *f = fopen(path, "wb");
    fwrite(&ehdr,     1, sizeof(ehdr),     f);
    fwrite(&phdr,     1, sizeof(phdr),     f);
    fwrite(pad,       1, sizeof(pad),      f);
    fwrite(code,      1, code_len,         f);

    uint64_t full_pad_size = (section_headers_file_offset + sizeof(Elf64_Ehdr)) - (CODE_OFFSET + total_code);
    uint8_t* full_pad = calloc(1, full_pad_size);
    fwrite(full_pad, 1, full_pad_size, f);
    free(full_pad);

    // [ ELF header ][ program headers ][ .text ][ .strtab ][ .symtab ][ .shstrtab ][ section headers ]

    fseek(f, strtab_file_offset, SEEK_SET);;
    fwrite(strtab, 1, strtab_len, f);
    free(strtab);

    fseek(f, symtab_file_offset, SEEK_SET);;
    fwrite(symtab, 1, symtab_size, f);
    free(symtab);

    fseek(f, shstrtab_file_offset, SEEK_SET);;
    fwrite(shstrtab, 1, sizeof(shstrtab), f);

    fseek(f, section_headers_file_offset, SEEK_SET);;
    fwrite(shdrs, 1, sizeof(shdrs), f);


    fclose(f);

    chmod(path, 0755);

    // fprintf(stderr, "shstrtab_file_offset: 0x%lx\n", shstrtab_file_offset);
    // fprintf(stderr, "symtab_file_offset:   0x%lx\n", symtab_file_offset);
    // fprintf(stderr, "symtab_size:          0x%lx\n", symtab_size);
    // fprintf(stderr, "shdr_offset:          0x%lx\n", section_headers_file_offset);
    // fprintf(stderr, "sizeof(shstrtab):     0x%lx\n", sizeof(shstrtab));
}