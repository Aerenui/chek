#include "utils.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>


inline StringView SV_from_string(const char *s) {
    return (StringView){
        .start = s,
        .len = (uint32_t) strlen(s)
    };
}

inline StringView SV_from_string_len(const char *s, const size_t len) {
    return (StringView){
        .start = s,
        .len = len
    };
}


inline void SV_p_printf(const StringView *sv) {
    printf("%.*s", (int) sv->len, sv->start);
}

inline bool SV__pp_cmp_eq(const StringView *sv1, const StringView *sv2) {
    if (sv1->len != sv2->len) {
        return false;
    }
    return memcmp(sv1->start, sv2->start, sv1->len) == 0;
}

inline bool SV__pv_cmp_eq(const StringView* sv1, const char* sv2, const size_t sv2_len) {
    assert(strlen(sv2) == sv2_len);
    if (sv1->len != sv2_len) {
        return false;
    }
    return memcmp(sv1->start, sv2, sv1->len) == 0;
}

inline StringView SV_lslice_from_SV(const StringView* sv, size_t new_size) {
    assert(sv->len >= new_size);
    return (StringView) { .start = sv->start, .len = new_size };
}

inline StringView SV_lrslice_from_SV(const StringView* sv, size_t start_index, size_t len) {
    assert(sv->len >= (start_index + len));
    return (StringView) { .start = (sv->start)+sizeof(char)*start_index, .len = len };
}

StringViewList SV_p_split_by_char(const StringView* sv, const char c) {
    StringViewList ot = SVL_new();
    size_t buff_start = 0;
    for(size_t i = 0; i < sv->len; i++) {
        if (sv->start[i] == c) {
            if (buff_start < i) {
                SVL_p_push(&ot, SV_lrslice_from_SV(sv, buff_start, i - buff_start));
            }
            buff_start = i+1;
        }
    }
    if (buff_start < sv->len) {
        SVL_p_push(&ot, SV_lrslice_from_SV(sv, buff_start, sv->len - buff_start));
    }
    return ot;
}


// ---------------------------------------------------


inline StringViewList SVL_new(void) {
    StringView* array = malloc(sizeof(StringView) * StringViewList_cap);
    return (StringViewList){
        .array = array,
        .len = 0,
        .cap = StringViewList_cap,
    };
}

inline void SVL_p_free(StringViewList* svl) {
    free(svl->array);
}

/// ensures there is enough space for one new element in the array
void SVL_p_overflow_guard(StringViewList* svl) {
    const size_t original_cap = svl->cap;
    while (svl->len + 1 >= svl->cap) svl->cap *= 2;
    if (original_cap == svl->cap) return;
    StringView* new_array = realloc(svl->array, (sizeof(StringView)) * svl->cap);
    assert(new_array != NULL);
    svl->array = new_array;
}

inline void SVL_p_push(StringViewList* svl, const StringView sv) {
    SVL_p_overflow_guard(svl);
    assert(svl->array != NULL);
    svl->array[svl->len++] = sv;
}

inline StringView SVL_p_pop(StringViewList* svl) {
    assert(svl->array != NULL);
    assert(svl->len > 0);
    StringView ot = svl->array[svl->len-1];
    svl->len--;
    return ot;
}

inline StringView* SVL_p_inspect_back(StringViewList* svl) {
    assert(svl->array != NULL);
    assert(svl->len > 0);
    return &svl->array[svl->len-1];
}

StringViewListView SVLV_lslice(const StringViewList* svl, const size_t cnt) {
    assert(svl->len > cnt);
    return (StringViewListView) {
        .array = svl->array + cnt,
        .len = svl->len - cnt,
    };
}

StringView SVLV_consume_one(StringViewListView* svlv) {
    assert(svlv->len > 0);
    StringView ot = svlv->array[0];
    svlv->len--;
    svlv->array++;
    return ot;
}

StringView SVLV_inspect_back(StringViewListView* svlv) {
    assert(svlv->len > 0);
    return svlv->array[0];
}

inline bool SVLV_is_empty(StringViewListView* svlv) {
    return svlv->len == 0;
}

StringViewListView SVLV_from_SVL(const StringViewList* svl) {
    return (StringViewListView) {
        .array = svl->array,
        .len = svl->len,
    };
}