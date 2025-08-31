#define NOB_IMPLEMENTATION
#include "nob.h"
#undef NOB_IMPLEMENTATION

#define IMJ_IMPLEMENTATION
#include "imj.h"

#ifdef _WIN32
#define FS "\\"
#else
#define FS "/"
#endif

#define PLAYER_MAX_ITEM_COUNT 3

int asprintf(char **strp, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    
    if (len < 0) return -1;
    
    *strp = malloc(len + 1);
    if (!*strp) return -1;
    
    va_start(args, fmt);
    vsnprintf(*strp, len + 1, fmt, args);
    va_end(args);
    
    return len;
}

typedef int item_type_t;
enum item_type_t {
    ITEM_BOW,
    ITEM_SWORD,
    ITEM_FISHING_LINE,
    ITEM_COUNT,
};

typedef struct item_t item_t;
struct item_t {
    item_type_t type;
    bool ranged;
    float damage;
};

typedef struct player_t player_t;
struct player_t {
    float health;
    item_t items[PLAYER_MAX_ITEM_COUNT];
};


typedef struct game_t game_t;
struct game_t {
    const char *name;
    int level;
    float spawn_probability;
    double delta_sec;
    size_t enemy_count;
    bool is_debug;

    player_t player;
};

static game_t dgame = {0};

void *tester_alloc(void *allocator, size_t size_bytes) {
    NOB_UNUSED(allocator);
    return malloc(size_bytes);
}

void game_io(game_t *game, imj_t *imj) {
    imj_begin_obj(imj);

        imj_key(imj, "name");
        imj_valcstr(imj, &game->name, dgame.name, tester_alloc, NULL);

        imj_key(imj, "level");
        imj_vali(imj, &game->level, dgame.level);

        imj_key(imj, "spawn_probability");
        imj_valf(imj, &game->spawn_probability, dgame.spawn_probability);

        imj_key(imj, "delta_sec");
        imj_vald(imj, &game->delta_sec, dgame.delta_sec);

        imj_key(imj, "enemy_count");
        imj_vals(imj, &game->enemy_count, dgame.enemy_count);

        imj_key(imj, "player");
        imj_begin_obj(imj);
            imj_key(imj, "health");
            imj_valf(imj, &game->player.health, dgame.player.health);

            imj_key(imj, "items");
            
            imj_begin_arr(imj);
            
            for (size_t i = 0; i < PLAYER_MAX_ITEM_COUNT; ++i) {
                item_t *item = &game->player.items[i];
                item_t ditem = dgame.player.items[i];

                imj_begin_obj(imj);

                    imj_key(imj, "type");
                    imj_vali(imj, &item->type, ditem.type);

                    imj_key(imj, "ranged");
                    imj_valb(imj, &item->ranged, ditem.ranged);

                    imj_key(imj, "damage");
                    imj_valf(imj, &item->damage, ditem.damage);

                imj_end_obj(imj);
            }

            imj_end_arr(imj);

        imj_end_obj(imj);

    imj_end_obj(imj);
}

bool dbl_eq(double a, double b, double eps) {
    return fabs(b-a) < eps;
}

bool compare_games(game_t *game, game_t *dgame) {
    bool passed = true;
    if (strcmp(game->name, dgame->name) != 0) {
        passed = false;
        nob_log(NOB_INFO, "expected game.name='%s' but got '%s' instead", dgame->name, game->name);
    }

    if (game->level != dgame->level) {
        passed = false;
        nob_log(NOB_INFO, "expected game.level='%d' but got '%d' instead", dgame->level, game->level);
    }

    if (!dbl_eq(game->delta_sec, dgame->delta_sec, 0.001)) {
        passed = false;
        nob_log(NOB_INFO, "expected game.delta_sec='%g' but got '%g' instead", dgame->delta_sec, game->delta_sec);
    }

    if (!dbl_eq(game->spawn_probability, dgame->spawn_probability, 0.001)) {
        passed = false;
        nob_log(NOB_INFO, "expected game.spawn_probability='%g' but got '%g' instead", dgame->spawn_probability, game->spawn_probability);
    }

    if (game->enemy_count != dgame->enemy_count) {
        passed = false;
        nob_log(NOB_INFO, "expected game.enemy_count='%zu' but got '%zu' instead", dgame->enemy_count, game->enemy_count);
    }

    if (game->is_debug != dgame->is_debug) {
        passed = false;
        nob_log(NOB_INFO, "expected game.is_debug='%d' but got '%d' instead", dgame->is_debug, game->is_debug);
    }

    if (!dbl_eq(game->player.health, dgame->player.health, 0.001)) {
        passed = false;
        nob_log(NOB_INFO, "expected game.player.health='%g' but got '%g' instead", dgame->player.health, game->player.health);
    }

    for (size_t i = 0; i < PLAYER_MAX_ITEM_COUNT; ++i) {
        item_t item = game->player.items[i];
        item_t ditem = dgame->player.items[i];
        if (item.type != ditem.type) {
            passed = false;
            nob_log(NOB_INFO, "expected game.player.item[%zu].health='%d' but got '%d' instead", i, ditem.type, item.type);
        }

        if (item.ranged != ditem.ranged) {
            passed = false;
            nob_log(NOB_INFO, "expected game.player.item[%zu].ranged='%d' but got '%d' instead", i, ditem.ranged, item.ranged);
        }

        if (!dbl_eq(item.damage, ditem.damage, 0.001)) {
            passed = false;
            nob_log(NOB_INFO, "expected game.player.item[%zu].damage='%g' but got '%g' instead", i, ditem.damage, item.damage);
        }
    }

    return passed;
}

bool object_reading_test(const char *path) {
    imj_t imj = {0};
    bool success = imj_from_file(path, &imj, IMJ_READ);
    assert(success);

    bool failed = false;
    float valf;
    double vald;
    size_t vals;
    int vali;
    const char *text;
    bool valb;

    do {
        imj_t imj_ = imj;
        imj_t *imj = &imj_;
        imj_begin_obj(imj);

            imj_key(imj, "arr");
            imj_begin_arr(imj);
                imj_vali(imj, &vali, 0);
                if (vali != 4) {
                    failed = true;
                    break;
                }

                imj_vali(imj, &vali, 0);
                if (vali != 5) {
                    failed = true;
                    break;
                }

                imj_vali(imj, &vali, 0);
                if (vali != 6) {
                    failed = true;
                    break;
                }
            imj_end_arr(imj);

            imj_key(imj, "obj");
            imj_begin_obj(imj);
                imj_key(imj, "arr");
                size_t count;
                imj_begin_arr_ex(imj, &count);
                    if (count != 7) {
                        failed = true;
                        break;
                    }

                    imj_vali(imj, &vali, 0);
                    if (vali != 4) {
                        failed = true;
                        break;
                    }

                    imj_begin_obj(imj);
                        imj_key(imj, "key");
                        imj_sv_t sv;
                        imj_valsv(imj, &sv, "");
                        if (!imj_sv_cstr_eq(sv, "value")) {
                            failed =true;
                            break;
                        }
                    imj_end_obj(imj);

                    imj_valcstr(imj, &text, "", tester_alloc, NULL);
                    if (strcmp(text, "text") != 0) {
                        failed = true;
                        break;
                    }

                    bool is_null = imj_valnull(imj);
                    if (!is_null) {
                        failed = true;
                        break;
                    }

                    imj_valb(imj, &valb, true);
                    if (valb != false) {
                        failed = true;
                        break;
                    }

                    imj_valb(imj, &valb, false);
                    if (valb != true) {
                        failed = true;
                        break;
                    }

                    imj_begin_arr(imj);
                        imj_vali(imj, &vali, 0);
                        if (vali != 1) {
                            failed = true;
                            break;
                        }

                        imj_vali(imj, &vali, 0);
                        if (vali != 2) {
                            failed = true;
                            break;
                        }

                        imj_vali(imj, &vali, 0);
                        if (vali != 3) {
                            failed = true;
                            break;
                        }
                    imj_end_arr(imj);
                imj_end_arr(imj);
            imj_end_obj(imj);

            imj_key(imj, "null");
            is_null = imj_valnull(imj);
            if (!is_null) {
                failed = true;
                break;
            }

            imj_key(imj, "text");
            imj_valcstr(imj, &text, "", tester_alloc, NULL);
            if (strcmp(text, "text") != 0) {
                failed = true;
                break;
            }

            imj_key(imj, "true");
            imj_valb(imj, &valb, 0);
            if (valb != true) {
                failed = true;
                break;
            }

            imj_key(imj, "false");
            imj_valb(imj, &valb, 0);
            if (valb != false) {
                failed = true;
                break;
            }

            imj_key(imj, "i");
            imj_vali(imj, &vali, 0);
            if (vali != -5) {
                failed = true;
                break;
            }

            imj_key(imj, "s");
            imj_vals(imj, &vals, 0);
            if (vals != 3) {
                failed = true;
                break;
            }

            imj_key(imj, "d");
            imj_vald(imj, &vald, 0);
            if (vald != 20) {
                failed = true;
                break;
            }

            imj_key(imj, "f");
            imj_valf(imj, &valf, 0);
            if (valf != 0.1f) {
                failed = true;
                break;
            }
        imj_end_obj(imj);
    } while(false);

    return !failed;
}

bool array_reading_test(const char *path) {
    bool failed = false;

    for (int i = 0; i < 2; ++i) {
        imj_t imj = {0};
        bool success = imj_from_file(path, &imj, IMJ_READ);
        assert(success);

        int num;

        if (i == 0) {
            imj_begin_arr(&imj);
        } else {
            size_t count;
            imj_begin_arr_ex(&imj, &count);
            if (count != 3) {
                failed = true;
            }
        }

        imj_vali(&imj, &num, 0);
        if (num != 1)  {
            failed = true;
        }

        imj_vali(&imj, &num, 0);
        if (num != 2) {
            failed = true;
        }

        imj_vali(&imj, &num, 0);
        if (num != 3) {
            failed = true;
        }

        imj_begin_arr(&imj);
    }

    return !failed;
}

int main() {
    Nob_File_Paths paths = {0};
    if (!nob_read_entire_dir("test", &paths)) {
        return 1;
    }

    dgame = (game_t){
        .name = "imj",
        .level = 42,
        .spawn_probability = 0.3,
        .delta_sec = 0.166666,
        .enemy_count = 169,
        .is_debug = false,
        .player = {
            .health = 79,
            .items = {
                (item_t){
                    .type = ITEM_BOW,
                    .damage = 10,
                    .ranged = true,
                },
                (item_t){
                    .type = ITEM_SWORD,
                    .damage = 34.5,
                    .ranged = false,
                },
                (item_t){
                    .type = ITEM_FISHING_LINE,
                    .damage = -2e5,
                    .ranged = true,
                },
            },
        }
    };

    size_t read_tests_passed = 0;
    size_t read_test_count = 0;
    // read tests
    game_t game = {0};
    for (size_t i = 0; i < paths.count; ++i) {
        const char *filename = paths.items[i];

        // if (strcmp(filename, "ensure_reading.rjson") != 0) continue;

        char *path;
        asprintf(&path, "test"FS"%s", filename);
        Nob_String_View pathsv = nob_sv_from_cstr(path);
        if (nob_sv_end_with(pathsv, ".ejson")) {
            ++read_test_count;

            imj_t imj = {0};
            bool success = imj_from_file(path, &imj, IMJ_READ);
            assert(success);
            imj.log_errors = false;

            game_io(&game, &imj);

            if (!imj.had_error) {
                nob_log(NOB_INFO, "expected error for %s", path);
            } else {
                ++read_tests_passed;
            }
        } else if (nob_sv_end_with(pathsv, ".json")) {
            ++read_test_count;

            imj_t imj = {0};
            bool success = imj_from_file(path, &imj, IMJ_READ);
            assert(success);

            game_io(&game, &imj);

            if (imj.had_error) {
                nob_log(NOB_INFO, "expected no errors for %s", path);
                continue;
            }

            if (compare_games(&game, &dgame)) {
                ++read_tests_passed;
            }
        } else if (nob_sv_end_with(pathsv, ".rjson")) {
            ++read_test_count;

            bool failed = false;
            if (nob_sv_end_with(pathsv, "object.rjson")) {
                if (!object_reading_test(path)) {
                    failed = true;
                } else {
                    ++read_tests_passed;
                }
            } else if (nob_sv_end_with(pathsv, "array.rjson")) {
                if (!array_reading_test(path)) {
                    failed = true;
                } else {
                    ++read_tests_passed;
                }
            } else if (nob_sv_end_with(pathsv, "text.rjson")) {
                imj_t imj = {0};
                bool success = imj_from_file(path, &imj, IMJ_READ);
                assert(success);

                const char *str;
                imj_valcstr(&imj, &str, "", tester_alloc, NULL);

                if (strcmp(str, "hello world") != 0) {
                    failed = true;
                } else {
                    ++read_tests_passed;
                }
            } else if (nob_sv_end_with(pathsv, "null.rjson")) {
                imj_t imj = {0};
                bool success = imj_from_file(path, &imj, IMJ_READ);
                assert(success);

                bool found_null = imj_valnull(&imj);

                if (!found_null) {
                    failed = true;
                } else {
                    ++read_tests_passed;
                }
            } else if (nob_sv_end_with(pathsv, "number.rjson")) {
                imj_t imj = {0};
                bool success = imj_from_file(path, &imj, IMJ_READ);
                assert(success);

                int val;
                imj_vali(&imj, &val, 0);

                if (val != 42069) {
                    failed = true;
                } else {
                    ++read_tests_passed;
                }
            } else if (nob_sv_end_with(pathsv, "bool.rjson")) {
                imj_t imj = {0};
                bool success = imj_from_file(path, &imj, IMJ_READ);
                assert(success);

                bool val;
                imj_valb(&imj, &val, false);

                if (!val) {
                    failed = true;
                } else {
                    ++read_tests_passed;
                }
            }

            if (failed) {
                nob_log(NOB_INFO, "failed reading ensurance test: %.*s", (int)pathsv.count, pathsv.data);
            }
        }
    }

    nob_log(NOB_INFO, "%zu out of %zu read tests passed", read_tests_passed, read_test_count);
}