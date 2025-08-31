# ImJSON - an immediate mode JSON reader and writer
ImJSON is a simple, small and portable immediate mode library for reading and writing JSON files.

Here's a sample:
```c
void player_io(player_t *player, const char *filepath, imj_io_mode_t io_mode) {
    imj_t imj = {0};
    bool success = imj_from_file(filepath, &imj, io_mode);
    if (!success) abort();

    imj_begin_obj(&imj);
        imj_key(&imj, "level");
        imj_vali(&imj, &player->level, 0);

        imj_key(&imj, "health");
        imj_vald(&imj, &player->health, 0);

        imj_key(&imj, "weapons");
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

        imj_key(&imj, "inventory");
        imj_begin_arr_ex(&imj, &player->inventory_count);

        for (size_t i = 0; i < player->inventory_count; ++i) {
            imj_vali(&imj, &player->inventory[i], ITEM_NONE);
        }

        imj_end_arr(&imj);

    imj_end_obj(&imj);
}

int main() {
    player_t player = player_default();

    player_io(&player, "example.json", IMJ_READ);
    
    player.health -= 5;

    player_io(&player, "example.json", IMJ_WRITE);
}
```
The same code (in the `player_io` function) is used for reading and writing.

In read mode, the json string is parsed on the fly. Keys are looked up by skipping the elements in between, so object key values are still order agnostic.

In write mode, the json string is seralized on the fly instead.

All _primitive_ value functions are prefixed with `imj_val*`. The second parameter is a pointer to the value you want to read to or to write from depending on the processing mode.
The third paramter is a default in read mode when the value is not found. In write mode, and if the second parmeter is `NULL`, then this argument is used instead of
the second for serialization.

Here are the primitive value functions:
```c
bool imj_valnull(imj_t *imj);
void imj_valb(imj_t *imj, bool *value, bool default_);
void imj_vali(imj_t *imj, int *value, int default_);
void imj_vals(imj_t *imj, size_t *value, size_t default_);
void imj_valf(imj_t *imj, float *value, float default_);
void imj_vald(imj_t *imj, double *value, double default_);
void imj_valcstr(imj_t *imj, const char **value, const char *default_, imj_alloc alloc, void *allocator);
void imj_valsv(imj_t *imj, imj_sv_t *value, const char *default_);
```

## Why
The API is an extension of an idea I saw in a [tweet](https://x.com/TylerGlaiel/status/1812974709052744158) by [Tyler Glaiel](https://x.com/TylerGlaiel)
showing off his pattern for a save/load system.

A common pattern in saving/loading system is having separate functions for reading and writing the data. I do not like this pattern as it means
writing almost the exact same code twice - once for reading and once for writing. The main issue with this is needing to update both whenever
the data changes.

Most JSON libraries require this pattern. If you want to write a JSON file, first you massage your data into the library's JSON heirarchy struct/class, and
then pass that into its seralizer to actually write it. If you want to read a file, first you read the JSON string into the library's JSON heirarchy struct/class,
and then massage that data back into your actual model. This adds a lot of "moving data" code that needs to be synced constantly as the data you want to save/load
increases or changes. If you decide to change the data structure, you need to remember to update that for both the reading and writing code, for example.

This small library aims to solve this issue of data "massaging" by using an immediate pattern rather than a retained one, allowing you to use the same 
code for reading _and_ writing.

## Future
- support for utf-8
- skipping arbitrary number of array values
- options for nonstandard JSON
  - allow for comments
  - allow trailing commas
  - allow byte data
- robustness (more tests)

## Limitations
As mentioned, ImJSON was made primarily for reading _and_ writing. As such, it's not very convenient to insepect arbitrary JSON strings. For that, you are better off using
any of the gazillions retain mode JSON parsers out there.


