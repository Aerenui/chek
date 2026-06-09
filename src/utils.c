#include "utils.h"

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>


#if defined(_WIN32) || defined(__MINGW32__)
  #include <stdlib.h>
  #define realpath(rel, abs) _fullpath((abs), (rel), PATH_MAX)
#else
  #include <stdlib.h>
#endif

char* read_file(const char *path, size_t *out_len) {
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


void resolve_import_path(const char* restrict importing_file, const char* restrict import_path, char* restrict out) {
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

inline bool SV_pp_cmp_eq(const StringView *sv1, const StringView *sv2) {
    if (sv1->len != sv2->len) {
        return false;
    }
    return memcmp(sv1->start, sv2->start, sv1->len) == 0;
}

inline bool SV_pv_cmp_eq(const StringView* restrict sv1, const char* restrict sv2, const size_t sv2_len) {
    assert(strlen(sv2) == sv2_len);
    if (sv1->len != sv2_len) {
        return false;
    }
    return memcmp(sv1->start, sv2, sv1->len) == 0;
}

inline StringView SV_lslice_from_SV(const StringView* sv, const size_t new_size) {
    assert(sv->len >= new_size);
    return (StringView) { .start = sv->start, .len = new_size };
}

inline StringView SV_lrslice_from_SV(const StringView* sv, const size_t start_index, const size_t len) {
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
