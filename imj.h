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

typedef struct imj_t imj_t;
struct imj_t {
    const char *filepath;
    imj_io_mode_t io_mode;
    imj_sv_t src;
    char *current;
    imj_arena_t arena;
    imj_lvl_t *lvl;
    bool value_pending;

    bool done;
    bool log_errors;
    bool had_error;
};

typedef void*(*imj_alloc)(void *allocator, size_t size_bytes);

imj_sv_t imj_cstr2sv(const char *cstr);

bool imj_from_file(const char *filepath, imj_t *imj, imj_io_mode_t mode);
void imj_free(imj_t *lson);

bool imj_key(imj_t *imj, const char *key);
void imj_begin_obj(imj_t *imj);
void imj_end_obj(imj_t *imj);
void imj_begin_arr_ex(imj_t *imj, size_t *count);
void imj_begin_arr(imj_t *imj);
void imj_end_arr(imj_t *imj);

bool imj_valnull(imj_t *imj);
void imj_valb(imj_t *imj, bool *value, bool default_);
void imj_vali(imj_t *imj, int *value, int default_);
void imj_vals(imj_t *imj, size_t *value, size_t default_);
void imj_valf(imj_t *imj, float *value, float default_);
void imj_vald(imj_t *imj, double *value, double default_);
void imj_valcstr(imj_t *imj, const char **value, const char *default_, imj_alloc alloc, void *allocator);
void imj_valsv(imj_t *imj, imj_sv_t *value, const char *default_);

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


#define imj_da_push(arr, item, arena) do { \
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

static void __imj_skip_whitespace(imj_t *imj);

bool imj_from_file(const char *filepath, imj_t *imj, imj_io_mode_t mode) {
    const char *open_mode = mode == IMJ_READ ? "r" : "w";
    FILE *file = fopen(filepath, open_mode);
    
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
    
    imj->filepath = filepath;
    imj->src.data = buffer;
    imj->src.length = size;
    imj->current = (char*)imj->src.data;
    imj->log_errors = true;

    __imj_skip_whitespace(imj);
    if (*imj->current != '\0') {
        imj->value_pending = true;
    }
    
    return true;
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
static bool imj_sv_cstr_eq(imj_sv_t sv, const char *cstr) {
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

static char __imj_next(imj_t *imj) {
    if (*imj->current == '\0') return '\0';

    char c = *imj->current;
    ++imj->current;
    return c;
}

static bool __imj_consume(imj_t *imj, char c) {
    char check = __imj_next(imj);
    if (check == c) {
        return true;
    }

    return false;
}

static void __imj_parse_error(imj_t *imj, const char *message) {
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

static bool __imj_is_whitespace(char c) {
    switch (c) {
    case ' ': case '\n':
    case '\r': case '\t': return true;
    default: return false;
    }
}

static void __imj_skip_whitespace(imj_t *imj) {
    while (__imj_is_whitespace(*imj->current)) ++imj->current;
}

static bool imj_peek(imj_t *imj, char c) {
    return *imj->current == c;
}

static bool __imj_match(imj_t *imj, char c) {
    if (imj_peek(imj, c)) {
        __imj_next(imj);
        return true;
    }

    return false;
}
static void __imj_pop_lvl(imj_t *imj) {
    imj->lvl = imj->lvl->prev;
}

static bool __imj_read_str(imj_t *imj, imj_sv_t *ret) {
    char *start = imj->current;
    char *previous = imj->current++;
    while (true) {
        if (*previous == '\"') break;
        if (*previous == '\0') {
            __imj_parse_error(imj, "found end of file before end of string.");
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
                        __imj_parse_error(imj, "invalid hex digit.");
                        return false;
                    }
                    }

                    ++imj->current;
                }

                --imj->current;
                break;
            }

            default: {
                __imj_parse_error(imj, "invalid escape sequence.");
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

static void __imj_skip_value(imj_t *imj);

static void __imj_skip_str(imj_t *imj) {
    char *previous = imj->current++;
    while (*previous != '\"') {
        if (*previous == '\0') {
            __imj_parse_error(imj, "source ended before string closed");
            return;
        }

        if (*previous == '\\' && *imj->current == '\"') {
            ++imj->current;
        }

        previous = imj->current++;
    }
}

static void __imj_skip_obj(imj_t *imj) {
    while (true) {
        __imj_skip_whitespace(imj);
        if (__imj_match(imj, '}')) return;
        if (*imj->current == '\0') {
            __imj_parse_error(imj, "expected '}' before end of file");
            return;
        }

        if (*imj->current == '\"') {
            ++imj->current;
            __imj_skip_str(imj);
        } else {
            __imj_parse_error(imj, "expected key");
            return;
        }
        
        __imj_skip_whitespace(imj);
        if (!__imj_consume(imj, ':')) {
            __imj_parse_error(imj, "expected ':' after key");
            return;
        }

        __imj_skip_whitespace(imj);
        __imj_skip_value(imj);
        __imj_skip_whitespace(imj);

        if (__imj_match(imj, ',')) {
            __imj_skip_whitespace(imj);
            if (*imj->current == '}') {
                __imj_parse_error(imj, "cannot have ',' before ending an object");
                return;
            }
        }
    }
}

static void __imj_skip_arr(imj_t *imj) {
    while (true) {
        __imj_skip_whitespace(imj);
        if (__imj_match(imj, ']')) return;

        if (*imj->current == '\0') {
            __imj_parse_error(imj, "expected ']' before end of file");
            return;
        }

        __imj_skip_value(imj);

        __imj_skip_whitespace(imj);

        if (__imj_match(imj, ',')) {
            __imj_skip_whitespace(imj);
            if (*imj->current == ']') {
                __imj_parse_error(imj, "cannot have ',' before ending an array");
                return;
            }
        }
    }
}

static void __imj_skip_until_whitespace_or_comma(imj_t *imj) {
    while (true) {
        switch (*imj->current) {
        case '\0': case ',': case ']': case '}': return;
        default: break;
        }

        if (__imj_is_whitespace(*imj->current)) return;
        ++imj->current;
    }
}

static void __imj_skip_value(imj_t *imj) {
    switch (*imj->current) {
    case '"': {
        ++imj->current;
        __imj_skip_str(imj);
        break;
    }

    case '{': {
        ++imj->current;
        __imj_skip_obj(imj);
        break;
    }

    case '[': {
        ++imj->current;
        __imj_skip_arr(imj);
        break;
    }

    default: __imj_skip_until_whitespace_or_comma(imj); break;
    }
}

static void __imj_dive_into_arr(imj_t *imj, bool use_left_off) {
    imj_lvl_t *arr = __imj_arena_alloc(&imj->arena, sizeof(imj_lvl_t));
    *arr = (imj_lvl_t){0};
    arr->type = IMJ_ARRAY;
    arr->prev = imj->lvl;
    arr->left_off_or_null = use_left_off ? imj->current : NULL;
    imj->lvl = arr;
}

static void __imj_update_array_if_necessary(imj_t *imj) {
    if (imj->lvl && imj->lvl->type == IMJ_ARRAY) {
        ++imj->lvl->count;

        __imj_skip_whitespace(imj);
        if (__imj_match(imj, ',')) {
            imj->value_pending = true;
            __imj_skip_whitespace(imj);
        } else {
            imj->value_pending = false;
        }
    }
}

void imj_begin_arr(imj_t *imj) {
    if (imj->had_error) return;

    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(!imj->lvl || imj->lvl->type == IMJ_KEY_VALUE || imj->lvl->type == IMJ_ARRAY, "cannot begin array directly inside object");

    __imj_skip_whitespace(imj);

    imj->value_pending = true;
    bool entered = false;
    if (!__imj_match(imj, '[')) {
        imj->value_pending = false;
    } else {
        __imj_skip_whitespace(imj);
        imj->value_pending = *imj->current != ']';
        entered = true;
    }

    __imj_dive_into_arr(imj, entered);
}

void imj_begin_arr_ex(imj_t *imj, size_t *count) {
    if (imj->had_error) return;

    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(!imj->lvl || imj->lvl->type == IMJ_KEY_VALUE || imj->lvl->type == IMJ_ARRAY, "cannot begin array directly inside object");

    __imj_skip_whitespace(imj);

    bool value_pending = true;
    bool entered = false;
    if (!__imj_match(imj, '[')) {
        *count = 0;
        value_pending = false;
    } else {
        __imj_skip_whitespace(imj);

        char *start = imj->current;

        entered = true;
        size_t seeked_count = 0;

        while (true) {
            __imj_skip_whitespace(imj);

            if (*imj->current == '\0') {
                __imj_parse_error(imj, "expected ']' before end of file");
                *count = 0;
                break;
            }

            if (*imj->current == ']') {
                break;
            }

            __imj_skip_value(imj);
            __imj_skip_whitespace(imj);
            ++seeked_count;

            if (__imj_match(imj, ',')) {
                __imj_skip_whitespace(imj);

                if (*imj->current == ']') {
                    __imj_parse_error(imj, "cannot use comma before closing array");
                    return;
                }
            } else {
                if (*imj->current != ']') {
                    __imj_parse_error(imj, "expected ']' to close array");
                    return;
                }

                __imj_skip_whitespace(imj);
            }
        }

        *count = seeked_count;

        __imj_skip_whitespace(imj);
        imj->value_pending = value_pending && __imj_match(imj, ']');
        imj->current = start;
    }

    __imj_dive_into_arr(imj, entered);
}

void imj_end_arr(imj_t *imj) {
    if (imj->had_error) return;

    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl && imj->lvl->type == IMJ_ARRAY, "ending array not inside of array");

    if (imj->lvl->left_off_or_null) {
        __imj_skip_whitespace(imj);
        __imj_skip_arr(imj);
    }

    __imj_pop_lvl(imj);

    if (imj->lvl) {
        if (imj->lvl->type == IMJ_KEY_VALUE) {
            __imj_pop_lvl(imj);
        }

        __imj_update_array_if_necessary(imj);
    }

    if (imj->lvl == NULL) {
        imj->done = true;
    }
}

void imj_begin_obj(imj_t *imj) {
    if (imj->had_error) {
        return;
    }

    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(!imj->lvl || imj->lvl->type == IMJ_KEY_VALUE || imj->lvl->type == IMJ_ARRAY, "cannot start object directly inside object");

    imj_lvl_t *obj = __imj_arena_alloc(&imj->arena, sizeof(imj_lvl_t));
    *obj = (imj_lvl_t){0};
    obj->type = IMJ_OBJECT;
    obj->prev = imj->lvl;
    imj->lvl = obj;

    bool incorrect_pending_value = imj->value_pending && *imj->current != '{';
    bool is_at_root = imj->lvl->prev == NULL;
    if (incorrect_pending_value || (!is_at_root && !imj->value_pending)) {
        obj->left_off_or_null = NULL;
        return;
    }

    __imj_skip_whitespace(imj);


    if (!__imj_consume(imj, '{')) {
        __imj_parse_error(imj, "expected '{' for object");
    }

    __imj_skip_whitespace(imj);

    obj->left_off_or_null = imj->current;
}

void imj_end_obj(imj_t *imj) {
    if (imj->had_error) return;

    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl && imj->lvl->type == IMJ_OBJECT, "cannot end object without being inside an object");

    if (imj->lvl->left_off_or_null) {
        imj->current = imj->lvl->left_off_or_null;
        __imj_skip_obj(imj);
    }

    __imj_pop_lvl(imj);

    if (imj->lvl) {
        if (imj->lvl->type == IMJ_KEY_VALUE) {
            __imj_pop_lvl(imj);
        }

        __imj_update_array_if_necessary(imj);
    }

    if (imj->lvl == NULL) {
        imj->done = true;
    }
}

static void __imj_dive_into_key(imj_t *imj, bool value_pending) {
    imj_lvl_t *lvl = __imj_arena_alloc(&imj->arena, sizeof(imj_lvl_t));
    *lvl = (imj_lvl_t){0};
    lvl->type = IMJ_KEY_VALUE;
    lvl->prev = imj->lvl;
    imj->lvl = lvl;
    imj->value_pending = value_pending;
}

bool imj_key(imj_t *imj, const char *key) {

    imj->value_pending = false;

    if (imj->had_error) return false;

    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl && imj->lvl->type == IMJ_OBJECT, "keys can only reside inside objects");

    imj_lvl_t *obj = imj->lvl;

    if (imj->lvl->left_off_or_null == NULL) {
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
            __imj_parse_error(imj, "object needs '}' to close");
            return false;
        }

        if (__imj_match(imj, '\"')) {
            imj_sv_t key_name;
            if (!__imj_read_str(imj, &key_name)) return false;

            __imj_skip_whitespace(imj);

            if (!__imj_consume(imj, ':')) {
                __imj_parse_error(imj, "expected ':' after key");
                return false;
            }

            __imj_skip_whitespace(imj);

            imj_key_t imj_key = {
                .name = key_name,
                .loc = imj->current,
            };

            imj_da_push(&obj->keys, imj_key, &imj->arena);

            if (imj_sv_cstr_eq(key_name, key)) {
                __imj_dive_into_key(imj, true);
                return true;
            }


            __imj_skip_value(imj);

            __imj_skip_whitespace(imj);

            if (__imj_match(imj, ',')) {
                __imj_skip_whitespace(imj);
                if (imj_peek(imj, '}')) {
                    __imj_parse_error(imj, "cannot end object with ','");
                    return false;
                }
            }

            obj->left_off_or_null = imj->current;
        } else {
            // no value consumed, we haven't found the key
            // but user expects value to be pending.
            if (__imj_consume(imj, '}')) {
                __imj_dive_into_key(imj, false);
                return false;
            }

            __imj_parse_error(imj, "unexpected character.");
            return false;
        }
    }
}

static bool __imj_is_digit(char c) {
    switch (c) {
    case '0': case __imj_cases_non_zero: return true;
    }
    return false;
}

static bool __imj_read_num(imj_t *imj, imj_val_t *ret) {
    *ret = __imj_val_error;
    bool is_negative = __imj_match(imj, '-');

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
        while (__imj_is_digit(*imj->current)) {
            size_t digit = *imj->current - '0';
            nat *= 10;
            nat += digit;
            ++imj->current;
        }
        break;
    }

    default: {
        __imj_parse_error(imj, "expected digit");
        return false;
    }
    }

    if (__imj_match(imj, '.')) {
        if (!__imj_is_digit(*imj->current)) {
            __imj_parse_error(imj, "expected digit");
            return false;
        }

        while (__imj_is_digit(*imj->current)) {
            double d = (double)(*imj->current - '0');
            dec += d/mag;
            mag *= 10;

            ++imj->current;
        }
    }

    if (__imj_match(imj, 'e') || __imj_match(imj, 'E')) {
        has_exp = true;

        if (__imj_match(imj, '-')) {
            exp_neg = true;
        } else __imj_match(imj, '+');

        if (!__imj_is_digit(*imj->current)) {
            __imj_parse_error(imj, "expected digit");
            return false;
        }

        while (__imj_is_digit(*imj->current)) {
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

static bool __imj_read_val(imj_t *imj, imj_val_t *ret) {
    switch (*imj->current) {
    case '{': {
        ++imj->current;
        __imj_skip_obj(imj);
        ret->kind = IMJ_OBJECT;
        break;
    }

    case '[': {
        ++imj->current;
        __imj_skip_arr(imj);
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
        if (!__imj_read_str(imj, &sv)) return false;
        *ret = (imj_val_t){0};
        ret->kind = IMJ_STRING;
        ret->sv = sv;
        break;
    }

    default: {
        char *start = imj->current;
        __imj_skip_until_whitespace_or_comma(imj);
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
            __imj_parse_error(imj, "unexpected value");
            return false;
        }
        break;
    }
    }

    return true;
}

static bool __imj_use_default_value_and_pop_lvl_if_possible(imj_t *imj) {
    bool value_pending = imj->value_pending;
    imj->value_pending = false;

    if (imj->lvl) {
        switch (imj->lvl->type) {
        case IMJ_OBJECT: {
            __imj_parse_error(imj, "cannot have objects directly inside objects");
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

bool imj_valnull(imj_t *imj) {
    if (imj->had_error) {
        return false;
    }

    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl == NULL || imj->lvl->type == IMJ_KEY_VALUE || imj->lvl->type == IMJ_ARRAY, "cannot put values directly inside objects");


    if (__imj_use_default_value_and_pop_lvl_if_possible(imj)) {
        return false;
    }

    imj_val_t val;
    bool success = __imj_read_val(imj, &val);

    __imj_update_array_if_necessary(imj);

    if (imj->lvl == NULL) {
        imj->done = true;
    }

    return success && val.kind == IMJ_NULL;
}

void imj_valb(imj_t *imj, bool *value, bool default_) {
    if (imj->had_error) {
        *value = default_;
        return;
    }

    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl == NULL || imj->lvl->type == IMJ_KEY_VALUE || imj->lvl->type == IMJ_ARRAY, "cannot put values directly inside objects");


    if (__imj_use_default_value_and_pop_lvl_if_possible(imj)) {
        *value = default_;
        return;
    }

    imj_val_t val;
    bool success = __imj_read_val(imj, &val);
    if (success && val.kind == IMJ_BOOL) {
        *value = (bool)val.i;
    } else {
        *value = default_;
    }

    __imj_update_array_if_necessary(imj);

    if (imj->lvl == NULL) {
        imj->done = true;
    }
}

void imj_vali(imj_t *imj, int *value, int default_) {
    if (imj->had_error) {
        *value = default_;
        return;
    }

    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl == NULL || imj->lvl->type == IMJ_KEY_VALUE || imj->lvl->type == IMJ_ARRAY, "cannot put values directly inside objects");


    if (__imj_use_default_value_and_pop_lvl_if_possible(imj)) {
        *value = default_;
        return;
    }

    imj_val_t val;
    bool success = __imj_read_val(imj, &val);
    if (success && val.kind == IMJ_NUMBER) {
        *value = (int)val.i;
    } else {
        *value = default_;
    }

    __imj_update_array_if_necessary(imj);

    if (imj->lvl == NULL) {
        imj->done = true;
    }
}

void imj_vals(imj_t *imj, size_t *value, size_t default_) {
    if (imj->had_error) {
        *value = default_;
        return;
    }

    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl == NULL || imj->lvl->type == IMJ_KEY_VALUE || imj->lvl->type == IMJ_ARRAY, "cannot put values directly inside objects");


    if (__imj_use_default_value_and_pop_lvl_if_possible(imj)) {
        *value = default_;
        return;
    }

    imj_val_t val;
    bool success = __imj_read_val(imj, &val);
    if (success && val.kind == IMJ_NUMBER) {
        *value = (size_t)val.s;
    } else {
        *value = (size_t)default_;
    }

    __imj_update_array_if_necessary(imj);

    if (imj->lvl == NULL) {
        imj->done = true;
    }
}

void imj_valf(imj_t *imj, float *value, float default_) {
    if (imj->had_error) {
        *value = default_;
        return;
    }

    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl == NULL || imj->lvl->type == IMJ_KEY_VALUE || imj->lvl->type == IMJ_ARRAY, "cannot put values directly inside objects");


    if (__imj_use_default_value_and_pop_lvl_if_possible(imj)) {
        *value = default_;
        return;
    }

    imj_val_t val;
    bool success = __imj_read_val(imj, &val);
    if (success && val.kind == IMJ_NUMBER) {
        *value = (float)val.d;
    } else {
        *value = (float)default_;
    }

    __imj_update_array_if_necessary(imj);

    if (imj->lvl == NULL) {
        imj->done = true;
    }
}

void imj_vald(imj_t *imj, double *value, double default_) {
    if (imj->had_error) {
        *value = default_;
        return;
    }

    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl == NULL || imj->lvl->type == IMJ_KEY_VALUE || imj->lvl->type == IMJ_ARRAY, "cannot put values directly inside objects");

    if (__imj_use_default_value_and_pop_lvl_if_possible(imj)) {
        *value = default_;
        return;
    }

    imj_val_t val;
    bool success = __imj_read_val(imj, &val);
    if (success) {
        *value = val.d;
    } else {
        *value = default_;
    }

    __imj_update_array_if_necessary(imj);

    if (imj->lvl == NULL) {
        imj->done = true;
    }
}

void imj_valsv(imj_t *imj, imj_sv_t *value, const char *default_) {
    if (imj->had_error) {
        *value = imj_cstr2sv(default_);
        return;
    }

    __imj_assert(!imj->done, "already finished processing");
    __imj_assert(imj->lvl == NULL || imj->lvl->type == IMJ_KEY_VALUE || imj->lvl->type == IMJ_ARRAY, "cannot put values directly inside objects");

    if (__imj_use_default_value_and_pop_lvl_if_possible(imj)) {
        *value = imj_cstr2sv(default_);
        return;
    }

    imj_val_t val;
    bool success = __imj_read_val(imj, &val);
    if (success && val.kind == IMJ_STRING) {
        *value = val.sv;
    } else {
        *value = imj_cstr2sv(default_);
    }

    __imj_update_array_if_necessary(imj);

    if (imj->lvl == NULL) {
        imj->done = true;
    }
}

void imj_valcstr(imj_t *imj, const char **value, const char *default_, imj_alloc alloc, void *allocator) {
    imj_sv_t sv;
    imj_valsv(imj, &sv, default_);

    char *new_val = alloc(allocator, sv.length+1);
    memcpy(new_val, sv.data, sv.length);
    new_val[sv.length] = '\0';

    *value = new_val;
}

#endif
