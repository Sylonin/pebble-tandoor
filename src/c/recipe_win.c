#include "recipe_win.h"

#include "comm.h"
#include "cook_win.h"
#include "model.h"
#include "ui.h"

#define FRAME_MS 300
#define ROW_H 44
#define LABEL_H 28
#define SECTION_H 30
#define STEP_PREVIEW_H 40
#define MAX_SERVINGS 9900

enum { SectionTop, SectionIngredients, SectionSteps, SectionCount };
enum { TopStart, TopServings, TopCount };

static Window *s_window;
static Layer *s_status;
static MenuLayer *s_menu;
static Layer *s_overlay;
static AppTimer *s_anim;
static uint8_t s_frame;
static int s_resume_step = -1;
static int16_t s_hero_h;
static int16_t s_ing_h[MAX_INGREDIENTS];
static int16_t s_step_h[MAX_STEPS];
static bool s_measured_ready;

static void prv_click_config(void *context);

static int16_t prv_width(void) {
  return layer_get_bounds(window_get_root_layer(s_window)).size.w;
}

static void prv_format_servings(char *buf, size_t size, int32_t x100) {
  if (x100 % 100 == 0) {
    snprintf(buf, size, "%d", (int)(x100 / 100));
  } else if (x100 % 10 == 0) {
    snprintf(buf, size, "%d.%d", (int)(x100 / 100), (int)((x100 % 100) / 10));
  } else {
    snprintf(buf, size, "%d.%02d", (int)(x100 / 100), (int)(x100 % 100));
  }
}

static void prv_servings_text(char *buf, size_t size) {
  const Recipe *r = model_recipe();
  char num[20];
  prv_format_servings(num, sizeof(num), r->chosen_x100);
  if (r->servtext[0]) {
    snprintf(buf, size, "%s %s", num, r->servtext);
  } else {
    snprintf(buf, size, "%s %s", num, r->chosen_x100 == 100 ? "serving" : "servings");
  }
}

// Measuring -------------------------------------------------------------------------------------

#define HERO_NAME_MAX 90
#define HERO_DESC_MAX 92

static void prv_measure_hero(void) {
  const Recipe *r = model_recipe();
  const int16_t w = (int16_t)(prv_width() - 2 * UI_PAD);
  int16_t h = 4 + ui_text_height(r->name, ui_font_title(), w, HERO_NAME_MAX) + 4;
  h += 22;  // the time and servings line
  if (r->desc[0]) {
    h += ui_text_height(r->desc, ui_font_text(), w, HERO_DESC_MAX) + 4;
  }
  s_hero_h = (int16_t)(h + 6);
}

static int16_t prv_measure_ingredient(uint8_t i) {
  const Ingredient *ing = &model_recipe()->ingredients[i];
  if (ing->flags & INGREDIENT_HEADER) {
    return LABEL_H;
  }
  char text[NAME_LEN + FOOD_LEN];
  model_ingredient_text(ing, text, sizeof(text));
  const int16_t w = (int16_t)(prv_width() - 2 * UI_PAD - 18 - UI_PAD);
  int16_t h = ui_text_height(text, ui_font_row(), w, 66);
  if (ing->note) {
    h += ui_text_height(ing->note, ui_font_small(), w, 34);
  }
  h += 10;
  return h < 34 ? 34 : h;
}

static void prv_measure_lists(void) {
  const Recipe *r = model_recipe();
  for (uint8_t i = 0; i < r->ingredients_received; i++) {
    s_ing_h[i] = prv_measure_ingredient(i);
  }
  const int16_t w = (int16_t)(prv_width() - 2 * UI_PAD - 26);
  for (uint8_t i = 0; i < r->step_count; i++) {
    const Step *s = &r->steps[i];
    int16_t h = 10;
    if (!s->received) {
      h += 22;
    } else {
      if (s->name) {
        h += ui_text_height(s->name, ui_font_row(), w, 22);
      }
      h += ui_text_height(s->text ? s->text : "", ui_font_text(), w, STEP_PREVIEW_H);
      if (s->seconds > 0) {
        h += 18;
      }
    }
    s_step_h[i] = h < ROW_H ? ROW_H : h;
  }
}

// Menu ------------------------------------------------------------------------------------------

static uint16_t prv_sections(MenuLayer *menu, void *context) {
  return SectionCount;
}

static uint16_t prv_rows(MenuLayer *menu, uint16_t section, void *context) {
  const Recipe *r = model_recipe();
  switch (section) {
    case SectionTop: return TopCount;
    case SectionIngredients: return r->ingredients_received;
    default: return r->step_count;
  }
}

static int16_t prv_header_height(MenuLayer *menu, uint16_t section, void *context) {
  const Recipe *r = model_recipe();
  switch (section) {
    case SectionTop: return s_hero_h;
    case SectionIngredients: return r->ingredient_count ? SECTION_H : 0;
    default: return r->step_count ? SECTION_H : 0;
  }
}

static int16_t prv_cell_height(MenuLayer *menu, MenuIndex *index, void *context) {
  switch (index->section) {
    case SectionTop: return ROW_H;
    case SectionIngredients: return s_ing_h[index->row];
    default: return s_step_h[index->row];
  }
}

static void prv_draw_meta(GContext *ctx, int16_t x, int16_t y, IconId icon, const char *text, int16_t *out_x) {
  const GSize s = ui_icon_size(icon);
  ui_icon_draw(ctx, icon, GPoint(x, (int16_t)(y + (18 - s.h) / 2 + 1)), UI_ACCENT);
  x += s.w + 4;
  const GSize t = graphics_text_layout_get_content_size(text, ui_font_text(), GRect(0, 0, 160, 22),
                                                        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
  graphics_context_set_text_color(ctx, UI_MUTED);
  graphics_draw_text(ctx, text, ui_font_text(), GRect(x, (int16_t)(y - 3), t.w + 2, 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  *out_x = (int16_t)(x + t.w + 12);
}

static void prv_draw_hero(GContext *ctx, const Layer *cell) {
  const Recipe *r = model_recipe();
  const GRect b = layer_get_bounds(cell);
  const int16_t w = (int16_t)(b.size.w - 2 * UI_PAD);
  graphics_context_set_fill_color(ctx, UI_BG);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  int16_t y = 0;
  const int16_t name_h = ui_text_height(r->name, ui_font_title(), w, HERO_NAME_MAX);
  graphics_context_set_text_color(ctx, UI_FG);
  graphics_draw_text(ctx, r->name, ui_font_title(), GRect(UI_PAD, y, w, name_h + 4), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);
  y += name_h + 8;
  int16_t x = UI_PAD;
  const int32_t minutes = r->working + r->waiting;
  if (minutes > 0) {
    char time_text[20];
    ui_format_minutes(time_text, sizeof(time_text), minutes);
    prv_draw_meta(ctx, x, y, IconClock, time_text, &x);
  }
  char servings[48];
  prv_servings_text(servings, sizeof(servings));
  prv_draw_meta(ctx, x, y, IconUsers, servings, &x);
  y += 22;
  if (r->desc[0]) {
    graphics_context_set_text_color(ctx, UI_MUTED);
    graphics_draw_text(ctx, r->desc, ui_font_text(), GRect(UI_PAD, (int16_t)(y - 2), w, HERO_DESC_MAX),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
}

static void prv_draw_header(GContext *ctx, const Layer *cell, uint16_t section, void *context) {
  const Recipe *r = model_recipe();
  char text[32];
  switch (section) {
    case SectionTop: prv_draw_hero(ctx, cell); return;
    case SectionIngredients: {
      int n = 0;
      for (uint8_t i = 0; i < r->ingredients_received; i++) {
        n += (r->ingredients[i].flags & INGREDIENT_HEADER) ? 0 : 1;
      }
      snprintf(text, sizeof(text), "INGREDIENTS \u00b7 %d", n);
      break;
    }
    default: snprintf(text, sizeof(text), "STEPS \u00b7 %d", r->step_count); break;
  }
  ui_draw_section_header(ctx, cell, text);
}

static void prv_draw_top(GContext *ctx, const Layer *cell, uint16_t row) {
  const Recipe *r = model_recipe();
  const GRect b = layer_get_bounds(cell);
  const bool hi = menu_cell_layer_is_highlighted(cell);
  if (row == TopStart) {
    char sub[24];
    if (r->state == RecipeReady) {
      snprintf(sub, sizeof(sub), r->step_count == 1 ? "1 step" : "%d steps", r->step_count);
    } else {
      snprintf(sub, sizeof(sub), "Loading steps\u2026");
    }
    ui_draw_row(ctx, cell, IconPlay, "Start cooking", sub);
    return;
  }
  ui_draw_row(ctx, cell, IconUsers, "Servings", NULL);
  char num[20];
  prv_format_servings(num, sizeof(num), r->chosen_x100);
  graphics_context_set_text_color(ctx, hi ? GColorBlack : UI_ACCENT);
  graphics_draw_text(ctx, num, ui_font_title(), GRect(b.size.w - 70 - UI_PAD, (int16_t)((b.size.h - 30) / 2), 70, 30),
                     GTextOverflowModeFill, GTextAlignmentRight, NULL);
}

static void prv_draw_ingredient(GContext *ctx, const Layer *cell, uint8_t i) {
  const Ingredient *ing = &model_recipe()->ingredients[i];
  const GRect b = layer_get_bounds(cell);
  const bool hi = menu_cell_layer_is_highlighted(cell);
  if (ing->flags & INGREDIENT_HEADER) {
    graphics_context_set_text_color(ctx, hi ? GColorBlack : UI_ACCENT);
    graphics_draw_text(ctx, ing->food ? ing->food : "", ui_font_small_bold(),
                       GRect(UI_PAD, (int16_t)(b.size.h - 21), b.size.w - 2 * UI_PAD, 18),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    return;
  }
  const bool checked = model_ingredient_checked(i);
  const IconId icon = checked ? IconCheckOn : IconCheckOff;
  const GSize s = ui_icon_size(icon);
  ui_icon_draw(ctx, icon, GPoint(UI_PAD, 7), hi ? GColorBlack : checked ? UI_DONE : UI_MUTED);
  const int16_t x = (int16_t)(UI_PAD + s.w + UI_PAD);
  const int16_t w = (int16_t)(b.size.w - x - UI_PAD);
  char text[NAME_LEN + FOOD_LEN];
  model_ingredient_text(ing, text, sizeof(text));
  const int16_t text_h = ui_text_height(text, ui_font_row(), w, 66);
  graphics_context_set_text_color(ctx, hi ? GColorBlack : checked ? UI_MUTED : UI_FG);
  graphics_draw_text(ctx, text, ui_font_row(), GRect(x, 1, w, text_h + 2), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);
  if (ing->note) {
    graphics_context_set_text_color(ctx, hi ? GColorBlack : UI_MUTED);
    graphics_draw_text(ctx, ing->note, ui_font_small(), GRect(x, (int16_t)(text_h + 1), w, 34),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
}

static void prv_draw_step(GContext *ctx, const Layer *cell, uint8_t i) {
  const Step *s = &model_recipe()->steps[i];
  const GRect b = layer_get_bounds(cell);
  const bool hi = menu_cell_layer_is_highlighted(cell);
  // Step number in a ring.
  char num[4];
  snprintf(num, sizeof(num), "%d", i + 1);
  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_stroke_color(ctx, hi ? GColorBlack : UI_ACCENT);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_circle(ctx, GPoint(UI_PAD + 9, 16), 9);
  graphics_context_set_text_color(ctx, hi ? GColorBlack : UI_ACCENT);
  graphics_draw_text(ctx, num, ui_font_small_bold(), GRect(UI_PAD, 6, 19, 16), GTextOverflowModeFill,
                     GTextAlignmentCenter, NULL);
  const int16_t x = UI_PAD + 26;
  const int16_t w = (int16_t)(b.size.w - x - UI_PAD);
  int16_t y = 3;
  if (!s->received) {
    graphics_context_set_text_color(ctx, hi ? GColorBlack : UI_MUTED);
    graphics_draw_text(ctx, "Loading\u2026", ui_font_text(), GRect(x, y, w, 22), GTextOverflowModeFill,
                       GTextAlignmentLeft, NULL);
    return;
  }
  if (s->name) {
    const int16_t h = ui_text_height(s->name, ui_font_row(), w, 22);
    graphics_context_set_text_color(ctx, hi ? GColorBlack : UI_FG);
    graphics_draw_text(ctx, s->name, ui_font_row(), GRect(x, y, w, h), GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentLeft, NULL);
    y += h;
  }
  const char *text = s->text ? s->text : "";
  const int16_t h = ui_text_height(text, ui_font_text(), w, STEP_PREVIEW_H);
  graphics_context_set_text_color(ctx, hi ? GColorBlack : s->name ? UI_MUTED : UI_FG);
  graphics_draw_text(ctx, text, ui_font_text(), GRect(x, y, w, h), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);
  y += h;
  if (s->seconds > 0) {
    char clock[16];
    ui_format_duration(clock, sizeof(clock), s->seconds);
    ui_icon_draw(ctx, IconClock, GPoint(x, (int16_t)(y + 3)), hi ? GColorBlack : UI_HOT);
    graphics_context_set_text_color(ctx, hi ? GColorBlack : UI_HOT);
    graphics_draw_text(ctx, clock, ui_font_small_bold(), GRect(x + 18, (int16_t)(y - 1), 80, 18),
                       GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  }
}

static void prv_draw_row(GContext *ctx, const Layer *cell, MenuIndex *index, void *context) {
  switch (index->section) {
    case SectionTop: prv_draw_top(ctx, cell, index->row); break;
    case SectionIngredients: prv_draw_ingredient(ctx, cell, (uint8_t)index->row); break;
    default: prv_draw_step(ctx, cell, (uint8_t)index->row); break;
  }
}

// Servings stepper ------------------------------------------------------------------------------

static Window *s_serv_window;
static Layer *s_serv_layer;
static Layer *s_serv_status;

static void prv_serv_update(Layer *layer, GContext *ctx) {
  const Recipe *r = model_recipe();
  const GRect b = layer_get_bounds(layer);
  const int16_t cx = b.size.w / 2;
  char num[20];
  prv_format_servings(num, sizeof(num), r->chosen_x100);
  graphics_context_set_fill_color(ctx, UI_ACCENT);
  // Up and down arrows above and below the number.
  const GPoint up[] = {{cx, 32}, {(int16_t)(cx + 12), 46}, {(int16_t)(cx - 12), 46}};
  const GPoint down[] = {{cx, 156}, {(int16_t)(cx + 12), 142}, {(int16_t)(cx - 12), 142}};
  GPathInfo info = {.num_points = 3, .points = (GPoint *)up};
  GPath *path = gpath_create(&info);
  if (path) {
    graphics_context_set_antialiased(ctx, true);
    gpath_draw_filled(ctx, path);
    gpath_destroy(path);
  }
  info.points = (GPoint *)down;
  path = gpath_create(&info);
  if (path) {
    gpath_draw_filled(ctx, path);
    gpath_destroy(path);
  }
  graphics_context_set_text_color(ctx, UI_FG);
  graphics_draw_text(ctx, num, ui_font_digits_big(), GRect(0, 70, b.size.w, 44), GTextOverflowModeFill,
                     GTextAlignmentCenter, NULL);
  graphics_context_set_text_color(ctx, UI_MUTED);
  graphics_draw_text(ctx, r->servtext[0] ? r->servtext : "servings", ui_font_text(), GRect(0, 112, b.size.w, 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  graphics_draw_text(ctx, "Ingredient amounts follow.", ui_font_small(), GRect(UI_PAD, 172, b.size.w - 2 * UI_PAD, 20),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

static void prv_serv_change(int32_t delta) {
  Recipe *r = model_recipe();
  // Whole servings: from 1.5, up goes to 2 and down to 1.
  int32_t v = r->chosen_x100;
  if (delta > 0) {
    v = (v / 100 + 1) * 100;
  } else {
    v = v % 100 ? (v / 100) * 100 : v - 100;
  }
  if (v < 100 || v > MAX_SERVINGS) {
    return;
  }
  r->chosen_x100 = v;
  layer_mark_dirty(s_serv_layer);
}

static void prv_serv_up(ClickRecognizerRef recognizer, void *context) {
  prv_serv_change(1);
}

static void prv_serv_down(ClickRecognizerRef recognizer, void *context) {
  prv_serv_change(-1);
}

static void prv_serv_select(ClickRecognizerRef recognizer, void *context) {
  window_stack_remove(s_serv_window, true);
}

static void prv_serv_click_config(void *context) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 150, prv_serv_up);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 150, prv_serv_down);
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_serv_select);
}

static void prv_serv_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  const GRect b = layer_get_bounds(root);
  s_serv_status = ui_status_create(GRect(0, 0, b.size.w, UI_STATUS_H), "Servings");
  s_serv_layer = layer_create(GRect(0, UI_STATUS_H, b.size.w, b.size.h - UI_STATUS_H));
  layer_set_update_proc(s_serv_layer, prv_serv_update);
  layer_add_child(root, s_serv_layer);
  layer_add_child(root, s_serv_status);
}

static void prv_serv_unload(Window *window) {
  layer_destroy(s_serv_layer);
  ui_status_destroy(s_serv_status);
  window_destroy(window);
  s_serv_window = NULL;
}

static void prv_serv_push(void) {
  s_serv_window = window_create();
  window_set_background_color(s_serv_window, UI_BG);
  window_set_click_config_provider(s_serv_window, prv_serv_click_config);
  window_set_window_handlers(s_serv_window, (WindowHandlers){.load = prv_serv_load, .unload = prv_serv_unload});
  window_stack_push(s_serv_window, true);
}

// Selection -------------------------------------------------------------------------------------

// Ingredient headers are labels, not choices: the selection steps over them.
static void prv_selection_will_change(MenuLayer *menu, MenuIndex *new_index, MenuIndex old_index, void *context) {
  const Recipe *r = model_recipe();
  if (new_index->section != SectionIngredients || !(r->ingredients[new_index->row].flags & INGREDIENT_HEADER)) {
    return;
  }
  const bool down = menu_index_compare(new_index, &old_index) > 0;
  if (down) {
    if (new_index->row + 1 < r->ingredients_received) {
      new_index->row++;
    } else {
      *new_index = (MenuIndex){.section = SectionSteps, .row = 0};
    }
  } else if (new_index->row > 0) {
    new_index->row--;
  } else {
    *new_index = (MenuIndex){.section = SectionTop, .row = TopServings};
  }
}

static void prv_select(MenuLayer *menu, MenuIndex *index, void *context) {
  const Recipe *r = model_recipe();
  switch (index->section) {
    case SectionTop:
      if (index->row == TopServings) {
        prv_serv_push();
      } else if (r->state == RecipeReady && r->step_count > 0) {
        cook_win_push(0);
      } else {
        vibes_short_pulse();
      }
      break;
    case SectionIngredients:
      if (!(r->ingredients[index->row].flags & INGREDIENT_HEADER)) {
        model_ingredient_toggle((uint8_t)index->row);
        layer_mark_dirty(menu_layer_get_layer(s_menu));
      }
      break;
    default:
      if (r->state == RecipeReady) {
        cook_win_push((uint8_t)index->row);
      } else {
        vibes_short_pulse();
      }
      break;
  }
}

// Overlay: loading and errors -------------------------------------------------------------------

static void prv_frame(void *context) {
  s_anim = NULL;
  s_frame++;
  layer_mark_dirty(s_overlay);
  if (!model_recipe()->has_head && model_recipe()->state == RecipeLoading) {
    s_anim = app_timer_register(FRAME_MS, prv_frame, NULL);
  }
}

static void prv_overlay_update(Layer *layer, GContext *ctx) {
  const Recipe *r = model_recipe();
  const GRect b = layer_get_bounds(layer);
  if (r->state == RecipeFailed) {
    char body[128];
    snprintf(body, sizeof(body), "%s\nSelect to try again.", r->error_text);
    ui_draw_message(ctx, b, r->error == ERR_OFFLINE ? IconOffline : r->error == ERR_SETUP ? IconSetup : IconEmpty,
                    UI_HOT, r->error == ERR_NOT_FOUND ? "Not found" : "Couldn\u2019t load", body);
    return;
  }
  const int16_t w = (int16_t)(b.size.w - 2 * UI_PAD);
  graphics_context_set_text_color(ctx, UI_FG);
  graphics_draw_text(ctx, r->name, ui_font_title(), GRect(UI_PAD, 0, w, HERO_NAME_MAX), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);
  ui_draw_loading(ctx, GRect(0, b.size.h / 2, b.size.w, 40), s_frame);
}

static void prv_apply(void) {
  const Recipe *r = model_recipe();
  const bool overlay = !r->has_head || r->state == RecipeFailed;
  layer_set_hidden(s_overlay, !overlay);
  layer_set_hidden(menu_layer_get_layer(s_menu), overlay);
  if (overlay) {
    window_set_click_config_provider(s_window, prv_click_config);
    layer_mark_dirty(s_overlay);
    if (r->state == RecipeLoading && !s_anim) {
      s_anim = app_timer_register(FRAME_MS, prv_frame, NULL);
    }
    return;
  }
  menu_layer_set_click_config_onto_window(s_menu, s_window);
  prv_measure_hero();
  prv_measure_lists();
  menu_layer_reload_data(s_menu);
}

static void prv_retry(ClickRecognizerRef recognizer, void *context) {
  Recipe *r = model_recipe();
  if (r->state != RecipeFailed) {
    return;
  }
  const uint32_t id = r->id;
  char name[NAME_LEN];
  ui_copy(name, sizeof(name), r->name);
  model_recipe_reset(id, name);
  if (!comm_request_recipe(id)) {
    model_recipe_fail(ERR_OFFLINE, "Too many requests at once.");
  }
  prv_apply();
}

static void prv_click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_retry);
}

void recipe_win_on_update(void) {
  if (!s_window) {
    return;
  }
  const Recipe *r = model_recipe();
  prv_apply();
  if (r->state == RecipeReady && !s_measured_ready && s_resume_step >= 0) {
    // Resuming: go straight on to the step, once.
    const int step = s_resume_step > r->step_count ? r->step_count : s_resume_step;
    s_resume_step = -1;
    cook_win_push((uint8_t)step);
  }
  s_measured_ready = r->state == RecipeReady;
}

// Window ----------------------------------------------------------------------------------------

static void prv_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  const GRect b = layer_get_bounds(root);
  s_status = ui_status_create(GRect(0, 0, b.size.w, UI_STATUS_H), "Recipe");
  const GRect body = GRect(0, UI_STATUS_H, b.size.w, b.size.h - UI_STATUS_H);
  s_menu = menu_layer_create(body);
  ui_style_menu(s_menu);
  menu_layer_set_callbacks(s_menu, NULL,
                           (MenuLayerCallbacks){
                               .get_num_sections = prv_sections,
                               .get_num_rows = prv_rows,
                               .get_header_height = prv_header_height,
                               .get_cell_height = prv_cell_height,
                               .draw_header = prv_draw_header,
                               .draw_row = prv_draw_row,
                               .select_click = prv_select,
                               .selection_will_change = prv_selection_will_change,
                           });
  s_overlay = layer_create(GRect(0, UI_STATUS_H + 4, b.size.w, b.size.h - UI_STATUS_H - 4));
  layer_set_update_proc(s_overlay, prv_overlay_update);
  layer_add_child(root, menu_layer_get_layer(s_menu));
  layer_add_child(root, s_overlay);
  layer_add_child(root, s_status);
  prv_apply();
}

static void prv_appear(Window *window) {
  // Servings may have changed in the stepper.
  if (model_recipe()->has_head) {
    prv_measure_lists();
    menu_layer_reload_data(s_menu);
  }
}

static void prv_unload(Window *window) {
  if (s_anim) {
    app_timer_cancel(s_anim);
    s_anim = NULL;
  }
  menu_layer_destroy(s_menu);
  layer_destroy(s_overlay);
  ui_status_destroy(s_status);
  window_destroy(s_window);
  s_window = NULL;
}

void recipe_win_push(uint32_t id, const char *name_hint, int resume_step) {
  // One recipe at a time: windows of the old one would show the new recipe's data.
  cook_win_close();
  if (s_serv_window) {
    window_stack_remove(s_serv_window, false);
  }
  if (s_window) {
    window_stack_remove(s_window, false);
  }
  s_resume_step = resume_step;
  s_measured_ready = false;
  model_recipe_reset(id, name_hint);
  if (!comm_request_recipe(id)) {
    model_recipe_fail(ERR_OFFLINE, "Too many requests at once.");
  }
  s_window = window_create();
  window_set_background_color(s_window, UI_BG);
  window_set_window_handlers(s_window, (WindowHandlers){
                                           .load = prv_load,
                                           .appear = prv_appear,
                                           .unload = prv_unload,
                                       });
  window_stack_push(s_window, true);
}

void recipe_win_open_step(uint32_t id, const char *name, uint8_t step) {
  const Recipe *r = model_recipe();
  if (r->state == RecipeReady && r->id == id && s_window) {
    cook_win_push(step > r->step_count ? r->step_count : step);
    return;
  }
  recipe_win_push(id, name, step);
}
