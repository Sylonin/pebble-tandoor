#include "model.h"

#include <string.h>

#include "ui.h"

#define STORAGE_VERSION 1
#define KEY_META 1
#define KEY_SESSION 2
// The highest key any module uses (timers.c uses 10 to 15).
#define KEY_LAST 15
#define SESSION_MAX_AGE (12 * 60 * 60)

#define FIELD_SEP '\x1f'
#define RECORD_END '\x1e'

typedef struct {
  uint8_t version;
  Settings settings;
} Meta;

_Static_assert(sizeof(Meta) <= PERSIST_DATA_MAX_LENGTH, "Meta fits one persist key");
_Static_assert(sizeof(Session) <= PERSIST_DATA_MAX_LENGTH, "Session fits one persist key");

static Meta s_meta;
static Session s_session;
static Recipe s_recipe;
static bool s_ready;
static bool s_status_known;
static char s_host[48];

// Settings and session ------------------------------------------------------------------------

void model_init(void) {
  s_meta = (Meta){.version = STORAGE_VERSION, .settings = {.backlight = true, .sound = true}};
  Meta stored;
  if (persist_read_data(KEY_META, &stored, sizeof(stored)) == (int)sizeof(stored) &&
      stored.version == STORAGE_VERSION) {
    s_meta = stored;
  } else {
    // First run or an older layout: drop everything else stored (the session and the timers).
    for (uint32_t key = KEY_META + 1; key <= KEY_LAST; key++) {
      persist_delete(key);
    }
    persist_write_data(KEY_META, &s_meta, sizeof(s_meta));
  }
  if (persist_read_data(KEY_SESSION, &s_session, sizeof(s_session)) != (int)sizeof(s_session)) {
    memset(&s_session, 0, sizeof(s_session));
  }
}

static void prv_recipe_free(void) {
  for (uint8_t i = 0; i < MAX_STEPS; i++) {
    free(s_recipe.steps[i].name);
    free(s_recipe.steps[i].text);
  }
  for (uint8_t i = 0; i < MAX_INGREDIENTS; i++) {
    Ingredient *ing = &s_recipe.ingredients[i];
    free(ing->unit);
    free(ing->unit_plural);
    free(ing->food);
    free(ing->food_plural);
    free(ing->note);
  }
  memset(&s_recipe, 0, sizeof(s_recipe));
}

void model_deinit(void) {
  prv_recipe_free();
}

const Settings *model_settings(void) {
  return &s_meta.settings;
}

void model_set_settings(bool backlight, bool sound) {
  if (s_meta.settings.backlight == backlight && s_meta.settings.sound == sound) {
    return;
  }
  s_meta.settings.backlight = backlight;
  s_meta.settings.sound = sound;
  persist_write_data(KEY_META, &s_meta, sizeof(s_meta));
}

void model_set_ready(bool ready, const char *host) {
  s_ready = ready;
  s_status_known = true;
  ui_copy(s_host, sizeof(s_host), host);
}

bool model_ready(void) {
  return s_ready;
}

bool model_status_known(void) {
  return s_status_known;
}

const char *model_host(void) {
  return s_host;
}

const Session *model_session(void) {
  if (s_session.recipe_id == 0 || time(NULL) - s_session.saved_at > SESSION_MAX_AGE) {
    return NULL;
  }
  return &s_session;
}

void model_session_save(uint8_t step) {
  if (s_recipe.state != RecipeReady) {
    return;
  }
  s_session.recipe_id = s_recipe.id;
  s_session.step = step;
  s_session.servings_x100 = s_recipe.chosen_x100;
  s_session.saved_at = (int32_t)time(NULL);
  ui_copy(s_session.name, sizeof(s_session.name), s_recipe.name);
  persist_write_data(KEY_SESSION, &s_session, sizeof(s_session));
}

void model_session_clear(void) {
  memset(&s_session, 0, sizeof(s_session));
  persist_delete(KEY_SESSION);
}

// Recipe --------------------------------------------------------------------------------------

Recipe *model_recipe(void) {
  return &s_recipe;
}

static char *prv_dup(const char *src, size_t max) {
  if (!src || !src[0]) {
    return NULL;
  }
  size_t n = strlen(src);
  if (n >= max) {
    n = max - 1;
  }
  char *out = malloc(n + 1);
  if (out) {
    ui_copy(out, n + 1, src);
  }
  return out;
}

void model_recipe_reset(uint32_t id, const char *name_hint) {
  prv_recipe_free();
  s_recipe.state = RecipeLoading;
  s_recipe.id = id;
  ui_copy(s_recipe.name, sizeof(s_recipe.name), name_hint);
}

void model_recipe_head(uint32_t id, const char *name, const char *desc, int32_t servings_x100, const char *servtext,
                       int16_t working, int16_t waiting, uint8_t rating, uint8_t steps, uint8_t ingredients,
                       const char *keywords) {
  s_recipe.id = id;
  s_recipe.has_head = true;
  ui_copy(s_recipe.name, sizeof(s_recipe.name), name);
  ui_copy(s_recipe.desc, sizeof(s_recipe.desc), desc);
  ui_copy(s_recipe.servtext, sizeof(s_recipe.servtext), servtext);
  ui_copy(s_recipe.keywords, sizeof(s_recipe.keywords), keywords);
  s_recipe.servings_x100 = servings_x100;
  s_recipe.chosen_x100 = servings_x100;
  const Session *session = model_session();
  if (session && session->recipe_id == id && session->servings_x100 > 0) {
    s_recipe.chosen_x100 = session->servings_x100;
  }
  s_recipe.working = working;
  s_recipe.waiting = waiting;
  s_recipe.rating = rating;
  s_recipe.step_count = steps > MAX_STEPS ? MAX_STEPS : steps;
  s_recipe.ingredient_count = ingredients > MAX_INGREDIENTS ? MAX_INGREDIENTS : ingredients;
}

// Cuts `*cursor` at the next separator and returns the field.
static char *prv_field(char **cursor) {
  char *start = *cursor;
  char *p = start;
  while (*p && *p != FIELD_SEP && *p != RECORD_END) {
    p++;
  }
  if (*p == FIELD_SEP) {
    *p = '\0';
    *cursor = p + 1;
  } else {
    // End of the record: leave the cursor on the terminator.
    *cursor = p;
  }
  return start;
}

static int32_t prv_int(const char *s) {
  int32_t sign = 1;
  int32_t v = 0;
  if (*s == '-') {
    sign = -1;
    s++;
  }
  while (*s >= '0' && *s <= '9') {
    v = v * 10 + (*s - '0');
    s++;
  }
  return sign * v;
}

void model_recipe_ingredients(uint8_t index, const char *data) {
  // Parse a copy: fields are cut in place.
  const size_t len = strlen(data);
  char *copy = malloc(len + 1);
  if (!copy) {
    return;
  }
  memcpy(copy, data, len + 1);
  char *cursor = copy;
  while (*cursor && index < s_recipe.ingredient_count) {
    char *record_end = strchr(cursor, RECORD_END);
    if (!record_end) {
      break;
    }
    *record_end = '\0';
    Ingredient *ing = &s_recipe.ingredients[index];
    const char *step = prv_field(&cursor);
    ing->step = step[0] ? (uint8_t)prv_int(step) : NO_STEP;
    ing->flags = (uint8_t)prv_int(prv_field(&cursor));
    ing->amount = prv_int(prv_field(&cursor));
    ing->unit = prv_dup(prv_field(&cursor), UNIT_LEN);
    ing->unit_plural = prv_dup(prv_field(&cursor), UNIT_LEN);
    ing->food = prv_dup(prv_field(&cursor), FOOD_LEN);
    ing->food_plural = prv_dup(prv_field(&cursor), FOOD_LEN);
    ing->note = prv_dup(prv_field(&cursor), NOTE_LEN);
    index++;
    cursor = record_end + 1;
  }
  if (index > s_recipe.ingredients_received) {
    s_recipe.ingredients_received = index;
  }
  free(copy);
}

void model_recipe_step(uint8_t index, const char *name, const char *text, int32_t seconds, uint8_t flags) {
  if (index >= s_recipe.step_count) {
    return;
  }
  Step *step = &s_recipe.steps[index];
  free(step->name);
  free(step->text);
  step->name = prv_dup(name, STEP_NAME_LEN);
  step->text = prv_dup(text, STEP_TEXT_LEN);
  step->seconds = seconds;
  step->flags = flags;
  step->received = true;
}

void model_recipe_done(void) {
  if (s_recipe.state == RecipeLoading) {
    s_recipe.state = RecipeReady;
  }
}

void model_recipe_fail(uint8_t code, const char *text) {
  s_recipe.state = RecipeFailed;
  s_recipe.error = code;
  ui_copy(s_recipe.error_text, sizeof(s_recipe.error_text), text);
}

bool model_ingredient_checked(uint8_t index) {
  return index < MAX_INGREDIENTS && (s_recipe.checked[index / 8] & (1 << (index % 8)));
}

void model_ingredient_toggle(uint8_t index) {
  if (index < MAX_INGREDIENTS) {
    s_recipe.checked[index / 8] ^= (uint8_t)(1 << (index % 8));
  }
}

static int32_t prv_scaled(int32_t amount) {
  if (s_recipe.servings_x100 <= 0 || s_recipe.chosen_x100 == s_recipe.servings_x100) {
    return amount;
  }
  return (int32_t)(((int64_t)amount * s_recipe.chosen_x100 + s_recipe.servings_x100 / 2) / s_recipe.servings_x100);
}

void model_ingredient_text(const Ingredient *ing, char *buf, size_t size) {
  const char *food = ing->food ? ing->food : "";
  if (ing->flags & INGREDIENT_HEADER) {
    ui_copy(buf, size, food);
    return;
  }
  if ((ing->flags & INGREDIENT_NO_AMOUNT) || ing->amount <= 0) {
    ui_copy(buf, size, food);
    return;
  }
  const int32_t amount = prv_scaled(ing->amount);
  const bool many = amount > 1000;
  char num[16];
  ui_format_amount(num, sizeof(num), amount);
  const char *unit = (many && ing->unit_plural) ? ing->unit_plural : ing->unit;
  // "2 eggs" takes the plural food; "2 cups flour" keeps it singular.
  if (!unit && many && ing->food_plural) {
    food = ing->food_plural;
  }
  if (unit) {
    snprintf(buf, size, "%s %s %s", num, unit, food);
  } else {
    snprintf(buf, size, "%s %s", num, food);
  }
}

uint8_t model_step_ingredients(uint8_t step, uint8_t *out, uint8_t max) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < s_recipe.ingredients_received && n < max; i++) {
    const Ingredient *ing = &s_recipe.ingredients[i];
    if (ing->step == step && !(ing->flags & INGREDIENT_HEADER)) {
      out[n++] = i;
    }
  }
  return n;
}
