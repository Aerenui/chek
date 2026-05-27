#include "utils.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>


char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(1);
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        perror("fseek");
        exit(1);
    }

    long len = ftell(f);
    if (len < 0) {
        perror("ftell");
        exit(1);
    }
    rewind(f);

    char *buf = malloc(len + 1);
    if (!buf) {
        perror("malloc");
        exit(1);
    }

    size_t read = fread(buf, 1, len, f);
    if ((long)read != len) {
        fprintf(stderr, "%s: read %zu of %ld bytes\n", path, read, len);
        free(buf);
        exit(1);
    }

    fclose(f);
    buf[len] = '\0';
    if (out_len) *out_len = read;

    return buf;
}


void resolve_import_path(const char* importing_file, const char* import_path, char* out) {
    char dir[PATH_MAX];
    // strncpy(dir, importing_file, PATH_MAX); // PATH_MAX is guaranteed by realpath that produced import_path
    strcpy(dir, importing_file);

    char* last_slash = strrchr(dir, '/');
    if (last_slash) {
        *(last_slash + 1) = '\0';
    } else {
        strcpy(dir, "./");
    }

    char combined[PATH_MAX];
    snprintf(combined, PATH_MAX, "%s%s", dir, import_path);

    realpath(combined, out);
}





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


const char* SV_to__c_string(const StringView* sv) {
    char* ot = malloc(sv->len+1);
    assert(ot != NULL);
    memcpy(ot, sv->start, sv->len);
    ot[sv->len] = 0;
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


// ------------------------------------------------------------------


// inline ByteSeg BS_new(void) {
//     return (ByteSeg) {
//         .array = {0},
//         .cursor = 0,
//         .size = ByteSeg_size
//     };
// }
//
// ByteSegList BSL_new(void) {
//     size_t cap = 4;
//     ByteSeg* array = malloc(cap * sizeof(ByteSeg));
//     for (size_t n=0; n < cap; n++)
//         array[n] = BS_new();
//
//     return (ByteSegList) {
//         .array = array,
//         .len = 0,
//         .cap = cap,
//         .cursor = 0,
//     };
// }
//
// void BS_print(ByteSeg* bs) {
//     for (int i = 0; i < ByteSeg_size; i++) {
//         if (i > 0 && i % 8 == 0) printf("\n");
//         printf("0x%02X ", bs->array[i]);
//     }
//     printf("\n");
// }
//
// inline void BS_write_byte(ByteSeg* bs, uint8_t val) {
//     bs->array[bs->cursor++] = val;
// }
//
// inline size_t BS_get_cursor(ByteSeg* bs) {
//     return bs->cursor;
// }
//
// void BS_set_cursor(ByteSeg* bs, size_t cur) {
//     assert(cur <= bs->size);
//     bs->cursor = cur;
// }
//
// inline bool BS_has_space(ByteSeg* bs) {
//     return bs->cursor < bs->size;
// }
//
// /*void BSL_write_byte(ByteSegList* bsl, uint8_t val) {
//     if (BS_has_space(&bsl->array[bsl->cursor])) {
//         BS_write_byte(&bsl->array[bsl->cursor], val);
//         return;
//     }
//
//     bsl->len++;
//     if (bsl->len + 1 >= bsl->cap) {
//         size_t org_cap = bsl->cap;
//         if (bsl->cap == 0) bsl->cap = ByteSeg_size;
//         else bsl->cap *= 2;
//
//         ByteSeg* new_array = realloc(bsl->array, sizeof(ByteSeg) * bsl->cap);
//         assert(new_array != NULL);
//         bsl->array = new_array;
//         for (size_t n = org_cap; n < bsl->cap; n++) {
//             bsl->array[n] = BS_new();
//         }
//         // memset(bsl->array + org_cap, 0, (bsl->cap - org_cap) * sizeof(ByteSeg));
//     }
//
//     bsl->cursor++;
//     BS_write_byte(&bsl->array[bsl->cursor], val);
// }*/
// void BSL_write_byte(ByteSegList* bsl, uint8_t val) {
//     if (bsl->len == 0) {
//         bsl->array[0] = BS_new();
//         bsl->len++;
//     }
//
//     if (BS_has_space(&bsl->array[bsl->cursor])) {
//         BS_write_byte(&bsl->array[bsl->cursor], val);
//         return;
//     }
//
//     bsl->len++;  // current segment is now full, record it
//
//     if (bsl->len + 1 >= bsl->cap) {
//         size_t org_cap = bsl->cap;
//         if (bsl->cap == 0) bsl->cap = 4;
//         else bsl->cap *= 2;
//
//         ByteSeg* new_array = realloc(bsl->array, sizeof(ByteSeg) * bsl->cap);
//         assert(new_array != NULL);
//         bsl->array = new_array;
//         for (size_t n = org_cap; n < bsl->cap; n++)
//             bsl->array[n] = BS_new();
//         // remove the memset — it overwrites the BS_new() you just did
//     }
//
//     bsl->cursor++;
//     BS_write_byte(&bsl->array[bsl->cursor], val);
// }
//
// inline ByteSegList_BytePosition BSL_get_cursor(ByteSegList* bsl) {
//     return (ByteSegList_BytePosition) { .block_nth = bsl->cursor, .byte_nth = BS_get_cursor(&bsl->array[bsl->cursor]) };
// }
//
// inline void BSL_set_cursor(ByteSegList* bsl, ByteSegList_BytePosition cur) {
//     bsl->cursor = cur.block_nth;
//     bsl->array[bsl->cursor].cursor = cur.byte_nth;
// }

inline ByteSeg BS_new(void) {
    uint8_t* array = calloc(ByteSeg_init_cap, sizeof(uint8_t));
    assert(array != NULL);
    return (ByteSeg) {
        .array = array,
        .len = 0,
        .cap = ByteSeg_init_cap,
        .cursor = 0,
    };
}

inline void BS_free(ByteSeg* bs) {
    free(bs->array);
}

void BS_write_guard(ByteSeg* bs, size_t write_size) {
    size_t org_cap = bs->cap;
    while (bs->cursor + write_size > bs->cap) bs->cap *= 2;
    if (org_cap != bs->cap) {
        uint8_t* new_array = realloc(bs->array, bs->cap);
        assert(new_array != NULL);
        bs->array = new_array;
    }
}

inline void BS_write(ByteSeg* bs, uint8_t val) {
    BS_write_guard(bs, 1);
    bs->array[bs->cursor++] = val;
    if (bs->cursor > bs->len) {
        bs->len = bs->cursor;
    }
}

void BS_write_array(ByteSeg* bs, const size_t size, uint8_t array[restrict size]) {
    BS_write_guard(bs, size);
    memcpy(&bs->array[bs->cursor], array, size);
    bs->cursor += size;
    if (bs->cursor > bs->len) {
        bs->len = bs->cursor;
    }
}

void BS_print(const ByteSeg* bs) {
    for (size_t i = 0; i < bs->len; i++) {
        if (i > 0 && i % 8 == 0) printf("\n");
        printf("0x%02X ", bs->array[i]);
    }
    printf("\n");
}

inline size_t BS_get_cursor(ByteSeg* bs) {
    return bs->cursor;
}

inline void BS_set_cursor(ByteSeg* bs, size_t cursor) {
    bs->cursor = cursor;
}


// --------

//
// inline ByteSegList BSL_new(void) {
//     ByteSeg* array = malloc(sizeof(ByteSeg) * ByteSegList_init_cap);
//     assert(array != NULL);
//     return (ByteSegList) {
//         .array = array,
//         .cap = ByteSegList_init_cap,
//         .len = 0,
//         .cursor = 0,
//     };
// }
// inline void BSL_free(ByteSegList* bsl) {
//        free(bsl->array);
// }
//
// void BSL_write(ByteSegList* bsl, uint8_t val) {
//     if (bsl->len == 0) {
//         bsl->array[0] = BS_new();
//         bsl->len++;
//     }
//     BS_write(&bsl->array[bsl->cursor], val);
// }
//
