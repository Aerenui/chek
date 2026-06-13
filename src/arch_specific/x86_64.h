
#ifndef SIMPLECOMPILERINC_2_X86_64_H
#define SIMPLECOMPILERINC_2_X86_64_H
#include "codegen.h"
#include "utils.h"


void emit_mov_eax(ByteSeg* out, Loc src, GlobalPatchList* gpl, CompilerTarget target);
void emit_op_eax(ByteSeg* out, uint8_t opcode, Loc src, GlobalPatchList* gpl, CompilerTarget target);
void emit_imul_eax(ByteSeg* out, Loc src, GlobalPatchList* gpl, CompilerTarget target);
void emit_mov_slot_imm32(ByteSeg* restrict out, int slot, uint32_t imm);
void emit_mov_slot_eax(ByteSeg* restrict out, int slot);
void emit_lea_rax_slot(ByteSeg* restrict out, int slot);

#endif //SIMPLECOMPILERINC_2_X86_64_H
