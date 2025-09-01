# imj
imj is a simple, small and portable immediate mode library for reading and writing JSON files.

Here's a sample:
```c
// full code in example.c

void player_io(player_t *player, const char *filepath, imj_io_mode_t io_mode) {
    imj_t imj = {0};
    bool success = imj_file(filepath, &imj, io_mode);
    if (!success) abort();

    imj.render_style = IMJ_STYLE_PRETTY;

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
```

Here's the output into `gen_example.json`.
```c
{
  "level": 42,
  "health": 95,
  "weapons": [ { "id": 0, "acquired": false }, { "id": 1, "acquired": true }, { "id": 2, "acquired": true } ],
  "inventory": [0,1,2,3,4,5,6,7,8,9,10,11,12]
}
```

The same code (in the `player_io` function) is used for reading and writing.

In read mode, the json string is parsed on the fly. Keys are queried on the fly, so reading object key values is still order agnostic.

In write mode, the json string is seralized on the fly instead. Since this is an immediate mode API, you can change the render style at any point
during the writing. Play around with changing `render_style` in different places to see how it affects the output JSON.

All _primitive_ value functions are prefixed with `imj_val*`. The second parameter is a pointer to the value you want to read to or to write from depending on the processing mode.
The third paramter is a default in read mode when the value is not found. In write mode, and if the second parameter is `NULL`, then this argument is used instead of
the second for serialization.

## Usage
imj is a header-only library, so all you need to do is copy/paste the `imj.h` file into your source directory and define `IMJ_IMPLEMENTATION` before including `imj.h`
in one of your source files to paste the implementation. You only do this in _one_ of the source files, then include `imj.h` regularly everywhere else.

In a single source file:
```c
#define IMJ_IMPLEMENTATION
#include "imj.h"
```

Everywhere else:
```c
#include "imj.h"
```

## Building the Example and Tests
I'm using [Tsoding](https://x.com/tsoding)'s [nobuild](https://github.com/tsoding/nob.h) to build the example and tester.

First compile the build system
```
gcc nob.c -o nob
```

And then simply run `nob`
```
./nob
```

## Future
- support for unicode code points
- skipping arbitrary number of array values
- options for nonstandard JSON
  - allow for comments
  - allow trailing commas
  - allow byte data
- robustness (more tests)

## Limitations
imj is made primarily for reading _and_ writing. As such, it's not very convenient for inspecting arbitrary JSON strings. For that, you are better off using
any of the [gazillion](https://www.json.org/json-en.html#:~:text=bmx%2Drjson-,C,-mu_json) retain mode JSON parsers out there.

## Why yet another JSON parser 
The API is an extension of an idea I saw in a [tweet](https://x.com/TylerGlaiel/status/1812974709052744158) by [Tyler Glaiel](https://x.com/TylerGlaiel)
showing off his pattern for a save/load system.

A common pattern in saving/loading system is having separate functions for reading and writing the data.
```c
void save(player_t *player, int version) {
    file_t *save_file = file_open("game_save.txt", "w");
    file_writei(save_file, version);
    file_write_cstr(save_file, player->name);
    file_writei(save_file, player->health);
    file_close(save_file);
}

void load(player_t *player) {
    file_t *load_file = file_open("game_save.txt", "r");

    int version;
    file_readi(load_file, &version, 0);

    file_read_cstr(load_file, &player->name, "");
    file_readi(load_file, &player->health, 10);
    file_close(load_file);
}
```
I do not like this pattern as it means writing almost the exact same code twice - once for reading and once for writing. The main issue with this is needing to keep them both in sync whenever the data model changes.

```c
void save(player_t *player, int version) {
    file_t *save_file = file_open("game_save.txt", "w");
    file_writei(save_file, version);
    file_write_cstr(save_file, player->name);

    file_writed(save_file, player->mana); // new data to load

    file_writei(save_file, player->health);
    file_close(save_file);
}

void load(player_t *player) {
    file_t *load_file = file_open("game_save.txt", "r");
    int version;
    file_readi(load_file, &version, 0);

    file_read_cstr(load_file, &player->name, "");

    // same thing in load and allow for backwards compatibility
    if (version > 1) file_readd(load_file, &player->mana, 46.3);
    
    file_readi(load_file, &player->health, 10);
    file_close(load_file);
}
```

Most JSON libraries only offer this pattern through their retained mode API if they support writing _and_ reading. If you want to write a JSON file, first you massage your data into the library's JSON heirarchy struct/class, and then pass that into its seralizer to actually write it.
```c
void save(player_t *player, int version) {
    json_val_t name = json_val_cstr(player->name);
    json_val_t health = json_vali(player->health);

    json_val_t *obj = json_val_obj();
    json_obj_put(obj, "name", name);
    json_obj_put(obj, "mana", json_vald(player->mana));
    json_obj_put(obj, "health", health);

    const char *json_cstr = json_render(obj);

    file_t *save_file = file_open("game_save.txt", "w");
    file_write_cstr(save_file, json_cstr);
    file_close(save_file);
}
```

If you want to read a file, first you read the JSON string into the library's JSON heirarchy struct/class, and then massage that data back into your actual model.
```c
void load(player_t *player) {
    file_t *load_file = file_open("game_save.txt", "r");
    json_val_t *obj = json_parse_file(load_file);
    file_close(load_file);

    player->name = json_obj_value_cstr(obj, "name", "");
    player->health = json_obj_valuei(obj, "health", 10);
    player->mama = json_obj_valued(obj, "mana", 0.0);
}
```

This adds a lot of "moving data" code that needs to be synced constantly as the data you want to save/load increases or changes.

See [cJSON](https://github.com/DaveGamble/cJSON?tab=readme-ov-file#printing) for a real example of this in a popular JSON library.

This small library aims to solve this syncing issue by using an immediate mode API that uses an internal variable to decide between reading _and_ writing, this allows you to use the same code for both. Thus, the reading and writing code will always be in sync because it's the same code.
