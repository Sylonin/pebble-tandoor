#pragma once

#include <pebble.h>

// Opens recipe `id` (0: a random one). With `resume_step` >= 0 it goes on to cooking at that step
// once loaded.
void recipe_win_push(uint32_t id, const char *name_hint, int resume_step);
// The recipe in the model changed: more of it arrived, it finished or it failed.
void recipe_win_on_update(void);
// Cooking at `step` of recipe `id`: straight away when that recipe is loaded, else after loading it.
void recipe_win_open_step(uint32_t id, const char *name, uint8_t step);
