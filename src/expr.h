//
// Created by frantisek on 25. 5. 2026.
//

#ifndef SIMPLECOMPILERINC_2_EXPR_H
#define SIMPLECOMPILERINC_2_EXPR_H
#include "emit.h"
#include "utils.h"


Loc get_int_expr(StringViewListView*, CompilerTarget, bool is_local);

#endif //SIMPLECOMPILERINC_2_EXPR_H