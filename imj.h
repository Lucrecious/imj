#ifndef IMJ_H_
#define IMJ_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum imj_io_mode_t imj_io_mode_t;
enum imj_io_mode_t {
    IMJ_WRITE,
    IMJ_READ,
};

typedef struct imj_sv_t imj_sv_t;
struct imj_sv_t {
    const char *data;
    size_t length;
};


typedef struct imj_region_t imj_region_t;
struct imj_region_t {
    imj_region_t *prev;
    size_t capacity;
    size_t count;
    char data[];
};

typedef struct imj_arena_t imj_arena_t;
struct imj_arena_t {
    imj_region_t *region_back;
};

typedef struct imj_key_t imj_key_t;
struct imj_key_t {
    imj_sv_t name;
    char *loc;
};

typedef struct imj_keys_t imj_keys_t;
struct imj_keys_t {
    imj_key_t *items;
    size_t count;
    size_t capacity;
};

typedef enum imj_val_kind_t imj_val_kind_t;
enum imj_val_kind_t {
    IMJ_NONE = 0,
    IMJ_BOOL,
    IMJ_NULL,
    IMJ_STRING,
    IMJ_NUMBER,

    // util
    IMJ_OBJECT,
    IMJ_ARRAY,
    IMJ_KEY_VALUE,
};

typedef struct imj_lvl_t imj_lvl_t;
struct imj_lvl_t {
    imj_val_kind_t type;
    imj_lvl_t *prev;
    char *left_off_or_null;
    imj_keys_t keys;
    size_t count;
};

typedef struct imj_val_t imj_val_t;
struct imj_val_t {
    imj_val_kind_t kind;
    long i;
    size_t s;
    double d;
    imj_sv_t sv;
    bool b;
};

typedef struct imj_sb_t imj_sb_t;
struct imj_sb_t {
    char *items;
    size_t count;
    size_t capacity;
};

typedef enum imj_render_style_t imj_render_style_t;
enum imj_render_style_t {
    IMJ_STYLE_MIN = 0,
    IMJ_STYLE_SINGLE_LINE,
    IMJ_STYLE_PRETTY,
};

typedef struct imj_t imj_t;
struct imj_t {
    const char *filepath;
    imj_io_mode_t io_mode;
    imj_arena_t arena;
    imj_lvl_t *lvl_or_null;
    bool done;

    // reading
    imj_sv_t src;
    char *current;
    bool value_pending;
    bool log_errors;
    bool had_error;

    // writing
    imj_sb_t sb;
    imj_render_style_t render_style;
    size_t indent_lvl;
    size_t indent_size;
};

typedef void*(*imj_alloc)(void *allocator, size_t size_bytes);

imj_sv_t imj_cstr2sv(const char *cstr);
bool imj_sv_cstr_eq(imj_sv_t sv, const char *cstr);
bool imj_rawsv_to_cstrn(imj_sv_t sv, char *buffer, size_t n);

bool imj_file(const char *filepath, imj_t *imj, imj_io_mode_t mode);
bool imjw_flush(imj_t *imj);
void imjr_cstrn(const char *cstr, size_t n, imj_t *imj);
void imjw_init(const char *cstr, size_t n, imj_t *imj);

void imj_free(imj_t *lson);

bool imj_key(imj_t *imj, const char *key);

bool imj_begin_obj(imj_t *imj);
void imj_end_obj(imj_t *imj);
bool imj_begin_arr_ex(imj_t *imj, size_t *count);
bool imj_begin_arr(imj_t *imj);
void imj_end_arr(imj_t *imj);

bool imj_valnull(imj_t *imj);
bool imj_valb(imj_t *imj, bool *value, bool default_);
bool imj_vali(imj_t *imj, int *value, int default_);
bool imj_vals(imj_t *imj, size_t *value, size_t default_);
bool imj_valf(imj_t *imj, float *value, float default_);
bool imj_vald(imj_t *imj, double *value, double default_);
bool imj_valcstr(imj_t *imj, const char **value, const char *default_, imj_alloc alloc, void *allocator);
bool imj_valrawsv(imj_t *imj, imj_sv_t *value, const char *default_);

#endif

#ifdef IMJ_IMPLEMENTATION
#undef IMJ_IMPLEMENTATION

#define __imj_cases_non_zero '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9'

#define __imj_assert(cond, message) assert((cond) && (message))

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#define LSON_REGION_MIN_SIZE 1024

static const imj_val_t __imj_val_error = {
    .kind = IMJ_NONE,
};

static void *__imj_arena_alloc(imj_arena_t *arena, size_t size) {
    size = ((size+sizeof(uintptr_t)-1)/sizeof(uintptr_t)) * sizeof(uintptr_t);

    imj_region_t *region = arena->region_back;
    if (region && (region->count + size) > region->capacity) {
        region = NULL;
    } 

    if (region == NULL) {
        size_t s = size > LSON_REGION_MIN_SIZE ? size : LSON_REGION_MIN_SIZE;
        region = malloc(sizeof(imj_region_t) + s);
        region->prev = arena->region_back;
        region->capacity = s;
        region->count = 0;

        arena->region_back = region;
    }

    void *memory = region->data + region->count;
    region->count += size;
    return memory;
}

static void *__imj_arena_realloc(imj_arena_t *a, void *oldptr, size_t oldsz, size_t newsz) {
    if (newsz <= oldsz) return oldptr;
    void *newptr = __imj_arena_alloc(a, newsz);
    char *newptr_char = (char*)newptr;
    char *oldptr_char = (char*)oldptr;
    for (size_t i = 0; i < oldsz; ++i) {
        newptr_char[i] = oldptr_char[i];
    }
    return newptr;
}


#define __imj_da_push(arr, item, arena) do { \
    if ((arr)->count >= (arr)->capacity) { \
        size_t new_cap = (arr)->capacity == 0 ? 8 : (arr)->capacity*2; \
        while (new_cap < (arr)->capacity) new_cap *= 2; \
        (arr)->items = __imj_arena_realloc(arena, (arr)->items, sizeof(*(arr)->items)*(arr)->capacity, sizeof(*(arr)->items)*new_cap); \
        (arr)->capacity = new_cap; \
    } \
    (arr)->items[(arr)->count++] = (item); \
} while (false);

static void __imj_arena_free(imj_arena_t *arena) {
    while (arena->region_back) {
        imj_region_t *r = arena->region_back->prev;
        free(arena->region_back);
        arena->region_back = r;
    }
}

imj_sv_t imj_cstr2sv(const char *cstr) {
    return (imj_sv_t) {
        .data = cstr,
        .length = strlen(cstr),
    };
}

static void __imjr_skip_whitespace(imj_t *imj);

static void __imjr_init(const char *filepath, const char *str, size_t n, imj_t *ret) {
    ret->filepath = filepath;
    ret->src.data = str;
    ret->src.length = n;
    ret->current = (char*)ret->src.data;
    ret->log_errors = true;
    ret->io_mode = IMJ_READ;

    __imjr_skip_whitespace(ret);
    if (*ret->current != '\0') {
        ret->value_pending = true;
    }
}

static void __imjw_init(const char *filepath, imj_t *ret) {
    ret->filepath = filepath;
    ret->io_mode = IMJ_WRITE;
    ret->indent_size = 2;
}

bool imj_file(const char *filepath, imj_t *imj, imj_io_mode_t mode) {
    *imj = (imj_t){0};

    switch (mode) {
    case IMJ_READ: {
        FILE *file = fopen(filepath, "r");
        
        if (!file) return false;
        
        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        fseek(file, 0, SEEK_SET);
        
        char *buffer = __imj_arena_alloc(&imj->arena, size + 1);
        if (!buffer) {
            fclose(file);
            return false;
        }
        
        fread(buffer, 1, size, file);
        buffer[size] = '\0';
        fclose(file);

        __imjr_init(filepath, buffer, size, imj);
        break;
    }

    case IMJ_WRITE: {
        __imjw_init(filepath, imj);
        break;
    }
    }


    return true;
}

void imjr_cstrn(const char *cstr, size_t n, imj_t *imj) {
    *imj = (imj_t){0};
    __imjr_init("", cstr, n, imj);
}

void imjw_init(const char *cstr, size_t n, imj_t *imj) {
    *imj = (imj_t){0};
    __imjw_init("", imj);
}

bool imjw_flush(imj_t *imj) {
    __imj_assert(imj->filepath[0] != '\0', "cannot flush without a filepath");

    switch (imj->io_mode) {
    case IMJ_READ: __imj_assert(false, "cannot flush in read mode"); return false;
    case IMJ_WRITE: {
        __imj_assert(imj->done, "must be finished to flush");

        FILE *file = fopen(imj->filepath, "w");
        
        if (!file) {
            printf("cannot open file %s\n", imj->filepath);
            return false;
        }

        fwrite(imj->sb.items, 1, imj->sb.count, file);
        fclose(file);
        return true;
    }
    }
}

void imj_free(imj_t *lson) {
    __imj_arena_free(&lson->arena);
    free(lson);
}

typedef enum imj_log_lvl_t imj_log_lvl_t;
enum imj_log_lvl_t {
    IMJ_LOG_INFO = 0,
    IMJ_LOG_ERROR,
};

static void __imj_log(imj_log_lvl_t lvl, const char *format, ...) {
    switch (lvl) {
    case IMJ_LOG_ERROR: printf("ERROR: "); break;
    case IMJ_LOG_INFO: printf("INFO: "); break;
    }

    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
}
bool imj_sv_cstr_eq(imj_sv_t sv, const char *cstr) {
    if (cstr == NULL) return false;

    size_t i = 0;
    char c = cstr[0];
    while (i < sv.length && c != '\0') {
        if (sv.data[i] != c) return false;
        ++i;
        c = cstr[i];

    }

    return i == sv.length && c == '\0';
}

bool imj_rawsv_to_cstrn(imj_sv_t sv, char *buffer, size_t n) {
    size_t count = 0;

    for (size_t i = 0; i < sv.length; ++i) {
        switch (sv.data[i]) {
        case '\\':  {
            ++i;
            switch (sv.data[i]) {
            case '"': buffer[count] = '"'; break;
            case '\\': buffer[count] = '\\'; break;
            case '/': buffer[count] = '/'; break;
            case 'b': buffer[count] = '\b'; break;
            case 'f': buffer[count] = '\f'; break;
            case 'n': buffer[count] = '\n'; break;
            case 'r': buffer[count] = '\r'; break;
            case 't': buffer[count] = '\t'; break;
            case 'u': {
                __imj_log(IMJ_LOG_ERROR, "unicode codepoints are not supported yet");
                return false;
            }
            default: return false;
            }
            break;
        }
        default: {
            buffer[count] = sv.data[i];
            break;
        }
        }

        ++count;
        if (count >= n) break;
    }

    return true;
}

static char __imjr_next(imj_t *imj) {
    if (*imj->current == '\0') return '\0';

    char c = *imj->current;
    ++imj->current;
    return c;
}

static bool __imjr_consume(imj_t *imj, char c) {
    char check = __imjr_next(imj);
    if (check == c) {
        return true;
    }

    return false;
}

static void __imjr_parse_error(imj_t *imj, const char *message) {
    if (imj->had_error) return;
    imj->had_error = true;

    if (!imj->log_errors) return;
    
    size_t line_count = 0;
    size_t col = 0;
    for (char *c = (char*)imj->src.data; c < imj->current; ++c) {
        if (*c == '\n') {
            ++line_count;
            col = 0;
        } else {
            ++col;
        }
    }

    __imj_log(IMJ_LOG_ERROR, "%s:%zu:%zu: %s", imj->filepath, line_count+1, col, message);
}

static bool __imjr_is_whitespace(char c) {
    switch (c) {
    case ' ': case '\n':
    case '\r': case '\t': return true;
    default: return false;
    }
}

static void __imjr_skip_whitespace(imj_t *imj) {
    while (__imjr_is_whitespace(*imj->current)) ++imj->current;
}

static bool __imjr_match(imj_t *imj, char c) {
    if (*imj->current == c) {
        __imjr_next(imj);
        return true;
    }

    return false;
}
static void __imj_pop_lvl(imj_t *imj) {
    imj->lvl_or_null = imj->lvl_or_null->prev;
}

static bool __imjr_read_str(imj_t *imj, imj_sv_t *ret) {
    char *start = imj->current;
    char *previous = imj->current++;
    while (true) {
        if (*previous == '\"') break;
        if (*previous == '\0') {
            __imjr_parse_error(imj, "found end of file before end of string.");
            return false;
        }

        if (*previous == '\\') {
            switch (*imj->current) {
            case '"': case '\\': case '/':
            case 'b': case 'f': case 'n':
            case 'r': case 't': {
                previous = imj->current++;
                break;
            }

            case 'u': {
                ++imj->current;

                for (size_t i = 0; i < 4; ++i) {
                    switch (*imj->current) {
                    // digits
                    case __imj_cases_non_zero: case '0':

                    // characters
                    case 'a': case 'A': case 'b': case 'B':
                    case 'c': case 'C': case 'd': case 'D':
                    case 'e': case 'E': case 'f': case 'F': break;

                    default: {
                        __imjr_parse_error(imj, "invalid hex digit.");
                        return false;
                    }
                    }

                    ++imj->current;
                }

                --imj->current;
                break;
            }

            default: {
                __imjr_parse_error(imj, "invalid escape sequence.");
                return false;
            }
            }

        }

        previous = imj->current++;
    }

    *ret = (imj_sv_t){
        .data = start,
        .length = previous-start,
    };

    return true;
}

static void __imjr_skip_value(imj_t *imj);

static void __imjr_skip_str(imj_t *imj) {
    char *previous = imj->current++;
    while (*previous != '\"') {
        if (*previous == '\0') {
            __imjr_parse_error(imj, "source ended before string closed");
            return;
        }

        if (*previous == '\\' && *imj->current == '\"') {
            ++imj->current;
        }

        previous = imj->current++;
    }
}

static void __imjr_skip_obj(imj_t *imj) {
    while (true) {
        __imjr_skip_whitespace(imj);
        if (__imjr_match(imj, '}')) return;
        if (*imj->current == '\0') {
            __imjr_parse_error(imj, "expected '}' before end of file");
            return;
        }

        if (*imj->current == '\"') {
            ++imj->current;
            __imjr_skip_str(imj);
        } else {
            __imjr_parse_error(imj, "expected key");
            return;
        }
        
        __imjr_skip_whitespace(imj);
        if (!__imjr_consume(imj, ':')) {
            __imjr_parse_error(imj, "expected ':' after key");
            return;
        }

        __imjr_skip_whitespace(imj);
        __imjr_skip_value(imj);
        __imjr_skip_whitespace(imj);

        if (__imjr_match(imj, ',')) {
            __imjr_skip_whitespace(imj);
            if (*imj->current == '}') {
                __imjr_parse_error(imj, "cannot have ',' before ending an object");
                return;
            }
        }
    }
}

static void __imjr_skip_arr(imj_t *imj) {
    while (true) {
        __imjr_skip_whitespace(imj);
        if (__imjr_match(imj, ']')) return;

        if (*imj->current == '\0') {
            __imjr_parse_error(imj, "expected ']' before end of file");
            return;
        }

        __imjr_skip_value(imj);

        __imjr_skip_whitespace(imj);

        if (__imjr_match(imj, ',')) {
            __imjr_skip_whitespace(imj);
            if (*imj->current == ']') {
                __imjr_parse_error(imj, "cannot have ',' before ending an array");
                return;
            }
        }
    }
}

static void __imjr_skip_until_whitespace_or_comma(imj_t *imj) {
    while (true) {
        switch (*imj->current) {
        case '\0': case ',': case ']': case '}': return;
        default: break;
        }

        if (__imjr_is_whitespace(*imj->current)) return;
        ++imj->current;
    }
}

static void __imjr_skip_value(imj_t *imj) {
    switch (*imj->current) {
    case '"': {
        ++imj->current;
        __imjr_skip_str(imj);
        break;
    }

    case '{': {
        ++imj->current;
        __imjr_skip_obj(imj);
        break;
    }

    case '[': {
        ++imj->current;
        __imjr_skip_arr(imj);
        break;
    }

    default: __imjr_skip_until_whitespace_or_comma(imj); break;
    }
}

static void __imj_dive_into_arr(imj_t *imj, bool use_left_off) {
    imj_lvl_t *arr = __imj_arena_alloc(&imj->arena, sizeof(imj_lvl_t));
    *arr = (imj_lvl_t){0};
    arr->type = IMJ_ARRAY;
    arr->prev = imj->lvl_or_null;
    arr->left_off_or_null = use_left_off ? imj->current : NULL;
    imj->lvl_or_null = arr;
    ++imj->indent_lvl;
}

static void __imjr_update_array_if_necessary(imj_t *imj) {
    if (imj->lvl_or_null && imj->lvl_or_null->type == IMJ_ARRAY) {
        ++imj->lvl_or_null->count;

        __imjr_skip_whitespace(imj);
        if (__imjr_match(imj, ',')) {
            imj->value_pending = true;
            __imjr_skip_whitespace(imj);
        } else {
            imj->value_pending = false;
        }
    }
}

static void __imjw_update_aggregate_count_if_necessary(imj_t *imj) {
    if (imj->lvl_or_null && (imj->lvl_or_null->type == IMJ_ARRAY || imj->lvl_or_null->type == IMJ_OBJECT)) {
        ++imj->lvl_or_null->count;
    }
}

static bool __imjr_begin_arr(imj_t *imj) {
    if (imj->had_error) return false;

    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(!imj->lvl_or_null || imj->lvl_or_null->type == IMJ_KEY_VALUE || imj->lvl_or_null->type == IMJ_ARRAY, "cannot begin array directly inside object");

    __imjr_skip_whitespace(imj);

    imj->value_pending = true;
    bool entered = false;
    if (!__imjr_match(imj, '[')) {
        imj->value_pending = false;
    } else {
        __imjr_skip_whitespace(imj);
        imj->value_pending = *imj->current != ']';
        entered = true;
    }

    __imj_dive_into_arr(imj, entered);

    return entered;
}

static void __imjw_sb_add_str(imj_sb_t *sb, const char *s, size_t n, imj_arena_t *arena) {
    for (size_t i = 0; i < n; ++i) __imj_da_push(sb, s[i], arena);
}

static void __imjw_sb_add_str_as_jsonstr(imj_sb_t *sb, const char *s, size_t n, imj_arena_t *arena) {
    __imj_da_push(sb, '"', arena);
    for (char *c = (char*)s; c < s+n; ++c) {
        if ((unsigned char)*c > 127) {
            __imj_assert(false, "unicode is not supported yet");
            return;
        }

        switch (*c) {
        case '"': 
        case '\\': 
        case '/':
        case '\b': {
            __imj_da_push(sb, '\\', arena);
            break;
        }
        
        case '\f': {
            __imj_da_push(sb, '\\', arena);
            __imj_da_push(sb, 'f', arena);
            break;
        }

        case '\n': {
            __imj_da_push(sb, '\\', arena);
            __imj_da_push(sb, 'n', arena);
            break;
        }

        case '\r': {
            __imj_da_push(sb, '\\', arena);
            __imj_da_push(sb, 'n', arena);
            break;
        }

        case '\t': {
            __imj_da_push(sb, '\\', arena);
            __imj_da_push(sb, 't', arena);
            break;
        }

        case '\v': case '\a': {
            __imj_assert(false, "unsupported escape sequences");
            break;
        }

        default: {
            __imj_da_push(sb, *c, arena);
            break;
        }
        }
    }

    __imj_da_push(sb, '"', arena);
}

static void __imjw_sb_add_indent(imj_t *imj) {
    for (size_t i = 0; i < imj->indent_lvl; ++i) {
        for (size_t i = 0; i < imj->indent_size; ++i) {
            __imj_da_push(&imj->sb, ' ', &imj->arena);
        }
    }
}

static void __imjw_add_comma_and_ws_if_necessary(imj_t *imj) {
    if (imj->lvl_or_null) {
        switch (imj->lvl_or_null->type) {
        case IMJ_KEY_VALUE: break;
        case IMJ_ARRAY:
        case IMJ_OBJECT: {
            if (imj->lvl_or_null->count == 0) {
                switch (imj->render_style) {
                case IMJ_STYLE_MIN: break;
                case IMJ_STYLE_SINGLE_LINE: {
                    __imjw_sb_add_str(&imj->sb, " ", 1, &imj->arena);
                    break;
                }
                case IMJ_STYLE_PRETTY: {
                    __imjw_sb_add_str(&imj->sb, "\n", 1, &imj->arena);
                    __imjw_sb_add_indent(imj);
                    break;
                }
                }
            } else {
                switch (imj->render_style)  {
                case IMJ_STYLE_MIN: {
                    __imjw_sb_add_str(&imj->sb, ",", 1, &imj->arena);
                    break;
                }
                case IMJ_STYLE_SINGLE_LINE: {
                    __imjw_sb_add_str(&imj->sb, ", ", 2, &imj->arena);
                    break;
                }
                case IMJ_STYLE_PRETTY: {
                    __imjw_sb_add_str(&imj->sb, ",\n", 2, &imj->arena);
                    __imjw_sb_add_indent(imj);
                    break;
                }
                }
            }

            ++imj->lvl_or_null->count;
            break;
        }
        default: __imj_assert(false, "invalid lvl type for writing"); break;
        }
    }
}

static void __imjw_begin_arr(imj_t *imj) {
    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl_or_null == NULL || imj->lvl_or_null->type == IMJ_KEY_VALUE || imj->lvl_or_null->type == IMJ_ARRAY, "cannot put values directly inside objects");

    __imjw_add_comma_and_ws_if_necessary(imj);

    __imjw_update_aggregate_count_if_necessary(imj);

    __imj_dive_into_arr(imj, false);

    __imjw_sb_add_str(&imj->sb, "[", 1, &imj->arena);
}

bool imj_begin_arr(imj_t *imj) {
    bool success = true;
    switch (imj->io_mode) {
    case IMJ_READ: {
        success = __imjr_begin_arr(imj);
        break;
    }
    case IMJ_WRITE: {
        __imjw_begin_arr(imj);
        break;
    }
    }

    return success;
}

static bool __imjr_begin_arr_ex(imj_t *imj, size_t *count) {
    if (imj->had_error) return false;

    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(!imj->lvl_or_null || imj->lvl_or_null->type == IMJ_KEY_VALUE || imj->lvl_or_null->type == IMJ_ARRAY, "cannot begin array directly inside object");

    __imjr_skip_whitespace(imj);

    bool value_pending = true;
    bool entered = false;
    if (!__imjr_match(imj, '[')) {
        *count = 0;
        value_pending = false;
    } else {
        __imjr_skip_whitespace(imj);

        char *start = imj->current;

        entered = true;
        size_t seeked_count = 0;

        while (true) {
            __imjr_skip_whitespace(imj);

            if (*imj->current == '\0') {
                __imjr_parse_error(imj, "expected ']' before end of file");
                *count = 0;
                break;
            }

            if (*imj->current == ']') {
                break;
            }

            __imjr_skip_value(imj);
            __imjr_skip_whitespace(imj);
            ++seeked_count;

            if (__imjr_match(imj, ',')) {
                __imjr_skip_whitespace(imj);

                if (*imj->current == ']') {
                    __imjr_parse_error(imj, "cannot use comma before closing array");
                    return false;
                }
            } else {
                if (*imj->current != ']') {
                    __imjr_parse_error(imj, "expected ']' to close array");
                    return false;
                }

                __imjr_skip_whitespace(imj);
            }
        }

        *count = seeked_count;

        __imjr_skip_whitespace(imj);
        imj->value_pending = value_pending && __imjr_match(imj, ']');
        imj->current = start;
    }

    __imj_dive_into_arr(imj, entered);

    return entered;
}

bool imj_begin_arr_ex(imj_t *imj, size_t *count) {
    bool success = true;
    switch (imj->io_mode) {
    case IMJ_READ: {
        success = __imjr_begin_arr_ex(imj, count);
        break;
    }
    case IMJ_WRITE: {
        __imjw_begin_arr(imj);
        break;
    }
    }

    return success;
}

static void __imjr_end_arr(imj_t *imj) {
    if (imj->had_error) return;

    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl_or_null && imj->lvl_or_null->type == IMJ_ARRAY, "ending array not inside of array");

    if (imj->lvl_or_null->left_off_or_null) {
        __imjr_skip_whitespace(imj);
        __imjr_skip_arr(imj);
    }

    __imj_pop_lvl(imj);
    --imj->indent_lvl;

    if (imj->lvl_or_null) {
        if (imj->lvl_or_null->type == IMJ_KEY_VALUE) {
            __imj_pop_lvl(imj);
        }

        __imjr_update_array_if_necessary(imj);
    }
}

static void __imjw_pop_necessary_lvls_after_val(imj_t *imj) {
    if (imj->lvl_or_null) {
        switch (imj->lvl_or_null->type) {
        case IMJ_ARRAY: break;
        case IMJ_KEY_VALUE: {
            __imj_pop_lvl(imj);
            break;
        }
        case IMJ_OBJECT: {
            __imj_assert(false, "invalid level");
            break;
        }

        default: __imj_assert(false, "invalid enum as level"); break;
        }
    }
}

static void __imjw_end_arr(imj_t *imj) {
    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl_or_null && imj->lvl_or_null->type == IMJ_ARRAY, "must be inside array to exit");

    size_t item_count = imj->lvl_or_null->count;

    __imj_pop_lvl(imj);
    --imj->indent_lvl;

    __imjw_pop_necessary_lvls_after_val(imj);

    if (item_count > 0) {
        switch (imj->render_style) {
        case IMJ_STYLE_MIN: break;
        case IMJ_STYLE_SINGLE_LINE: {
            __imjw_sb_add_str(&imj->sb, " ", 1, &imj->arena);
            break;
        }
        case IMJ_STYLE_PRETTY: {
            __imjw_sb_add_str(&imj->sb, "\n", 1, &imj->arena);
            __imjw_sb_add_indent(imj);
            break;
        }
        }
    }

    
    __imjw_sb_add_str(&imj->sb, "]", 1, &imj->arena);
}

void imj_end_arr(imj_t *imj) {
    switch (imj->io_mode) {
    case IMJ_READ: {
        __imjr_end_arr(imj);
        break;
    }
    case IMJ_WRITE: {
        __imjw_end_arr(imj);
        break;
    }
    }

    if (imj->lvl_or_null == NULL) {
        imj->done = true;
    }
}

static imj_lvl_t *__imj_dive_into_obj(imj_t *imj) {
    imj_lvl_t *obj = __imj_arena_alloc(&imj->arena, sizeof(imj_lvl_t));
    *obj = (imj_lvl_t){0};
    obj->type = IMJ_OBJECT;
    obj->prev = imj->lvl_or_null;
    imj->lvl_or_null = obj;

    ++imj->indent_lvl;

    return obj;
}

static bool __imjr_begin_obj(imj_t *imj) {
    if (imj->had_error) {
        return false;
    }

    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(!imj->lvl_or_null || imj->lvl_or_null->type == IMJ_KEY_VALUE || imj->lvl_or_null->type == IMJ_ARRAY, "cannot start object directly inside object");

    imj_lvl_t *obj = __imj_dive_into_obj(imj);

    bool incorrect_pending_value = imj->value_pending && *imj->current != '{';
    bool is_at_root = imj->lvl_or_null->prev == NULL;
    if (incorrect_pending_value || (!is_at_root && !imj->value_pending)) {
        obj->left_off_or_null = NULL;
        return false;
    }

    __imjr_skip_whitespace(imj);


    if (!__imjr_consume(imj, '{')) {
        __imjr_parse_error(imj, "expected '{' for object");
    }

    __imjr_skip_whitespace(imj);

    obj->left_off_or_null = imj->current;

    return true;
}

static void __imjw_begin_obj(imj_t *imj) {
    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl_or_null == NULL || imj->lvl_or_null->type == IMJ_KEY_VALUE || imj->lvl_or_null->type == IMJ_ARRAY, "cannot put values directly inside objects");

    __imjw_add_comma_and_ws_if_necessary(imj);

    __imjw_update_aggregate_count_if_necessary(imj);

    __imj_dive_into_obj(imj);

    __imjw_sb_add_str(&imj->sb, "{", 1, &imj->arena);
}

bool imj_begin_obj(imj_t *imj) {
    bool success = true;
    switch (imj->io_mode) {
    case IMJ_READ: {
        success = __imjr_begin_obj(imj);
        break;
    }
    case IMJ_WRITE: {
        __imjw_begin_obj(imj);
        break;
    }
    }

    return success;
}

static void __imjr_end_obj(imj_t *imj) {
    if (imj->had_error) return;

    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl_or_null && imj->lvl_or_null->type == IMJ_OBJECT, "cannot end object without being inside an object");

    if (imj->lvl_or_null->left_off_or_null) {
        imj->current = imj->lvl_or_null->left_off_or_null;
        __imjr_skip_obj(imj);
    }

    __imj_pop_lvl(imj);

    if (imj->lvl_or_null) {
        if (imj->lvl_or_null->type == IMJ_KEY_VALUE) {
            __imj_pop_lvl(imj);
        }

        __imjr_update_array_if_necessary(imj);
    }
}

static void __imjw_end_obj(imj_t *imj) {
    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl_or_null->type == IMJ_OBJECT, "must be inside array to exit");

    size_t key_value_count = imj->lvl_or_null->count;
    __imj_pop_lvl(imj);
    --imj->indent_lvl;

    __imjw_pop_necessary_lvls_after_val(imj);

    if (key_value_count > 0) {
        switch (imj->render_style) {
        case IMJ_STYLE_MIN: break;
        case IMJ_STYLE_SINGLE_LINE: {
            __imjw_sb_add_str(&imj->sb, " ", 1, &imj->arena);
            break;
        }
        case IMJ_STYLE_PRETTY: {
            __imjw_sb_add_str(&imj->sb, "\n", 1, &imj->arena);
            __imjw_sb_add_indent(imj);
            break;
        }
        }
    }

    __imjw_sb_add_str(&imj->sb, "}", 1, &imj->arena);
}

void imj_end_obj(imj_t *imj) {
    switch (imj->io_mode) {
    case IMJ_READ: {
        __imjr_end_obj(imj);
        break;
    }
    case IMJ_WRITE: {
        __imjw_end_obj(imj);
        break;
    }
    }

    if (imj->lvl_or_null == NULL) {
        imj->done = true;
    }
}

static void __imj_dive_into_key(imj_t *imj, bool value_pending) {
    imj_lvl_t *lvl = __imj_arena_alloc(&imj->arena, sizeof(imj_lvl_t));
    *lvl = (imj_lvl_t){0};
    lvl->type = IMJ_KEY_VALUE;
    lvl->prev = imj->lvl_or_null;
    imj->lvl_or_null = lvl;
    imj->value_pending = value_pending;
}

static bool __imjr_key(imj_t *imj, const char *key) {

    imj->value_pending = false;

    if (imj->had_error) return false;

    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl_or_null && imj->lvl_or_null->type == IMJ_OBJECT, "keys can only reside inside objects");

    imj_lvl_t *obj = imj->lvl_or_null;

    if (imj->lvl_or_null->left_off_or_null == NULL) {
        __imj_dive_into_key(imj, false);
        return false;
    }

    char *left_off = obj->left_off_or_null;

    for (size_t i = 0; i < obj->keys.count; ++i) {
        imj_key_t k = obj->keys.items[i];
        if (imj_sv_cstr_eq(k.name, key)) {
            imj->current = k.loc;
            __imj_dive_into_key(imj, true);
            return true;
        }
    }

    imj->current = left_off;

    while (true) {
        if (*imj->current == '\0') {
            __imjr_parse_error(imj, "object needs '}' to close");
            return false;
        }

        if (__imjr_match(imj, '\"')) {
            imj_sv_t key_name;
            if (!__imjr_read_str(imj, &key_name)) return false;

            __imjr_skip_whitespace(imj);

            if (!__imjr_consume(imj, ':')) {
                __imjr_parse_error(imj, "expected ':' after key");
                return false;
            }

            __imjr_skip_whitespace(imj);

            imj_key_t imj_key = {
                .name = key_name,
                .loc = imj->current,
            };

            __imj_da_push(&obj->keys, imj_key, &imj->arena);

            if (imj_sv_cstr_eq(key_name, key)) {
                __imj_dive_into_key(imj, true);
                return true;
            }


            __imjr_skip_value(imj);

            __imjr_skip_whitespace(imj);

            if (__imjr_match(imj, ',')) {
                __imjr_skip_whitespace(imj);
                if (*imj->current == '}') {
                    __imjr_parse_error(imj, "cannot end object with ','");
                    return false;
                }
            }

            obj->left_off_or_null = imj->current;
        } else {
            // no value consumed, we haven't found the key
            // but user expects value to be pending.
            if (__imjr_consume(imj, '}')) {
                __imj_dive_into_key(imj, false);
                return false;
            }

            __imjr_parse_error(imj, "unexpected character.");
            return false;
        }
    }
}

static void __imjw_key(imj_t *imj, const char *key) {
    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl_or_null && imj->lvl_or_null->type == IMJ_OBJECT, "keys can only reside inside objects");

    __imjw_add_comma_and_ws_if_necessary(imj);

    __imj_dive_into_key(imj, false);

    __imjw_sb_add_str_as_jsonstr(&imj->sb, key, strlen(key), &imj->arena);

    __imjw_sb_add_str(&imj->sb, ":", 1, &imj->arena);

    switch (imj->render_style) {
    case IMJ_STYLE_MIN: break;
    case IMJ_STYLE_SINGLE_LINE:
    case IMJ_STYLE_PRETTY: {
        __imjw_sb_add_str(&imj->sb, " ", 1, &imj->arena);
        break;
    }
    }
}

bool imj_key(imj_t *imj, const char *key) {
    bool success = true;
    switch (imj->io_mode) {
    case IMJ_READ: {
        success = __imjr_key(imj, key);
        break;
    }

    case IMJ_WRITE: {
        __imjw_key(imj, key);
        break;
    }
    }

    return success;
}

static bool __imjr_is_digit(char c) {
    switch (c) {
    case '0': case __imj_cases_non_zero: return true;
    }
    return false;
}

static bool __imj_read_num(imj_t *imj, imj_val_t *ret) {
    *ret = __imj_val_error;
    bool is_negative = __imjr_match(imj, '-');

    size_t nat = 0;
    double mag = 10;
    double dec = 0;

    bool has_exp = false;
    bool exp_neg = false;
    size_t exp = 0;


    switch (*imj->current) {
    case '0': {
        ++imj->current;
        break;
    }

    case __imj_cases_non_zero: {
        nat = *imj->current - '0';
        ++imj->current;
        while (__imjr_is_digit(*imj->current)) {
            size_t digit = *imj->current - '0';
            nat *= 10;
            nat += digit;
            ++imj->current;
        }
        break;
    }

    default: {
        __imjr_parse_error(imj, "expected digit");
        return false;
    }
    }

    if (__imjr_match(imj, '.')) {
        if (!__imjr_is_digit(*imj->current)) {
            __imjr_parse_error(imj, "expected digit");
            return false;
        }

        while (__imjr_is_digit(*imj->current)) {
            double d = (double)(*imj->current - '0');
            dec += d/mag;
            mag *= 10;

            ++imj->current;
        }
    }

    if (__imjr_match(imj, 'e') || __imjr_match(imj, 'E')) {
        has_exp = true;

        if (__imjr_match(imj, '-')) {
            exp_neg = true;
        } else __imjr_match(imj, '+');

        if (!__imjr_is_digit(*imj->current)) {
            __imjr_parse_error(imj, "expected digit");
            return false;
        }

        while (__imjr_is_digit(*imj->current)) {
            exp *= 10;
            exp += (*imj->current - '0');
            ++imj->current;
        }
    }

    double num = (double)nat + dec;
    if (has_exp) {
        num *= pow(10.0, (exp_neg ? -1.0 : 1.0)*exp);
    }

    if (is_negative) {
        num = -num;
    }

    ret->kind = IMJ_NUMBER;
    ret->s = has_exp ? (size_t)num : nat;
    ret->i = has_exp ? (size_t)num : (long)nat * (is_negative ? -1 : 1);
    ret->d = num;
    ret->b = num != 0;

    return true;
}

static bool __imjr_read_val(imj_t *imj, imj_val_t *ret) {
    switch (*imj->current) {
    case '{': {
        ++imj->current;
        __imjr_skip_obj(imj);
        ret->kind = IMJ_OBJECT;
        break;
    }

    case '[': {
        ++imj->current;
        __imjr_skip_arr(imj);
        ret->kind = IMJ_ARRAY;
        break;
    }

    case '0': case '-': case __imj_cases_non_zero: {
        *ret = (imj_val_t){0};
        if (!__imj_read_num(imj, ret)) return false;
        break;
    }

    case '"': {
        imj_sv_t sv;
        ++imj->current;
        if (!__imjr_read_str(imj, &sv)) return false;
        *ret = (imj_val_t){0};
        ret->kind = IMJ_STRING;
        ret->sv = sv;
        break;
    }

    default: {
        char *start = imj->current;
        __imjr_skip_until_whitespace_or_comma(imj);
        imj_sv_t sv = {
            .data = start,
            .length = imj->current-start,
        };

        if (imj_sv_cstr_eq(sv, "true")) {
            *ret = (imj_val_t){0};
            ret->kind = IMJ_BOOL;
            ret->b = true;
            ret->s = (size_t)true;
            ret->i = (long)true;
            ret->d = (double)true;
        } else if (imj_sv_cstr_eq(sv, "false") || imj_sv_cstr_eq(sv, "null")) {
            *ret = (imj_val_t){0};
            ret->kind = sv.data[0] == 'n' ? IMJ_NULL : IMJ_BOOL;
            ret->b = false;
            ret->s = (size_t)false;
            ret->i = (long)false;
            ret->d = (double)false;
        } else {
            __imjr_parse_error(imj, "unexpected value");
            return false;
        }
        break;
    }
    }

    return true;
}

static bool __imjr_use_default_value_and_pop_lvl_if_possible(imj_t *imj) {
    bool value_pending = imj->value_pending;
    imj->value_pending = false;

    if (imj->lvl_or_null) {
        switch (imj->lvl_or_null->type) {
        case IMJ_OBJECT: {
            __imjr_parse_error(imj, "cannot have objects directly inside objects");
            return true;
        }

        case IMJ_KEY_VALUE: {
            __imj_pop_lvl(imj);
            break;
        }

        case IMJ_ARRAY: break;

        default: assert(false); break;
    }
    }

    return !value_pending;
}

static bool __imjr_valnull(imj_t *imj) {
    if (imj->had_error) {
        return false;
    }

    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl_or_null == NULL || imj->lvl_or_null->type == IMJ_KEY_VALUE || imj->lvl_or_null->type == IMJ_ARRAY, "cannot put values directly inside objects");


    if (__imjr_use_default_value_and_pop_lvl_if_possible(imj)) {
        return false;
    }

    imj_val_t val;
    bool success = __imjr_read_val(imj, &val);

    __imjr_update_array_if_necessary(imj);

    return success && val.kind == IMJ_NULL;
}

static void __imjw_valnull(imj_t *imj) {
    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl_or_null == NULL || imj->lvl_or_null->type == IMJ_KEY_VALUE || imj->lvl_or_null->type == IMJ_ARRAY, "cannot put values directly inside objects");

    __imjw_add_comma_and_ws_if_necessary(imj);

    __imjw_sb_add_str(&imj->sb, "null", 4, &imj->arena);

    __imjw_pop_necessary_lvls_after_val(imj);
    __imjw_update_aggregate_count_if_necessary(imj);
}

bool imj_valnull(imj_t *imj) {
    bool success = true;
    switch (imj->io_mode) {
    case IMJ_READ: {
        success = __imjr_valnull(imj);
        break;
    }

    case IMJ_WRITE: {
        __imjw_valnull(imj);
        break;
    }
    }

    if (imj->lvl_or_null == NULL) {
        imj->done = true;
    }

    return success;
}

static bool __imjr_valb(imj_t *imj, bool *value, bool default_) {
    if (imj->had_error) {
        *value = default_;
        return false;
    }

    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl_or_null == NULL || imj->lvl_or_null->type == IMJ_KEY_VALUE || imj->lvl_or_null->type == IMJ_ARRAY, "cannot put values directly inside objects");


    if (__imjr_use_default_value_and_pop_lvl_if_possible(imj)) {
        *value = default_;
        return false;
    }

    imj_val_t val;
    bool success = __imjr_read_val(imj, &val);
    if (success && val.kind == IMJ_BOOL) {
        *value = (bool)val.i;
    } else {
        *value = default_;
    }

    __imjr_update_array_if_necessary(imj);

    return success && val.kind == IMJ_BOOL;
}

static void __imjw_valb(imj_t *imj, bool *value, bool default_) {
    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl_or_null == NULL || imj->lvl_or_null->type == IMJ_KEY_VALUE || imj->lvl_or_null->type == IMJ_ARRAY, "cannot put values directly inside objects");

    __imjw_add_comma_and_ws_if_necessary(imj);

    bool val = value ? *value : default_;
    __imjw_sb_add_str(&imj->sb,
        val ? "true" : "false",
        val ? 4 : 5,
        &imj->arena);

    __imjw_pop_necessary_lvls_after_val(imj);
    __imjw_update_aggregate_count_if_necessary(imj);
}

bool imj_valb(imj_t *imj, bool *value, bool default_) {
    bool success = true;
    switch (imj->io_mode) {
    case IMJ_READ: {
        success = __imjr_valb(imj, value, default_);
        break;
    }

    case IMJ_WRITE: {
        __imjw_valb(imj, value, default_);
        break;
    }
    }

    if (imj->lvl_or_null == NULL) {
        imj->done = true;
    }

    return success;
}

static bool __imjr_vali(imj_t *imj, int *value, int default_) {
    if (imj->had_error) {
        *value = default_;
        return false;
    }

    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl_or_null == NULL || imj->lvl_or_null->type == IMJ_KEY_VALUE || imj->lvl_or_null->type == IMJ_ARRAY, "cannot put values directly inside objects");


    if (__imjr_use_default_value_and_pop_lvl_if_possible(imj)) {
        *value = default_;
        return false;
    }

    imj_val_t val;
    bool success = __imjr_read_val(imj, &val);
    if (success && val.kind == IMJ_NUMBER) {
        *value = (int)val.i;
    } else {
        *value = default_;
    }

    __imjr_update_array_if_necessary(imj);

    return success && val.kind == IMJ_NUMBER;
}

static void __imjw_vali(imj_t *imj, int *value, int default_) {
    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl_or_null == NULL || imj->lvl_or_null->type == IMJ_KEY_VALUE || imj->lvl_or_null->type == IMJ_ARRAY, "cannot put values directly inside objects");

    __imjw_add_comma_and_ws_if_necessary(imj);

    int val = value ? *value : default_;
    char buffer[12];
    int len = sprintf(buffer, "%d", val);
    __imjw_sb_add_str(&imj->sb, buffer, len, &imj->arena);

    __imjw_pop_necessary_lvls_after_val(imj);
    __imjw_update_aggregate_count_if_necessary(imj);
}

bool imj_vali(imj_t *imj, int *value, int default_) {
    bool success = false;

    switch (imj->io_mode) {
    case IMJ_READ: {
        success = __imjr_vali(imj, value, default_);
        break;
    }

    case IMJ_WRITE: {
        __imjw_vali(imj, value, default_);
        break;
    }
    }

    if (imj->lvl_or_null == NULL) {
        imj->done = true;
    }

    return success;
}

static bool __imjr_vals(imj_t *imj, size_t *value, size_t default_) {
    if (imj->had_error) {
        *value = default_;
        return false;
    }

    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl_or_null == NULL || imj->lvl_or_null->type == IMJ_KEY_VALUE || imj->lvl_or_null->type == IMJ_ARRAY, "cannot put values directly inside objects");


    if (__imjr_use_default_value_and_pop_lvl_if_possible(imj)) {
        *value = default_;
        return false;
    }

    imj_val_t val;
    bool success = __imjr_read_val(imj, &val);
    if (success && val.kind == IMJ_NUMBER) {
        *value = (size_t)val.s;
    } else {
        *value = (size_t)default_;
    }

    __imjr_update_array_if_necessary(imj);

    return success && val.kind == IMJ_NUMBER;
}

static void __imjw_vals(imj_t *imj, size_t *value, size_t default_) {
    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl_or_null == NULL || imj->lvl_or_null->type == IMJ_KEY_VALUE || imj->lvl_or_null->type == IMJ_ARRAY, "cannot put values directly inside objects");

    __imjw_add_comma_and_ws_if_necessary(imj);

    size_t val = value ? *value : default_;
    char buffer[32];
    int len = sprintf(buffer, "%zu", val);
    __imjw_sb_add_str(&imj->sb, buffer, len, &imj->arena);

    __imjw_pop_necessary_lvls_after_val(imj);
    __imjw_update_aggregate_count_if_necessary(imj);
}

bool imj_vals(imj_t *imj, size_t *value, size_t default_) {
    bool success = true;
    switch(imj->io_mode) {
    case IMJ_READ: {
        success = __imjr_vals(imj, value, default_);
        break;
    }

    case IMJ_WRITE: {
        __imjw_vals(imj, value, default_);
        break;
    }
    }

    if (imj->lvl_or_null == NULL) {
        imj->done = true;
    }

    return success;
}

static bool __imjr_valf(imj_t *imj, float *value, float default_) {
    if (imj->had_error) {
        *value = default_;
        return false;
    }

    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl_or_null == NULL || imj->lvl_or_null->type == IMJ_KEY_VALUE || imj->lvl_or_null->type == IMJ_ARRAY, "cannot put values directly inside objects");


    if (__imjr_use_default_value_and_pop_lvl_if_possible(imj)) {
        *value = default_;
        return false;
    }

    imj_val_t val;
    bool success = __imjr_read_val(imj, &val);
    if (success && val.kind == IMJ_NUMBER) {
        *value = (float)val.d;
    } else {
        *value = (float)default_;
    }

    __imjr_update_array_if_necessary(imj);

    return success && val.kind == IMJ_NUMBER;
}

static void __imjw_valf(imj_t *imj, float *value, float default_) {
    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl_or_null == NULL || imj->lvl_or_null->type == IMJ_KEY_VALUE || imj->lvl_or_null->type == IMJ_ARRAY, "cannot put values directly inside objects");

    __imjw_add_comma_and_ws_if_necessary(imj);

	float val = value ? *value : default_;
	char buffer[32];
	int len = sprintf(buffer, "%g", val);
    __imjw_sb_add_str(&imj->sb, buffer, len, &imj->arena);

    __imjw_pop_necessary_lvls_after_val(imj);
    __imjw_update_aggregate_count_if_necessary(imj);
}

bool imj_valf(imj_t *imj, float *value, float default_) {
    bool success = true;
    switch(imj->io_mode) {
    case IMJ_READ: {
        success = __imjr_valf(imj, value, default_);
        break;
    }

    case IMJ_WRITE: {
        __imjw_valf(imj, value, default_);
        break;
    }
    }

    if (imj->lvl_or_null == NULL) {
        imj->done = true;
    }

    return success;
}

static bool __imjr_vald(imj_t *imj, double *value, double default_) {
    if (imj->had_error) {
        *value = default_;
        return false;
    }

    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl_or_null == NULL || imj->lvl_or_null->type == IMJ_KEY_VALUE || imj->lvl_or_null->type == IMJ_ARRAY, "cannot put values directly inside objects");

    if (__imjr_use_default_value_and_pop_lvl_if_possible(imj)) {
        *value = default_;
        return false;
    }

    imj_val_t val;
    bool success = __imjr_read_val(imj, &val);
    if (success && val.kind == IMJ_NUMBER) {
        *value = val.d;
    } else {
        *value = default_;
    }

    __imjr_update_array_if_necessary(imj);

    return success && val.kind == IMJ_NUMBER;
}

static void __imjw_vald(imj_t *imj, double *value, double default_) {
    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl_or_null == NULL || imj->lvl_or_null->type == IMJ_KEY_VALUE || imj->lvl_or_null->type == IMJ_ARRAY, "cannot put values directly inside objects");

    __imjw_add_comma_and_ws_if_necessary(imj);

	double val = value ? *value : default_;
    char buffer[32];
    int len = sprintf(buffer, "%g", val);
    __imjw_sb_add_str(&imj->sb, buffer, len, &imj->arena);

    __imjw_pop_necessary_lvls_after_val(imj);
    __imjw_update_aggregate_count_if_necessary(imj);
}

bool imj_vald(imj_t *imj, double *value, double default_) {
    bool success = true;
    switch(imj->io_mode) {
    case IMJ_READ: {
        success = __imjr_vald(imj, value, default_);
        break;
    }

    case IMJ_WRITE: {
        __imjw_vald(imj, value, default_);
        break;
    }
    }

    if (imj->lvl_or_null == NULL) {
        imj->done = true;
    }

    return success;
}

static bool __imjr_valrawsv(imj_t *imj, imj_sv_t *value, const char *default_) {
    if (imj->had_error) {
        *value = imj_cstr2sv(default_);
        return false;
    }

    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl_or_null == NULL || imj->lvl_or_null->type == IMJ_KEY_VALUE || imj->lvl_or_null->type == IMJ_ARRAY, "cannot put values directly inside objects");

    if (__imjr_use_default_value_and_pop_lvl_if_possible(imj)) {
        *value = imj_cstr2sv(default_);
        return false;
    }

    imj_val_t val;
    bool success = __imjr_read_val(imj, &val);
    if (success && val.kind == IMJ_STRING) {
        *value = val.sv;
    } else {
        *value = imj_cstr2sv(default_);
    }

    __imjr_update_array_if_necessary(imj);

    return success && val.kind == IMJ_STRING;
}

static void __imjw_valrawsv(imj_t *imj, imj_sv_t *value, const char *default_) {
    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl_or_null == NULL || imj->lvl_or_null->type == IMJ_KEY_VALUE || imj->lvl_or_null->type == IMJ_ARRAY, "cannot put values directly inside objects");

    __imjw_add_comma_and_ws_if_necessary(imj);

    const char *val = value ? value->data : default_;
    __imjw_sb_add_str_as_jsonstr(&imj->sb, val, value->length, &imj->arena);

    __imjw_pop_necessary_lvls_after_val(imj);
    __imjw_update_aggregate_count_if_necessary(imj);
}

bool imj_valrawsv(imj_t *imj, imj_sv_t *value, const char *default_) {
    bool success = true;
    switch (imj->io_mode) {
    case IMJ_READ: {
        success = __imjr_valrawsv(imj, value, default_);
        break;
    }

    case IMJ_WRITE: {
        __imjw_valrawsv(imj, value, default_);
        break;
    }
    }

    if (imj->lvl_or_null == NULL) {
        imj->done = true;
    }

    return success;
}

static bool __imjr_valcstr(imj_t *imj, const char **value, const char *default_, imj_alloc alloc, void *allocator) {
    imj_sv_t sv;
    bool success = imj_valrawsv(imj, &sv, default_);

    char *new_val = alloc(allocator, sv.length+1);
    imj_rawsv_to_cstrn(sv, new_val, sv.length);
    new_val[sv.length] = '\0';

    *value = new_val;

    return success;
}

static void __imjw_valcstr(imj_t *imj, const char **value, const char *default_) {
    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl_or_null == NULL || imj->lvl_or_null->type == IMJ_KEY_VALUE || imj->lvl_or_null->type == IMJ_ARRAY, "cannot put values directly inside objects");

    __imjw_add_comma_and_ws_if_necessary(imj);

    const char *val = value ? *value : default_;
    __imjw_sb_add_str_as_jsonstr(&imj->sb, val, strlen(val), &imj->arena);

    __imjw_pop_necessary_lvls_after_val(imj);
    __imjw_update_aggregate_count_if_necessary(imj);
}


bool imj_valcstr(imj_t *imj, const char **value, const char *default_, imj_alloc alloc, void *allocator) {
    bool success = true;
    switch (imj->io_mode) {
    case IMJ_READ: {
        success = __imjr_valcstr(imj, value, default_, alloc, allocator);
        break;
    }

    case IMJ_WRITE: {
        __imjw_valcstr(imj, value, default_);
        break;
    }
    }
    
    return success;
}

#endif
