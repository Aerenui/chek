#ifndef SIMPLECOMPILERINC_2_UTILS_H
#define SIMPLECOMPILERINC_2_UTILS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>


typedef struct {
    const char* start;
    size_t len;
} StringView;


StringView SV_from_string(const char*);
StringView SV_from_string_len(const char*, size_t);
StringView SV_lslice_from_SV(const StringView*, size_t);
StringView SV_lrslice_from_SV(const StringView*, size_t, size_t);

void SV_p_printf(const StringView*);

bool SV__pp_cmp_eq(const StringView*, const StringView*);
bool SV__pv_cmp_eq(const StringView*, const char*, const size_t);



// ---------------------------------------------------

typedef struct {
    StringView* array;
    size_t len;
    size_t cap;
} StringViewList;

typedef struct {
    StringView* array;
    size_t len;
} StringViewListView;

#define StringViewList_cap 4

StringViewList SVL_new(void);
void SVL_p_free(StringViewList*);

void SVL_p_push(StringViewList*, StringView sv);
StringView SVL_p_pop(StringViewList*);
StringView* SVL_p_inspect_back(StringViewList*);

StringViewList SV_p_split_by_char(const StringView*, const char);

StringViewListView SVLV_lslice(const StringViewList*, const size_t);
StringViewListView SVLV_from_SVL(const StringViewList*);

StringView SVLV_consume_one(StringViewListView*);
StringView SVLV_inspect_back(StringViewListView*);
bool SVLV_is_empty(StringViewListView*);



#endif //SIMPLECOMPILERINC_2_UTILS_H