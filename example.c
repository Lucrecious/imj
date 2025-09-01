#define IMJ_IMPLEMENTATION
#include "imj.h"

#include <string.h>

#define MAX_INVENTORY_SPACE 100

typedef int item_id_t;
enum item_id_t {
    ITEM_NONE = 0,
    ITEM_ARROW,
    ITEM_FOOD,
};

typedef int weapon_id_t;
enum weapon_id_t {
    WEAPON_SWORD = 0,
    WEAPON_BOW,
    WEAPON_SHIELD,

    WEAPON_COUNT,
};

typedef struct weapon_t weapon_t;
struct weapon_t {
    weapon_id_t id;
    bool acquired;
};

typedef struct player_t player_t;
struct player_t {
    int level;
    double health;
    weapon_t weapons[WEAPON_COUNT];

    size_t inventory_count;
    int inventory[MAX_INVENTORY_SPACE];
};

static player_t player_default(void) {
    player_t player;
    player.health = 100;
    player.level = 10;

    for (int i = 0; i < WEAPON_COUNT; ++i) {
        player.weapons[i].id = i;
        player.weapons[i].acquired = false;
    }

    for (int i = 0; i < 13; ++i) {
        player.inventory[i] = i;
    }

    player.inventory_count = 13;

    return player;
}

void player_io(player_t *player, const char *filepath, imj_io_mode_t io_mode) {
    imj_t imj = {0};
    bool success = imj_file(filepath, &imj, io_mode);
    if (!success) abort();

    if (io_mode == IMJ_WRITE) {
        imj.render_style = IMJ_STYLE_PRETTY;
    }

    imj_begin_obj(&imj);
        imj_key(&imj, "level");
        imj_vali(&imj, &player->level, 0);

        imj_key(&imj, "health");
        imj_vald(&imj, &player->health, 0);

        imj_key(&imj, "weapons");

        imj.render_style = IMJ_STYLE_SINGLE_LINE;

        imj_begin_arr(&imj);
            for (size_t i = 0; i < WEAPON_COUNT; ++i) {
                weapon_t *w = &player->weapons[i];

                imj_begin_obj(&imj);
                    imj_key(&imj, "id");
                    imj_vali(&imj, &w->id, -1);

                    imj_key(&imj, "acquired");
                    imj_valb(&imj, &w->acquired, false);
                imj_end_obj(&imj);
            }
        imj_end_arr(&imj);

        imj.render_style = IMJ_STYLE_PRETTY;

        imj_key(&imj, "inventory");

        imj.render_style = IMJ_STYLE_MIN;

        imj_begin_arr_ex(&imj, &player->inventory_count);

        for (size_t i = 0; i < player->inventory_count; ++i) {
            imj_vali(&imj, &player->inventory[i], ITEM_NONE);
        }

        imj_end_arr(&imj);
        imj.render_style = IMJ_STYLE_PRETTY;

    imj_end_obj(&imj);

    if (imj.io_mode == IMJ_WRITE) {
        imjw_flush(&imj);
        printf("%.*s\n", (int)imj.sb.count, imj.sb.items);
    }
}

int main() {

    player_t player = player_default();

    player_io(&player, "example.json", IMJ_READ);
    
    player.health -= 5;
    player.level = 42;
    player.weapons[0].acquired = false;

    player_io(&player, "gen_example.json", IMJ_WRITE);
}