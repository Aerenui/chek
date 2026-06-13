
#include "x86_64.h"

#include "compiler.h"



/// mov eax, SRC
void emit_mov_eax(ByteSeg* out, const Loc src, GlobalPatchList* gpl, const CompilerTarget target) {
    switch (src.kind) {
        case LOC_IMMEDIATE:
            BS_write(out, 0xB8);
            BS_write(out, (src.value >>  0) & 0xFF);
            BS_write(out, (src.value >>  8) & 0xFF);
            BS_write(out, (src.value >> 16) & 0xFF);
            BS_write(out, (src.value >> 24) & 0xFF);
            break;
        case LOC_VAR:
        case LOC_STACK_SLOT: {
            const int slot = (src.kind == LOC_VAR) ? lookup_var(src.var) : src.offset;
            if (slot <= 128) {
                BS_write(out, 0x8B);
                BS_write(out, 0x45);
                BS_write(out, (uint8_t)(-slot));
            } else {
                const int32_t disp = -slot;

                BS_write(out, 0x8B);
                BS_write(out, 0x85);         // ModR/M for [rbp + disp32]

                // Write the 32-bit negative displacement (Little-Endian)
                BS_write(out, (uint8_t)(disp & 0xFF));
                BS_write(out, (uint8_t)((disp >> 8) & 0xFF));
                BS_write(out, (uint8_t)((disp >> 16) & 0xFF));
                BS_write(out, (uint8_t)((disp >> 24) & 0xFF));
            }
            break;
        }
        case LOC_GLOBAL: {
            switch (target) {
                case F_Elf64: {
                    const size_t pos = BS_get_cursor(out) + 3;
                    BS_write(out, 0x8B);        // MOV eax, [addr32]
                    BS_write(out, 0x04);
                    BS_write(out, 0x25);
                    BS_write(out, 0x00); BS_write(out, 0x00);
                    BS_write(out, 0x00); BS_write(out, 0x00);
                    GPL_register_patch(gpl, (GlobalPatch){
                        .index = src.offset,
                        .offset = pos,
                        .bit_size = 4,
                    });
                    break;
                }
                case F_Win64: {
                    const size_t pos = BS_get_cursor(out) + 2;
                    BS_write(out, 0x48); BS_write(out, 0xB9);   // MOV rcx, imm64
                    BS_write(out, 0x00); BS_write(out, 0x00);
                    BS_write(out, 0x00); BS_write(out, 0x00);
                    BS_write(out, 0x00); BS_write(out, 0x00);
                    BS_write(out, 0x00); BS_write(out, 0x00);
                    BS_write(out, 0x8B); BS_write(out, 0x01);   // MOV eax, [rcx]
                    GPL_register_patch(gpl, (GlobalPatch){
                        .index = src.offset,
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


/// OP eax, SRC
void emit_op_eax(ByteSeg* restrict out, const uint8_t opcode, const Loc src, GlobalPatchList* restrict gpl, const CompilerTarget target) {
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
            const int slot = (src.kind == LOC_VAR) ? lookup_var(src.var) : src.offset;
            if (slot <= 128) {
            BS_write(out, opcode);
            BS_write(out, 0x45);
            BS_write(out, (uint8_t)(-slot));
            } else {
                // 32-bit displacement path
                const int32_t disp = -slot;

                BS_write(out, opcode);
                BS_write(out, 0x85);               // ModR/M for [rbp + disp32]

                // Write 32-bit negative displacement (Little-Endian)
                BS_write(out, (uint8_t)(disp & 0xFF));
                BS_write(out, (uint8_t)((disp >> 8) & 0xFF));
                BS_write(out, (uint8_t)((disp >> 16) & 0xFF));
                BS_write(out, (uint8_t)((disp >> 24) & 0xFF));
            }
            break;
        }
        case LOC_GLOBAL: {
            switch (target) {
                case F_Elf64: {
                    const size_t pos = BS_get_cursor(out) + 3;
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
                    const size_t pos = BS_get_cursor(out) + 2;
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

/// imul eax, SRC
void emit_imul_eax(ByteSeg* restrict out, const Loc src, GlobalPatchList* restrict gpl, const CompilerTarget target) {
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
            const int slot = (src.kind == LOC_VAR) ? lookup_var(src.var) : src.offset;
            if (slot <= 128) {
                BS_write(out, 0x0F);
                BS_write(out, 0xAF);
                BS_write(out, 0x45);
                BS_write(out, (uint8_t)(-slot));
            } else {
                // 32-bit displacement path
                const int32_t disp = -slot;

                BS_write(out, 0x0F);
                BS_write(out, 0xAF);
                BS_write(out, 0x85);               // ModR/M for [rbp + disp32]

                // Write 32-bit negative displacement (Little-Endian)
                BS_write(out, (uint8_t)(disp & 0xFF));
                BS_write(out, (uint8_t)((disp >> 8) & 0xFF));
                BS_write(out, (uint8_t)((disp >> 16) & 0xFF));
                BS_write(out, (uint8_t)((disp >> 24) & 0xFF));
            }
            break;
        }
        case LOC_GLOBAL: {
            switch (target) {
                case F_Elf64: {
                    const size_t pos = BS_get_cursor(out) + 3;
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
                    const size_t pos = BS_get_cursor(out) + 2;
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

/// mov [rbp - slot], imm32
void emit_mov_slot_imm32(ByteSeg* restrict out, const int slot, const uint32_t imm) {
    if (slot <= 128) {
        BS_write(out, 0xC7);               // Opcode for MOV r/m32, imm32
        BS_write(out, 0x45);               // ModR/M for [rbp + disp8] (/0 extension)
        BS_write(out, (uint8_t) (-slot));   // 8-bit negative displacement
    } else {
        // 32-bit displacement path
        const int32_t disp = -slot;

        BS_write(out, 0xC7);               // Opcode for MOV r/m32, imm32
        BS_write(out, 0x85);               // ModR/M for [rbp + disp32] (/0 extension)

        // Write 32-bit negative displacement (Little-Endian)
        BS_write(out, (uint8_t)(disp & 0xFF));
        BS_write(out, (uint8_t)((disp >> 8) & 0xFF));
        BS_write(out, (uint8_t)((disp >> 16) & 0xFF));
        BS_write(out, (uint8_t)((disp >> 24) & 0xFF));
    }

    // Write 32-bit immediate value (Little-Endian)
    BS_write(out, (uint8_t)(imm & 0xFF));
    BS_write(out, (uint8_t)((imm >> 8) & 0xFF));
    BS_write(out, (uint8_t)((imm >> 16) & 0xFF));
    BS_write(out, (uint8_t)((imm >> 24) & 0xFF));
}

/// mov [rbp - slot], eax
void emit_mov_slot_eax(ByteSeg* restrict out, const int slot) {
    if (slot <= 128) {
        BS_write(out, 0x89);
        BS_write(out, 0x45);
        BS_write(out, (uint8_t) (-slot));
    } else {
        // 32-bit displacement path
        const int32_t disp = -slot;

        BS_write(out, 0x89);
        BS_write(out, 0x85);               // ModR/M for [rbp + disp32]

        // Write 32-bit negative displacement (Little-Endian)
        BS_write(out, (uint8_t)(disp & 0xFF));
        BS_write(out, (uint8_t)((disp >> 8) & 0xFF));
        BS_write(out, (uint8_t)((disp >> 16) & 0xFF));
        BS_write(out, (uint8_t)((disp >> 24) & 0xFF));
    }
}

/// lea rax, [rbp - slot]
void emit_lea_rax_slot(ByteSeg* restrict out, const int slot) {
    if (slot <= 128) {
        // 8-bit displacement path
        BS_write(out, 0x48);
        BS_write(out, 0x8D);
        BS_write(out, 0x45);               // ModR/M for [rbp + disp8]
        BS_write(out, (uint8_t)(-slot));
    } else {
        // 32-bit displacement path
        const int32_t disp = -slot;

        BS_write(out, 0x48);
        BS_write(out, 0x8D);
        BS_write(out, 0x85);               // ModR/M for [rbp + disp32]

        // Write 32-bit negative displacement (Little-Endian)
        BS_write(out, (uint8_t)(disp & 0xFF));
        BS_write(out, (uint8_t)((disp >> 8) & 0xFF));
        BS_write(out, (uint8_t)((disp >> 16) & 0xFF));
        BS_write(out, (uint8_t)((disp >> 24) & 0xFF));
    }
}