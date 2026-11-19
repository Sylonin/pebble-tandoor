#include "cook_win.h"

#include "comm.h"
#include "model.h"
#include "timers.h"
#include "timers_win.h"
#include "ui.h"

#define PROGRESS_H 5
#define CARD_H 54
#define SCROLL_MS 180
#define SLIDE 36
#define MAX_STEP_INGREDIENTS 16
#define CONTENT_MAX 4000

typedef enum { LogIdle, LogSending, LogDone, LogFailed } LogState;

static Window *s_window;
static Layer *s_status;
static Layer *s_progress;
static Layer *s_content;
static Layer *s_card;
static Animation *s_anim;
static uint8_t s_step;
static int16_t s_offset;
static int16_t s_slide;
static int16_t s_content_h;
static int16_t s_from_offset;
static int16_t s_to_offset;
static int16_t s_from_slide;
static LogState s_log;
static uint32_t s_log_req;
static char s_log_text[96];
static char s_title[TIMER_LABEL_LEN];

static const Recipe *prv_recipe(void) {
  return model_recipe();
}

static bool prv_done_page(void) {
  return s_step >= prv_recipe()->step_count;
}

static const Step *prv_current(void) {
  return prv_done_page() ? NULL : &prv_recipe()->steps[s_step];
}

static bool prv_has_timer(void) {
  const Step *s = prv_current();
  return s && s->seconds > 0;
}

static int16_t prv_view_h(void) {
  return layer_get_bounds(s_content).size.h;
}

static int16_t prv_max_offset(void) {
  const int16_t m = (int16_t)(s_content_h - prv_view_h());
  return m > 0 ? m : 0;
}

// Content ---------------------------------------------------------------------------------------

typedef struct {
  GContext *ctx;  // NULL while measuring
  int16_t x;
  int16_t w;
  int16_t y;
  int16_t shift;  // scroll offset and slide
} Pen;

static void prv_text(Pen *pen, const char *text, GFont font, GColor color, int16_t gap_after) {
  if (!text || !text[0]) {
    return;
  }
  const int16_t h = ui_text_height(text, font, pen->w, CONTENT_MAX);
  if (pen->ctx) {
    graphics_context_set_text_color(pen->ctx, color);
    graphics_draw_text(pen->ctx, text, font, GRect(pen->x, (int16_t)(pen->y - pen->shift), pen->w, h + 4),
                       GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
  }
  pen->y += h + gap_after;
}

static void prv_rule(Pen *pen) {
  if (pen->ctx) {
    graphics_context_set_stroke_color(pen->ctx, UI_LINE);
    graphics_draw_line(pen->ctx, GPoint(pen->x, (int16_t)(pen->y - pen->shift)),
                       GPoint((int16_t)(pen->x + pen->w), (int16_t)(pen->y - pen->shift)));
  }
  pen->y += 6;
}

// Lays out the step; draws it when pen->ctx is set. Returns the content height.
static int16_t prv_layout_step(Pen *pen) {
  const Recipe *r = prv_recipe();
  const Step *s = prv_current();
  char label[32];
  snprintf(label, sizeof(label), "STEP %d OF %d", s_step + 1, r->step_count);
  pen->y = 4;
  prv_text(pen, label, ui_font_small_bold(), UI_ACCENT, 0);
  if (!s->received) {
    prv_text(pen, "Loading\u2026", ui_font_body(), UI_MUTED, 6);
    return pen->y;
  }
  prv_text(pen, s->name, ui_font_row(), UI_FG, 0);
  prv_text(pen, s->text, ui_font_body(), UI_FG, 8);

  uint8_t ings[MAX_STEP_INGREDIENTS];
  const uint8_t n = model_step_ingredients(s_step, ings, MAX_STEP_INGREDIENTS);
  if (n > 0) {
    prv_rule(pen);
    prv_text(pen, "YOU NEED", ui_font_small_bold(), UI_MUTED, 0);
    for (uint8_t i = 0; i < n; i++) {
      const Ingredient *ing = &r->ingredients[ings[i]];
      char text[NAME_LEN + FOOD_LEN + NOTE_LEN];
      char main[NAME_LEN + FOOD_LEN];
      model_ingredient_text(ing, main, sizeof(main));
      if (ing->note) {
        snprintf(text, sizeof(text), "\u2022 %s, %s", main, ing->note);
      } else {
        snprintf(text, sizeof(text), "\u2022 %s", main);
      }
      prv_text(pen, text, ui_font_row(), UI_FG, 0);
    }
    pen->y += 8;
  }

  prv_rule(pen);
  if (s_step + 1 < r->step_count) {
    const Step *next = &r->steps[s_step + 1];
    prv_text(pen, "NEXT", ui_font_small_bold(), UI_ACCENT, 0);
    const char *preview = next->name ? next->name : next->text;
    if (preview) {
      // Two lines are enough to say what comes.
      const int16_t h = ui_text_height(preview, ui_font_text(), pen->w, 44);
      if (pen->ctx) {
        graphics_context_set_text_color(pen->ctx, UI_MUTED);
        graphics_draw_text(pen->ctx, preview, ui_font_text(), GRect(pen->x, (int16_t)(pen->y - pen->shift), pen->w, h),
                           GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
      }
      pen->y += h;
    }
  } else {
    prv_text(pen, "LAST STEP", ui_font_small_bold(), UI_ACCENT, 0);
    prv_text(pen, "Press Down to finish.", ui_font_text(), UI_MUTED, 0);
  }
  pen->y += 10;
  return pen->y;
}

static void prv_draw_done(GContext *ctx, GRect b) {
  const Recipe *r = prv_recipe();
  const int16_t w = (int16_t)(b.size.w - 2 * UI_PAD);
  const GSize isz = ui_icon_size(IconDone);
  int16_t y = (int16_t)(16 - s_slide);
  ui_icon_draw(ctx, IconDone, GPoint((int16_t)((b.size.w - isz.w) / 2), y), UI_ACCENT);
  y += isz.h + 6;
  graphics_context_set_text_color(ctx, UI_FG);
  graphics_draw_text(ctx, "Enjoy your meal!", ui_font_title(), GRect(UI_PAD, y, w, 30), GTextOverflowModeFill,
                     GTextAlignmentCenter, NULL);
  y += 30;
  graphics_context_set_text_color(ctx, UI_MUTED);
  graphics_draw_text(ctx, r->name, ui_font_text(), GRect(UI_PAD, y, w, 44), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter, NULL);
  y = (int16_t)(b.size.h - 50);
  const GRect pill = GRect(UI_PAD * 2, y, (int16_t)(b.size.w - 4 * UI_PAD), 34);
  const char *text = "Log as cooked";
  GColor fill = UI_ACCENT;
  GColor fg = GColorBlack;
  switch (s_log) {
    case LogIdle: break;
    case LogSending: text = "Logging\u2026"; fill = UI_LINE; fg = UI_FG; break;
    case LogDone: text = "Logged in Tandoor"; fill = UI_DONE; break;
    case LogFailed: text = "Retry logging"; fill = UI_HOT; break;
  }
  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_rect(ctx, pill, 17, GCornersAll);
  graphics_context_set_text_color(ctx, fg);
  graphics_draw_text(ctx, text, ui_font_row(), GRect(pill.origin.x, (int16_t)(pill.origin.y + 5), pill.size.w, 24),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
  if (s_log == LogFailed && s_log_text[0]) {
    graphics_context_set_text_color(ctx, UI_HOT);
    graphics_draw_text(ctx, s_log_text, ui_font_small(), GRect(UI_PAD, (int16_t)(y - 34), w, 34),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  }
}

static void prv_content_update(Layer *layer, GContext *ctx) {
  const GRect b = layer_get_bounds(layer);
  if (prv_done_page()) {
    prv_draw_done(ctx, b);
    return;
  }
  Pen pen = {.ctx = ctx, .x = UI_PAD, .w = (int16_t)(b.size.w - 2 * UI_PAD), .shift = (int16_t)(s_offset - s_slide)};
  prv_layout_step(&pen);
  // Scroll position, as a thin bar on the right.
  const int16_t max = (int16_t)(s_content_h - b.size.h);
  if (max > 0) {
    const int16_t bar_h = (int16_t)(b.size.h * b.size.h / s_content_h);
    const int16_t bar_y = (int16_t)((b.size.h - bar_h) * s_offset / max);
    graphics_context_set_fill_color(ctx, UI_LINE);
    graphics_fill_rect(ctx, GRect(b.size.w - 3, bar_y, 2, bar_h), 1, GCornersAll);
  }
}

static void prv_measure(void) {
  if (prv_done_page()) {
    s_content_h = 0;
    return;
  }
  Pen pen = {.ctx = NULL, .x = UI_PAD, .w = (int16_t)(layer_get_bounds(s_content).size.w - 2 * UI_PAD)};
  s_content_h = prv_layout_step(&pen);
}

// Progress and timer card -----------------------------------------------------------------------

static void prv_progress_update(Layer *layer, GContext *ctx) {
  const GRect b = layer_get_bounds(layer);
  const uint8_t n = prv_recipe()->step_count;
  if (n == 0) {
    return;
  }
  const int16_t gap = n > 20 ? 1 : 2;
  const int16_t total = (int16_t)(b.size.w - 2 * UI_PAD);
  for (uint8_t i = 0; i < n; i++) {
    const int16_t x0 = (int16_t)(UI_PAD + total * i / n);
    const int16_t x1 = (int16_t)(UI_PAD + total * (i + 1) / n - (i + 1 < n ? gap : 0));
    GColor color = UI_LINE;
    if (i < s_step) {
      color = UI_ACCENT;
    } else if (i == s_step) {
      color = UI_FG;
    }
    graphics_context_set_fill_color(ctx, color);
    graphics_fill_rect(ctx, GRect(x0, 1, (int16_t)(x1 - x0), 3), 1, GCornersAll);
  }
}

static void prv_card_update(Layer *layer, GContext *ctx) {
  const GRect b = layer_get_bounds(layer);
  const Step *s = prv_current();
  if (!s || s->seconds <= 0) {
    return;
  }
  const int slot = timers_find(prv_recipe()->id, s_step);
  const Timer *t = timers_get(slot);
  const TimerState state = t ? (TimerState)t->state : TimerIdle;
  const GRect card = GRect(6, 4, (int16_t)(b.size.w - 12), (int16_t)(b.size.h - 10));
  GColor fill = UI_LINE;
  GColor fg = UI_FG;
  GColor icon_bg = UI_ACCENT;
  GColor icon_fg = GColorBlack;
  IconId icon = IconPlaySmall;
  const char *hint = "Start";
  int32_t seconds = s->seconds;
  switch (state) {
    case TimerRunning:
      fill = UI_HOT;
      fg = GColorBlack;
      icon_bg = GColorBlack;
      icon_fg = UI_HOT;
      icon = IconPause;
      hint = "Pause";
      seconds = timers_left(slot);
      break;
    case TimerPaused:
      fg = UI_ACCENT;
      icon = IconPlaySmall;
      hint = "Resume";
      seconds = timers_left(slot);
      break;
    case TimerRinging:
      fill = UI_ACCENT;
      fg = GColorBlack;
      icon_bg = GColorBlack;
      icon_fg = UI_ACCENT;
      icon = IconBell;
      hint = "Done";
      seconds = timers_left(slot);
      break;
    case TimerIdle: break;
  }
  graphics_context_set_fill_color(ctx, fill);
  graphics_fill_rect(ctx, card, 10, GCornersAll);

  // Round button with the action's icon.
  const int16_t r = 15;
  const GPoint c = GPoint((int16_t)(card.origin.x + 8 + r), (int16_t)(card.origin.y + card.size.h / 2));
  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_fill_color(ctx, icon_bg);
  graphics_fill_circle(ctx, c, r);
  const IconId shown = icon == IconBell ? IconTimer : icon;
  const GSize isz = ui_icon_size(shown);
  ui_icon_draw(ctx, shown, GPoint((int16_t)(c.x - isz.w / 2 + (shown == IconPlaySmall ? 1 : 0)), (int16_t)(c.y - isz.h / 2)),
               icon_fg);

  char clock[16];
  ui_format_clock(clock, sizeof(clock), seconds);
  const int16_t tx = (int16_t)(c.x + r + 10);
  graphics_context_set_text_color(ctx, fg);
  graphics_draw_text(ctx, clock, ui_font_digits(), GRect(tx, (int16_t)(card.origin.y + 7), 90, 26),
                     GTextOverflowModeFill, GTextAlignmentLeft, NULL);
  graphics_draw_text(ctx, hint, ui_font_small_bold(),
                     GRect(card.origin.x, (int16_t)(card.origin.y + 11), (int16_t)(card.size.w - 10), 18),
                     GTextOverflowModeFill, GTextAlignmentRight, NULL);
  // Elapsed share along the bottom edge.
  if ((state == TimerRunning || state == TimerPaused) && t->duration > 0) {
    int32_t done = t->duration - seconds;
    if (done < 0) {
      done = 0;
    }
    const int16_t full = (int16_t)(card.size.w - 20);
    const int16_t w = (int16_t)(full * done / t->duration);
    graphics_context_set_fill_color(ctx, state == TimerRunning ? GColorBlack : UI_ACCENT);
    graphics_fill_rect(ctx, GRect((int16_t)(card.origin.x + 10), (int16_t)(card.origin.y + card.size.h - 6), w, 2), 0,
                       GCornerNone);
  }
}

static void prv_layout(void) {
  const GRect b = layer_get_bounds(window_get_root_layer(s_window));
  const int16_t top = UI_STATUS_H + PROGRESS_H;
  const int16_t card_h = prv_has_timer() ? CARD_H : 0;
  layer_set_frame(s_content, GRect(0, top, b.size.w, (int16_t)(b.size.h - top - card_h)));
  layer_set_frame(s_card, GRect(0, (int16_t)(b.size.h - card_h), b.size.w, card_h));
  layer_set_hidden(s_card, card_h == 0);
}

// Scrolling and paging --------------------------------------------------------------------------

static void prv_anim_update(Animation *animation, const AnimationProgress progress) {
  s_offset = (int16_t)(s_from_offset + (s_to_offset - s_from_offset) * progress / ANIMATION_NORMALIZED_MAX);
  s_slide = (int16_t)(s_from_slide - s_from_slide * progress / ANIMATION_NORMALIZED_MAX);
  layer_mark_dirty(s_content);
}

static const AnimationImplementation ANIM_IMPL = {.update = prv_anim_update};

static void prv_anim_stopped(Animation *animation, bool finished, void *context) {
  if (animation == s_anim) {
    s_anim = NULL;
  }
}

static void prv_animate(int16_t to_offset, int16_t from_slide) {
  if (s_anim) {
    animation_unschedule(s_anim);
    s_anim = NULL;
  }
  s_from_offset = s_offset;
  s_to_offset = to_offset;
  s_from_slide = from_slide;
  s_slide = from_slide;
  s_anim = animation_create();
  if (!s_anim) {
    s_offset = to_offset;
    s_slide = 0;
    layer_mark_dirty(s_content);
    return;
  }
  animation_set_duration(s_anim, SCROLL_MS);
  animation_set_curve(s_anim, AnimationCurveEaseOut);
  animation_set_implementation(s_anim, &ANIM_IMPL);
  // The system frees a finished animation; forget it then.
  animation_set_handlers(s_anim, (AnimationHandlers){.stopped = prv_anim_stopped}, NULL);
  animation_schedule(s_anim);
}

static void prv_go(uint8_t step, int16_t slide) {
  s_step = step;
  s_offset = 0;
  prv_layout();
  prv_measure();
  layer_mark_dirty(s_progress);
  layer_mark_dirty(s_card);
  if (prv_done_page()) {
    model_session_clear();
  } else {
    model_session_save(s_step);
  }
  prv_animate(0, slide);
}

static int16_t prv_page(void) {
  const int16_t page = (int16_t)(prv_view_h() - 44);
  return page < 40 ? 40 : page;
}

static void prv_down(ClickRecognizerRef recognizer, void *context) {
  const int16_t max = prv_max_offset();
  if (!prv_done_page() && s_to_offset < max) {
    const int16_t to = (int16_t)(s_to_offset + prv_page());
    prv_animate(to > max ? max : to, 0);
    return;
  }
  if (click_recognizer_is_repeating(recognizer) || prv_done_page()) {
    return;
  }
  prv_go((uint8_t)(s_step + 1), SLIDE);
}

static void prv_up(ClickRecognizerRef recognizer, void *context) {
  if (!prv_done_page() && s_to_offset > 0) {
    const int16_t to = (int16_t)(s_to_offset - prv_page());
    prv_animate(to < 0 ? 0 : to, 0);
    return;
  }
  if (click_recognizer_is_repeating(recognizer) || s_step == 0) {
    return;
  }
  prv_go((uint8_t)(s_step - 1), -SLIDE);
}

static void prv_log_cooked(void) {
  if (s_log == LogSending || s_log == LogDone) {
    return;
  }
  const Recipe *r = prv_recipe();
  s_log_req = comm_send_cooked(r->id, r->chosen_x100);
  s_log = s_log_req ? LogSending : LogFailed;
  layer_mark_dirty(s_content);
}

static void prv_select(ClickRecognizerRef recognizer, void *context) {
  if (prv_done_page()) {
    prv_log_cooked();
    return;
  }
  const Step *s = prv_current();
  if (!s->received || s->seconds <= 0) {
    prv_go((uint8_t)(s_step + 1), SLIDE);
    return;
  }
  const Recipe *r = prv_recipe();
  const int slot = timers_find(r->id, s_step);
  if (slot < 0) {
    if (timers_start(r->id, s_step, s->seconds, s_title) < 0) {
      vibes_double_pulse();
      return;
    }
    vibes_short_pulse();
  } else {
    timers_toggle(slot);
  }
  layer_mark_dirty(s_card);
}

// Step menu (long Select) -----------------------------------------------------------------------

typedef enum {
  ActStart,
  ActRestart,
  ActMinute,
  ActCancel,
  ActTimers,
  ActSteps,
  ActFinish,
} Act;

static void prv_action(ActionMenu *menu, const ActionMenuItem *item, void *context) {
  const Act act = (Act)(uintptr_t)action_menu_item_get_action_data(item);
  const Recipe *r = prv_recipe();
  const int slot = timers_find(r->id, s_step);
  switch (act) {
    case ActStart: {
      const Step *s = prv_current();
      if (s) {
        timers_start(r->id, s_step, s->seconds, s_title);
      }
      break;
    }
    case ActRestart: timers_restart(slot); break;
    case ActMinute: timers_add_minute(slot); break;
    case ActCancel: timers_cancel(slot); break;
    case ActTimers: timers_win_push(); break;
    case ActSteps: window_stack_remove(s_window, true); break;
    case ActFinish: prv_go(r->step_count, SLIDE); break;
  }
  layer_mark_dirty(s_card);
}

static void prv_menu_closed(ActionMenu *menu, const ActionMenuItem *performed, void *context) {
  action_menu_hierarchy_destroy(action_menu_get_root_level(menu), NULL, NULL);
}

static void prv_long_select(ClickRecognizerRef recognizer, void *context) {
  const Recipe *r = prv_recipe();
  ActionMenuLevel *level = action_menu_level_create(7);
  if (!level) {
    return;
  }
  const int slot = prv_done_page() ? -1 : timers_find(r->id, s_step);
  if (prv_has_timer()) {
    if (slot < 0) {
      action_menu_level_add_action(level, "Start timer", prv_action, (void *)ActStart);
    } else {
      action_menu_level_add_action(level, "Add a minute", prv_action, (void *)ActMinute);
      action_menu_level_add_action(level, "Restart timer", prv_action, (void *)ActRestart);
      action_menu_level_add_action(level, "Cancel timer", prv_action, (void *)ActCancel);
    }
  }
  uint8_t slots[MAX_TIMERS];
  if (timers_sorted(slots) > 0) {
    action_menu_level_add_action(level, "All timers", prv_action, (void *)ActTimers);
  }
  action_menu_level_add_action(level, "Ingredients and steps", prv_action, (void *)ActSteps);
  if (!prv_done_page()) {
    action_menu_level_add_action(level, "Finish cooking", prv_action, (void *)ActFinish);
  }
  ActionMenuConfig config = {
      .root_level = level,
      .colors = {.background = UI_ACCENT, .foreground = GColorBlack},
      .did_close = prv_menu_closed,
      .align = ActionMenuAlignCenter,
  };
  action_menu_open(&config);
}

static void prv_click_config(void *context) {
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 120, prv_down);
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 120, prv_up);
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, prv_long_select, NULL);
}

// Window ----------------------------------------------------------------------------------------

static void prv_timers_changed(void) {
  if (s_card) {
    layer_mark_dirty(s_card);
  }
}

static void prv_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  const GRect b = layer_get_bounds(root);
  s_status = ui_status_create(GRect(0, 0, b.size.w, UI_STATUS_H), s_title);
  s_progress = layer_create(GRect(0, UI_STATUS_H, b.size.w, PROGRESS_H));
  layer_set_update_proc(s_progress, prv_progress_update);
  s_content = layer_create(GRect(0, UI_STATUS_H + PROGRESS_H, b.size.w, b.size.h - UI_STATUS_H - PROGRESS_H));
  layer_set_update_proc(s_content, prv_content_update);
  s_card = layer_create(GRect(0, b.size.h - CARD_H, b.size.w, CARD_H));
  layer_set_update_proc(s_card, prv_card_update);
  layer_add_child(root, s_content);
  layer_add_child(root, s_card);
  layer_add_child(root, s_progress);
  layer_add_child(root, s_status);
}

static void prv_appear(Window *window) {
  timers_observe(prv_timers_changed);
  if (model_settings()->backlight) {
    light_enable(true);
  }
}

static void prv_disappear(Window *window) {
  timers_unobserve(prv_timers_changed);
  light_enable(false);
}

static void prv_unload(Window *window) {
  if (s_anim) {
    animation_unschedule(s_anim);
    s_anim = NULL;
  }
  layer_destroy(s_content);
  layer_destroy(s_card);
  layer_destroy(s_progress);
  ui_status_destroy(s_status);
  s_content = NULL;
  s_card = NULL;
  window_destroy(s_window);
  s_window = NULL;
}

void cook_win_push(uint8_t step) {
  const Recipe *r = prv_recipe();
  if (step > r->step_count) {
    step = r->step_count;
  }
  if (s_window && window_stack_get_top_window() != s_window) {
    // Opened again from a timer while other windows cover it: open it afresh on top.
    window_stack_remove(s_window, false);
  }
  if (!s_window) {
    ui_copy(s_title, sizeof(s_title), r->name);
    s_log = LogIdle;
    s_log_req = 0;
    s_window = window_create();
    window_set_background_color(s_window, UI_BG);
    window_set_click_config_provider(s_window, prv_click_config);
    window_set_window_handlers(s_window, (WindowHandlers){
                                             .load = prv_load,
                                             .appear = prv_appear,
                                             .disappear = prv_disappear,
                                             .unload = prv_unload,
                                         });
    window_stack_push(s_window, true);
  }
  prv_go(step, 0);
}

void cook_win_close(void) {
  if (s_window) {
    window_stack_remove(s_window, false);
  }
}

void cook_win_on_result(uint32_t req, uint8_t state, const char *text) {
  if (!s_window || req != s_log_req) {
    return;
  }
  s_log = state == 0 ? LogDone : LogFailed;
  ui_copy(s_log_text, sizeof(s_log_text), text);
  if (s_log == LogDone) {
    vibes_short_pulse();
  }
  layer_mark_dirty(s_content);
}
