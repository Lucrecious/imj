#define IMJ_IMPLEMENTATION
#include "imj.h"

#include <string.h>

#define ITEM_NAME_CAPACITY 32

typedef int item_id_t;
enum item_id_t {
    ITEM_ID_SWORD = 0,
    ITEM_ID_BOW,
    ITEM_ID_SHIELD,

    ITEM_ID_COUNT,
};

typedef struct item_t item_t;
struct item_t {
    item_id_t id;
    char name[ITEM_NAME_CAPACITY];
    int acquired;
};

typedef struct player_t player_t;
struct player_t {
    int level;
    double health;
    item_t items[ITEM_ID_COUNT];
};

static player_t player_default(void) {
    player_t player;
    player.health = 100;
    player.level = 10;

    for (int i = 0; i < ITEM_ID_COUNT; ++i) {
        player.items[i].id = i;
        strncpy(player.items[i].name, "", ITEM_NAME_CAPACITY);
        player.items[i].acquired = 0;

    }

    return player;
}

void player_io(player_t *player, const char *filepath, imj_io_mode_t io_mode) {
    imj_t imj = {0};
    bool success = imj_from_file(filepath, &imj, io_mode);
    if (!success) abort();

    player_t dplayer = player_default();

    imj_begin_obj(&imj);
        imj_key(&imj, "level");
        imj_vali(&imj, &player->level, dplayer.level);

        imj_key(&imj, "health");
        imj_vald(&imj, &player->health, dplayer.health);

    imj_end_obj(&imj);
}

int main() {

    player_t player = player_default();

    player_io(&player, "example.json", IMJ_READ);
}