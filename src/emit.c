//
// Created by frantisek on 25. 5. 2026.
//

#include "emit.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "compiler.h"
#include "expr.h"
#include "utils.h"

LocStack LS_new(void) {
    return (LocStack){
        .array = calloc(LocStack_size, sizeof(Loc)),
        .len = 0,
        .cap = LocStack_size
    };
}

void LS_p_overflow_guard(LocStack* ls) {
    const size_t original_cap = ls->cap;
    while (ls->len + 1 >= ls->cap) ls->cap *= 2;
    if (original_cap == ls->cap) return;
    Loc* new_array = realloc(ls->array, (sizeof(Loc)) * ls->cap);
    assert(new_array != NULL);
    ls->array = new_array;
}

void LS_push(LocStack* ls, Loc l) {
    LS_p_overflow_guard(ls);
    assert(ls->array != NULL);
    ls->array[ls->len++] = l;
}

inline bool LS_is_empty(LocStack* ls) {
    return ls->len < 1;
}

Loc LS_pop(LocStack* ls) {
    assert(ls->array != NULL);
    if (LS_is_empty(ls)) {
        fprintf(stderr, "[ERROR] pop on empty LS\n");
        exit(1);
    }
    Loc ot = ls->array[ls->len - 1];
    ls->len--;
    return ot;
}

Loc LS_peek(LocStack* ls) {
    assert(ls->array != NULL);
    if (LS_is_empty(ls)) {
        fprintf(stderr, "[ERROR] peek on empty LS\n");
        exit(1);
    }
    return ls->array[ls->len - 1];
}

void LS_free(LocStack* ls) {
    free(ls->array);
}

// -----------------------------------------------------------------------------------------

inline RelocationList RL_new(void) {
    Relocation* array = malloc(sizeof(Relocation) * RelocationList_default_cap);
    assert(array != NULL);
    return (RelocationList){
        .array = array,
        .len = 0,
        .cap = RelocationList_default_cap
    };
}

void RL_free(RelocationList* rl) {
    free(rl->array);
}

void RL_push_guard(RelocationList* rl) {
    size_t org_cap = rl->cap;
    while (rl->len + 1 > rl->cap) rl->cap *= 2;
    if (org_cap != rl->cap) {
        Relocation* new_array = realloc(rl->array, rl->cap * sizeof(Relocation));
        assert(new_array != NULL);
        rl->array = new_array;
    }
}

void RL_push(RelocationList* rl, const Relocation r) {
    RL_push_guard(rl);
    rl->array[rl->len++] = r;
}

// -----------------------------------------------------------------------------------------

inline LabelList LL_new(void) {
    Label* array = malloc(sizeof(Label) * LabelList_default_cap);
    assert(array != NULL);
    return (LabelList){
        .array = array,
        .cap = LabelList_default_cap,
        .len = 0
    };
}

inline void LL_free(LabelList* ll) {
    free(ll->array);
}

void LL_push_guard(LabelList* ll) {
    size_t org_cap = ll->cap;
    while (ll->len + 1 > ll->cap) ll->cap *= 2;
    if (org_cap != ll->cap) {
        Label* new_array = realloc(ll->array, ll->cap * sizeof(Label));
        assert(new_array != NULL);
        ll->array = new_array;
    }
}

void LL_push(LabelList* ll, Label l) {
    LL_push_guard(ll);
    ll->array[ll->len++] = l;
}

// -----------------------------------------------------------------------------------------

// void define_label(ByteSeg* buf, LabelList* labels, StringView name) {
//     LL_push(labels, (Label){
//                 .label = name,
//                 .offset = BS_get_cursor(buf),
//             });
// }

void resolve_relocations(ByteSeg *buf, RelocationList *rels, LabelList *labels) {
    for (size_t i = 0; i < rels->len; i++) {
        Relocation *r = &rels->array[i];
        Label *target = NULL;
        for (size_t j = 0; j < labels->len; j++) {
            if (labels->array[j].id == r->id && labels->array[j].kind == r->kind) {
                target = &labels->array[j];
                break;
            }
        }
        if (target == NULL) {
            fprintf(stderr, "[ERROR] unresolved relocation id=%zu kind=%d\n", r->id, r->kind);
            exit(1);
        }
        int32_t offset = (int32_t)((int64_t)target->offset - (int64_t)r->inst_end);
        memcpy(buf->array + r->patch_pos, &offset, 4);
    }
}

// -----------------------------------------------------------------------------------------

void emit_mov_eax(ByteSeg* out, Loc loc, GlobalPatchList* gpl, CompilerTarget target) {
    switch (loc.kind) {
        case LOC_IMMEDIATE:
            BS_write(out, 0xB8);
            BS_write(out, (loc.value >>  0) & 0xFF);
            BS_write(out, (loc.value >>  8) & 0xFF);
            BS_write(out, (loc.value >> 16) & 0xFF);
            BS_write(out, (loc.value >> 24) & 0xFF);
            break;
        case LOC_VAR:
        case LOC_STACK_SLOT: {
            int slot = (loc.kind == LOC_VAR) ? lookup_var(loc.var) : loc.offset;
            BS_write(out, 0x8B);
            BS_write(out, 0x45);
            BS_write(out, (uint8_t)(-slot));
            break;
        }
        case LOC_GLOBAL: {
            switch (target) {
                case F_Elf64: {
                    size_t pos = BS_get_cursor(out) + 3;
                    BS_write(out, 0x8B);        // MOV eax, [addr32]
                    BS_write(out, 0x04);
                    BS_write(out, 0x25);
                    BS_write(out, 0x00); BS_write(out, 0x00);
                    BS_write(out, 0x00); BS_write(out, 0x00);
                    GPL_register_patch(gpl, (GlobalPatch){
                        .index = loc.offset,
                        .offset = pos,
                        .bit_size = 4,
                    });
                    break;
                }
                case F_Win64: {
                    size_t pos = BS_get_cursor(out) + 2;
                    BS_write(out, 0x48); BS_write(out, 0xB9);   // MOV rcx, imm64
                    BS_write(out, 0x00); BS_write(out, 0x00);
                    BS_write(out, 0x00); BS_write(out, 0x00);
                    BS_write(out, 0x00); BS_write(out, 0x00);
                    BS_write(out, 0x00); BS_write(out, 0x00);
                    BS_write(out, 0x8B); BS_write(out, 0x01);   // MOV eax, [rcx]
                    GPL_register_patch(gpl, (GlobalPatch){
                        .index = loc.offset,
                        .offset = pos,
                        .bit_size = 8,
                    });
                    break;
                }
            }
            break;
        }
    }
}

void emit_op_eax(ByteSeg* out, uint8_t opcode, Loc src, GlobalPatchList* gpl, CompilerTarget target) {
    switch (src.kind) {
        case LOC_IMMEDIATE:
            BS_write(out, 0xB9);
            BS_write(out, (src.value >>  0) & 0xFF);
            BS_write(out, (src.value >>  8) & 0xFF);
            BS_write(out, (src.value >> 16) & 0xFF);
            BS_write(out, (src.value >> 24) & 0xFF);
            BS_write(out, opcode);
            BS_write(out, 0xC1);
            break;
        case LOC_VAR:
        case LOC_STACK_SLOT: {
            int slot = (src.kind == LOC_VAR) ? lookup_var(src.var) : src.offset;
            BS_write(out, opcode);
            BS_write(out, 0x45);
            BS_write(out, (uint8_t)(-slot));
            break;
        }
        case LOC_GLOBAL: {
            switch (target) {
                case F_Elf64: {
                    size_t pos = BS_get_cursor(out) + 3;
                    BS_write(out, 0x8B);        // MOV ecx, [addr32]
                    BS_write(out, 0x0C);
                    BS_write(out, 0x25);
                    BS_write(out, 0x00); BS_write(out, 0x00);
                    BS_write(out, 0x00); BS_write(out, 0x00);
                    GPL_register_patch(gpl, (GlobalPatch){
                        .index = src.offset,
                        .offset = pos,
                        .bit_size = 4,
                    });
                    BS_write(out, opcode);
                    BS_write(out, 0xC1);        // OP eax, ecx
                    break;
                }
                case F_Win64: {
                    size_t pos = BS_get_cursor(out) + 2;
                    BS_write(out, 0x48); BS_write(out, 0xB9);   // MOV rcx, imm64
                    BS_write(out, 0x00); BS_write(out, 0x00);
                    BS_write(out, 0x00); BS_write(out, 0x00);
                    BS_write(out, 0x00); BS_write(out, 0x00);
                    BS_write(out, 0x00); BS_write(out, 0x00);
                    BS_write(out, 0x8B); BS_write(out, 0x09);   // MOV ecx, [rcx]
                    GPL_register_patch(gpl, (GlobalPatch){
                        .index = src.offset,
                        .offset = pos,
                        .bit_size = 8,
                    });
                    BS_write(out, opcode);
                    BS_write(out, 0xC1);        // OP eax, ecx
                    break;
                }
            }
            break;
        }
    }
}

void emit_imul_eax(ByteSeg* out, Loc src, GlobalPatchList* gpl, CompilerTarget target) {
    switch (src.kind) {
        case LOC_IMMEDIATE:
            BS_write(out, 0x69);
            BS_write(out, 0xC0);
            BS_write(out, (src.value >>  0) & 0xFF);
            BS_write(out, (src.value >>  8) & 0xFF);
            BS_write(out, (src.value >> 16) & 0xFF);
            BS_write(out, (src.value >> 24) & 0xFF);
            break;
        case LOC_VAR:
        case LOC_STACK_SLOT: {
            int slot = (src.kind == LOC_VAR) ? lookup_var(src.var) : src.offset;
            BS_write(out, 0x0F);
            BS_write(out, 0xAF);
            BS_write(out, 0x45);
            BS_write(out, (uint8_t)(-slot));
            break;
        }
        case LOC_GLOBAL: {
            switch (target) {
                case F_Elf64: {
                    size_t pos = BS_get_cursor(out) + 3;
                    BS_write(out, 0x8B);        // MOV ecx, [addr32]
                    BS_write(out, 0x0C);
                    BS_write(out, 0x25);
                    BS_write(out, 0x00); BS_write(out, 0x00);
                    BS_write(out, 0x00); BS_write(out, 0x00);
                    GPL_register_patch(gpl, (GlobalPatch){
                        .index = src.offset,
                        .offset = pos,
                        .bit_size = 4,
                    });
                    BS_write(out, 0x0F);
                    BS_write(out, 0xAF);        // IMUL eax, ecx
                    BS_write(out, 0xC1);
                    break;
                }
                case F_Win64: {
                    size_t pos = BS_get_cursor(out) + 2;
                    BS_write(out, 0x48); BS_write(out, 0xB9);   // MOV rcx, imm64
                    BS_write(out, 0x00); BS_write(out, 0x00);
                    BS_write(out, 0x00); BS_write(out, 0x00);
                    BS_write(out, 0x00); BS_write(out, 0x00);
                    BS_write(out, 0x00); BS_write(out, 0x00);
                    BS_write(out, 0x8B); BS_write(out, 0x09);   // MOV ecx, [rcx]
                    GPL_register_patch(gpl, (GlobalPatch){
                        .index = src.offset,
                        .offset = pos,
                        .bit_size = 8,
                    });
                    BS_write(out, 0x0F);
                    BS_write(out, 0xAF);        // IMUL eax, ecx
                    BS_write(out, 0xC1);
                    break;
                }
            }
            break;
        }
    }
}

// void emit_mov_eax(ByteSeg* out, Loc loc) {
//     if (loc.kind == LOC_IMMEDIATE) {
//         // MOV eax, imm32  (B8 id)
//         BS_write(out, 0xB8);
//         BS_write(out, (loc.value >> 0) & 0xFF);
//         BS_write(out, (loc.value >> 8) & 0xFF);
//         BS_write(out, (loc.value >> 16) & 0xFF);
//         BS_write(out, (loc.value >> 24) & 0xFF);
//     } else {
//         // LOC_VAR + LOC_STACK_SLOT
//         // MOV eax, [rbp - offset]
//         int slot = (loc.kind == LOC_VAR) ? lookup_var(loc.var) : loc.offset;
//         BS_write(out, 0x8B);
//         BS_write(out, 0x45);
//         BS_write(out, (uint8_t) (-slot));
//     }
// }
//
// void emit_op_eax(ByteSeg* out, uint8_t opcode, Loc src) {
//     if (src.kind == LOC_IMMEDIATE) {
//         // 81 /0 id  (ADD eax, imm32)  — /0 for ADD, /5 for SUB
//         // but simpler: MOV ecx, imm32 then OP eax, ecx
//         BS_write(out, 0xB9); // MOV ecx, imm32
//         BS_write(out, (src.value >> 0) & 0xFF);
//         BS_write(out, (src.value >> 8) & 0xFF);
//         BS_write(out, (src.value >> 16) & 0xFF);
//         BS_write(out, (src.value >> 24) & 0xFF);
//         BS_write(out, opcode); // ADD/SUB eax, ecx
//         BS_write(out, 0xC1); // ModRM: mod=11, reg=eax(0), rm=ecx(1)
//     } else {
//         // src is in memory [rbp - offset]
//         int slot = (src.kind == LOC_VAR) ? lookup_var(src.var) : src.offset;
//         BS_write(out, opcode); // ADD/SUB eax, [rbp - slot]
//         BS_write(out, 0x45); // ModRM: mod=01, reg=eax(0), rm=rbp(5)
//         BS_write(out, (uint8_t) (-slot));
//     }
// }
//
// void emit_imul_eax(ByteSeg* out, Loc src) {
//     if (src.kind == LOC_IMMEDIATE) {
//         // IMUL eax, eax, imm32  (6B /r ib for imm8, 69 /r id for imm32)
//         BS_write(out, 0x69); // IMUL r32, r/m32, imm32
//         BS_write(out, 0xC0); // ModRM: mod=11, reg=eax(0), rm=eax(0)
//         BS_write(out, (src.value >> 0) & 0xFF);
//         BS_write(out, (src.value >> 8) & 0xFF);
//         BS_write(out, (src.value >> 16) & 0xFF);
//         BS_write(out, (src.value >> 24) & 0xFF);
//     } else {
//         // IMUL eax, [rbp - slot]  (0F AF /r)
//         int slot = (src.kind == LOC_VAR) ? lookup_var(src.var) : src.offset;
//         BS_write(out, 0x0F);
//         BS_write(out, 0xAF); // IMUL r32, r/m32
//         BS_write(out, 0x45); // ModRM: mod=01, reg=eax(0), rm=rbp(5)
//         BS_write(out, (uint8_t) (-slot));
//     }
// }

// void emit_jmp_label(ByteSeg* buf, RelocationList* rels, StringView label) {
//     BS_write(buf, 0xE9); // JMP rel32
//     size_t patch_pos = BS_get_cursor(buf);
//     BS_write_array(buf, 4, (uint8_t[]){0, 0, 0, 0});
//     RL_push(rels, (Relocation){
//                 .patch_pos = patch_pos,
//                 .patch_size = 4,
//                 .label = label,
//                 .inst_end = BS_get_cursor(buf),
//             });
// }
//
// void emit_je_label(ByteSeg* buf, RelocationList* rels, StringView label) {
//     BS_write_array(buf, 2, (uint8_t[]){0x0F, 0x84}); // JE rel32
//     size_t patch_pos = BS_get_cursor(buf);
//     BS_write_array(buf, 4, (uint8_t[]){0, 0, 0, 0});
//     RL_push(rels, (Relocation){
//                 .patch_pos = patch_pos,
//                 .patch_size = 4,
//                 .label = label,
//                 .inst_end = BS_get_cursor(buf),
//             });
// }


void emit_jmp(ByteSeg *buf, RelocationList *rels, size_t id, RelKind kind) {
    BS_write(buf, 0xE9);
    size_t patch_pos = BS_get_cursor(buf);
    BS_write_array(buf, 4, (uint8_t[]){0, 0, 0, 0});
    RL_push(rels, (Relocation){
        .patch_pos  = patch_pos,
        .patch_size = 4,
        .inst_end   = BS_get_cursor(buf),
        .id         = id,
        .kind       = kind,
    });
}

void emit_je(ByteSeg *buf, RelocationList *rels, size_t id, RelKind kind) {
    BS_write_array(buf, 2, (uint8_t[]){0x0F, 0x84});
    size_t patch_pos = BS_get_cursor(buf);
    BS_write_array(buf, 4, (uint8_t[]){0, 0, 0, 0});
    RL_push(rels, (Relocation){
        .patch_pos  = patch_pos,
        .patch_size = 4,
        .inst_end   = BS_get_cursor(buf),
        .id         = id,
        .kind       = kind,
    });
}

// -----------------------------------------------------------------------------------------


FunctionsRegistry FR_new(void) {
    Function* array = malloc(sizeof(Function)*FunctionsRegistry_default_cap);
    assert(array != NULL);
    return (FunctionsRegistry) {
        .array = array,
        .cap = FunctionsRegistry_default_cap,
        .len = 0
    };
}

inline void FR_free(FunctionsRegistry* fr) {
    free(fr->array);
}

void FR_register_function(FunctionsRegistry* fr, Function f) {
    if (FR_has_function(fr, f.name)) {
        fprintf(stderr, "[ERROR] inserting function when function with that name already exists, '%.*s'\n", (int)f.name.len, f.name.start);
        exit(1);
    }
    if (fr->len + 1 > fr->cap) {
        fr->cap *= 2;
        Function* new_array = realloc(fr->array,sizeof(Function)*fr->cap);
        assert(new_array != NULL);
        fr->array = new_array;
    }
    fr->array[fr->len++] = f;
}

void FR_overwrite_function(FunctionsRegistry* fr, Function f) {
    for (size_t n = 0; n < fr->len; n++) {
        if (SV__pp_cmp_eq(&fr->array[n].name, &f.name)) {
            fr->array[n] = f;
            return;
        }
    }
    fprintf(stderr, "[ERROR] <internal> function not found in registry\n");
    exit(1);
}

bool FR_has_function(FunctionsRegistry* fr, StringView name) {
    for (size_t n = 0; n < fr->len; n++) {
        if (SV__pp_cmp_eq(&fr->array[n].name, &name)) {
            return true;
        }
    }
    return false;
}

Function FR_lookup_function(FunctionsRegistry* fr, StringView name) {
    for (size_t n = 0; n < fr->len; n++) {
        if (SV__pp_cmp_eq(&fr->array[n].name, &name)) {
            return fr->array[n];
        }
    }
    fprintf(stderr, "[ERROR] <internal> function not found in registry\n");
    exit(1);
}

// -----------------------------------------------------------------------------------------


FunctionCallPatchList FCPL_new(void) {
    FunctionCallPatch* array = malloc(sizeof(FunctionCallPatch)*FunctionCallPatchList_default_cap);
    assert(array != NULL);
    return (FunctionCallPatchList) {
        .array = array,
        .len = 0,
        .cap = FunctionCallPatchList_default_cap,
    };
}

inline void FCPL_free(FunctionCallPatchList* fclp) {
    free(fclp->array);
}

void FCPL_register_pach(FunctionCallPatchList* fl, FunctionCallPatch fp) {
    if (fl->len + 1 > fl->cap) {
        fl->cap *= 2;
        FunctionCallPatch* new_array = realloc(fl->array, sizeof(FunctionCallPatch) * fl->cap);
        assert(new_array != NULL);
        fl->array = new_array;
    }

    fl->array[fl->len++] = fp;
}


// -----------------------------------------------------------------------------------------


GlobalsRegistry GR_new(void) {
    Global* array = malloc(sizeof(Global)*GlobalsRegistry_default_cap);
    assert(array != NULL);
    return (GlobalsRegistry) {
        .array = array,
        .cap = GlobalsRegistry_default_cap,
        .len = 0,
    };
}

inline void GR_free(GlobalsRegistry* gr) {
    free(gr->array);
}

size_t GR_register_global(GlobalsRegistry* gr, Global g) {
    if (gr->len + 1 > gr->cap) {
        gr->cap *= 2;
        Global* new_array = realloc(gr->array, sizeof(Global)*gr->cap);
        assert(new_array != NULL);
        gr->array = new_array;
    }
    gr->array[gr->len++] = g;
    return gr->len - 1;
}
bool GR_has_global(GlobalsRegistry* gr, StringView g) {
    for (size_t n=0; n< gr->len; n++) {
        if (SV__pp_cmp_eq(&gr->array[n].name, &g)) {
            return true;
        }
    }
    return false;
}

Global GR_lookup_global(GlobalsRegistry* gr, StringView g) {
    for (size_t n=0; n< gr->len; n++) {
        if (SV__pp_cmp_eq(&gr->array[n].name, &g)) {
            return gr->array[n];
        }
    }
    fprintf(stderr, "[ERROR] <internal> global variable lookup of non-existent variable '%.*s'\n", (int)g.len, g.start);
    exit(1);
}

size_t GR_lookup_global_index(GlobalsRegistry* gr, StringView g) {
    for (size_t n=0; n< gr->len; n++) {
        if (SV__pp_cmp_eq(&gr->array[n].name, &g)) {
            return n;
        }
    }
    fprintf(stderr, "[ERROR] <internal> global variable index lookup of non-existent variable '%.*s'\n", (int)g.len, g.start);
    exit(1);
}


GlobalPatchList GPL_new(void) {
    GlobalPatch* array = malloc(sizeof(GlobalPatch)*GlobalPatchList_default_cap);
    assert(array != NULL);
    return (GlobalPatchList) {
        .array = array,
        .cap = GlobalPatchList_default_cap,
        .len = 0,
    };
}
inline void GPL_free(GlobalPatchList* gpl) {
    free(gpl->array);
}
void GPL_register_patch(GlobalPatchList* gpl, GlobalPatch gp) {
    // printf("[DEBUG] patch index = %lu\n", gp.index);
    if (gpl->len + 1 > gpl->cap) {
        gpl->cap *= 2;
        GlobalPatch* new_array = realloc(gpl->array,sizeof(GlobalPatch)*gpl->cap);
        assert(new_array != NULL);
        gpl->array = new_array;
    }
    gpl->array[gpl->len++] = gp;
}



// -----------------------------------------------------------------------------------------

StringConstAddrRelocationList SCARL_new(void) {
    StringConstAddrRelocation* array = malloc(sizeof(StringConstAddrRelocation) * StringConstAddrRelocationList_default_cap);
    assert(array != NULL);
    return (StringConstAddrRelocationList) {
        .array = array,
        .len = 0,
        .cap = StringConstAddrRelocationList_default_cap

    };
}

void SCARL_push(StringConstAddrRelocationList* scl, StringConstAddrRelocation scr) {
    if (scl->len + 1 > scl->cap) {
        scl->cap *= 2;
        StringConstAddrRelocation* new_array = realloc(scl->array, sizeof(StringConstAddrRelocation) * scl->cap);
        assert(new_array != NULL);
        scl->array = new_array;
    }
    scl->array[scl->len++] = scr;
}

inline void SCARL_free(StringConstAddrRelocationList* scl) {
    free(scl->array);
}


void SCARL_resolve(StringConstAddrRelocationList* scarl, uint8_t* array, uint64_t const_section_load_addr, StringViewList* string_consts) {
    for (size_t n=0; n < scarl->len; n++) {
        StringConstAddrRelocation rel = scarl->array[n];
        size_t const_offset = 1; // first byte is NULL
        for (size_t i=0; i < rel.const_index;  i++) {
            const_offset += string_consts->array[i].len + 1; // for NULL
        }
        if (rel.bit_size == 4) {
            uint32_t addr = (uint32_t)const_section_load_addr + (uint32_t)const_offset;
            memcpy(array + rel.patch_offset, &addr, 4);
        } else if (rel.bit_size == 8) {
            uint64_t addr = (uint64_t)const_section_load_addr + (uint64_t)const_offset;
            memcpy(array + rel.patch_offset, &addr, 8);
        } else {
            fprintf(stderr, "[ERROR] <internal> string_const_rel.bit_size != 4 | 8, = %i\n", rel.bit_size);
            exit(1);
        }

    }
}



// -----------------------------------------------------------------------------------------

void resolve_function_calls(uint8_t* array, size_t local_code_size, FunctionsRegistry* fr, FunctionCallPatchList* patches) {
    for (size_t i = 0; i < patches->len; i++) {
        FunctionCallPatch* patch = &patches->array[i];

        if (!FR_has_function(fr, patch->name)) {
            fprintf(stderr, "[ERROR] unknown function '%.*s'\n", (int)patch->name.len, patch->name.start);
            exit(1);
        }

        Function f = FR_lookup_function(fr, patch->name);
        size_t target = f.offset;

        // size_t target = f.offset;
        // size_t* target = FR_lookup(fr, patch->name);
        // if (target == NULL) {
        //     fprintf(stderr, "resolve_function_calls: unresolved symbol '%.*s'\n",
        //             (int)patch->name.len, patch->name.ptr);
        //     continue;
        // }

        // size_t patch_site_addr = addr_offset + patch->offset;
        uint64_t value;
        // printf("<%.*s> patch->offset=%lu, target=%lu = 0x%x\n", (int)patch->name.len, patch->name.start, patch->offset, target, (uint8_t)target);

        uint64_t target_offset;
        if (patch->is_local) {
            target_offset = patch->offset;
        } else {
            target_offset = local_code_size + patch->offset;
        }


        if (patch->relative) {
            value = (uint64_t)(target - (target_offset + patch->bit_size));
        } else {
            fprintf(stderr, "unimplement absolute function patching\n");
            exit(1);
            // value = (uint64_t)target;
        }

        // if (!patch->is_local) {
        //     printf("target_offset = 0x%lx\n", target_offset);
        //     printf("value = 0x%lx\n", value);
        // }

        memcpy(array + target_offset, &value, patch->bit_size);
        // if (patch->is_local) {
        //     memcpy(array + patch->offset, &value, patch->bit_size);
        // } else {
        //     memcpy(array + target_offset, &value, patch->bit_size);
        // }
        // memcpy(array + patch->offset, &value, patch->bit_size);
    }
}


void resolve_globals(uint8_t* array, size_t bss_base_addr, GlobalsRegistry* gr, GlobalPatchList* gpl) {
    for (size_t i = 0; i < gpl->len; i++) {
        GlobalPatch* patch = &gpl->array[i];
        Global* global = &gr->array[patch->index];
        size_t addr = bss_base_addr + global->bss_offset;
        uint8_t* target = array + patch->offset;
        for (uint8_t b = 0; b < patch->bit_size; b++) {
            target[b] = (addr >> (b * 8)) & 0xFF;
        }
    }
}