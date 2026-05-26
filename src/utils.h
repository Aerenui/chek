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


// ---------------------------------------------------

// #define ByteSeg_size 64
// typedef struct {
//     uint8_t array[ByteSeg_size];
//     size_t cursor;
//     size_t size;
// } ByteSeg;
//
// typedef struct {
//     ByteSeg* array;
//     size_t len;
//     size_t cursor;
//     size_t cap;
// } ByteSegList;
//
// typedef struct {
//     size_t block_nth;
//     size_t byte_nth;
// } ByteSegList_BytePosition;
//
// ByteSeg BS_new(void);
// ByteSegList BSL_new(void);
//
// void BS_print(ByteSeg*);
//
// void BS_write_byte(ByteSeg*, uint8_t);
// size_t BS_get_cursor(ByteSeg*);
// void BS_set_cursor(ByteSeg*, size_t);
// bool BS_has_space(ByteSeg*);
//
// void BSL_write_byte(ByteSegList*, uint8_t);
// ByteSegList_BytePosition BSL_get_cursor(ByteSegList*);
// void BSL_set_cursor(ByteSegList*, ByteSegList_BytePosition);

typedef struct {
    uint8_t* array; // the data
    size_t len; // how much we have
    size_t cap; // how much we can store
    size_t cursor; // where are we writing
} ByteSeg;
#define ByteSeg_init_cap 64

ByteSeg BS_new(void);
void BS_free(ByteSeg*);
void BS_print(const ByteSeg*);
void BS_write(ByteSeg*, uint8_t);
void BS_write_array(ByteSeg* bs, size_t size, uint8_t array[restrict size]);
size_t BS_get_cursor(ByteSeg*);
void BS_set_cursor(ByteSeg*, size_t);



//
// typedef struct {
//     ByteSeg* array;
//     size_t len;
//     size_t cap;
//     size_t cursor;
// } ByteSegList;
// #define ByteSegList_init_cap 64
//
// ByteSegList BSL_new(void);
// void BSL_free(ByteSegList*);
// void BSL_write(ByteSegList*, uint8_t);

#endif //SIMPLECOMPILERINC_2_UTILS_H