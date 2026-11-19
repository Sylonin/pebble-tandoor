#pragma once

#include <pebble.h>

// String buffers include the terminator; PKJS cuts each string one byte shorter (docs/design.md, Limits).
#define LIST_TITLE_LEN 64
#define LIST_SUB_LEN 48
#define NAME_LEN 64
#define DESC_LEN 256
#define SERVTEXT_LEN 24
#define KEYWORDS_LEN 64
#define STEP_NAME_LEN 48
#define STEP_TEXT_LEN 1800
#define UNIT_LEN 24
#define FOOD_LEN 48
#define NOTE_LEN 48
#define QUERY_LEN 96
#define TIMER_LABEL_LEN 32

#define MAX_LIST 200
#define MAX_STEPS 40
#define MAX_INGREDIENTS 80

#define NO_STEP 0xFF
#define INGREDIENT_HEADER 1
#define INGREDIENT_NO_AMOUNT 2
#define STEP_TIME_FROM_TEXT 1

typedef struct {
  bool backlight;
  bool sound;
} Settings;

// The recipe being cooked, so Home can offer to resume it.
typedef struct {
  uint32_t recipe_id;
  uint8_t step;
  int32_t servings_x100;
  int32_t saved_at;
  char name[TIMER_LABEL_LEN];
} Session;

typedef struct {
  uint8_t step;
  uint8_t flags;
  int32_t amount;  // ×1000
  char *unit;
  char *unit_plural;
  char *food;
  char *food_plural;
  char *note;
} Ingredient;

typedef struct {
  char *name;
  char *text;
  int32_t seconds;
  uint8_t flags;
  bool received;
} Step;

typedef enum {
  RecipeEmpty,
  RecipeLoading,
  RecipeReady,
  RecipeFailed,
} RecipeState;

typedef struct {
  RecipeState state;
  bool has_head;  // RECIPE arrived: name, servings and counts are known
  uint32_t id;
  char name[NAME_LEN];
  char desc[DESC_LEN];
  char servtext[SERVTEXT_LEN];
  char keywords[KEYWORDS_LEN];
  int32_t servings_x100;  // Tandoor's
  int32_t chosen_x100;    // the user's
  int16_t working;
  int16_t waiting;
  uint8_t rating;
  uint8_t step_count;
  uint8_t ingredient_count;
  uint8_t ingredients_received;
  Step steps[MAX_STEPS];
  Ingredient ingredients[MAX_INGREDIENTS];
  uint8_t checked[(MAX_INGREDIENTS + 7) / 8];
  uint8_t error;
  char error_text[96];
} Recipe;

void model_init(void);
void model_deinit(void);

const Settings *model_settings(void);
void model_set_settings(bool backlight, bool sound);

// Server state from PKJS STATUS.
void model_set_ready(bool ready, const char *host);
bool model_ready(void);
bool model_status_known(void);
const char *model_host(void);

const Session *model_session(void);  // NULL when there is none to resume
void model_session_save(uint8_t step);
void model_session_clear(void);

Recipe *model_recipe(void);
void model_recipe_reset(uint32_t id, const char *name_hint);
void model_recipe_head(uint32_t id, const char *name, const char *desc, int32_t servings_x100, const char *servtext,
                       int16_t working, int16_t waiting, uint8_t rating, uint8_t steps, uint8_t ingredients,
                       const char *keywords);
void model_recipe_ingredients(uint8_t index, const char *data);
void model_recipe_step(uint8_t index, const char *name, const char *text, int32_t seconds, uint8_t flags);
void model_recipe_fail(uint8_t code, const char *text);
void model_recipe_done(void);

bool model_ingredient_checked(uint8_t index);
void model_ingredient_toggle(uint8_t index);
// "1½ cups flour (sifted)", scaled to the chosen servings.
void model_ingredient_text(const Ingredient *ing, char *buf, size_t size);
// Ingredients of one step, in order; returns how many were written to `out`.
uint8_t model_step_ingredients(uint8_t step, uint8_t *out, uint8_t max);
