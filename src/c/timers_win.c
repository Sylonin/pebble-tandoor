#include "timers_win.h"

#include "recipe_win.h"
#include "timers.h"
#include "ui.h"

#define ROW_H 56

static Window *s_window;
static Layer *s_status;
static MenuLayer *s_menu;
static Layer *s_empty;
static uint8_t s_slots[MAX_TIMERS];
static uint8_t s_count;

static void prv_reload(void) {
  s_count = timers_sorted(s_slots);
  layer_set_hidden(s_empty, s_count > 0);
  layer_set_hidden(menu_layer_get_layer(s_menu), s_count == 0);
  menu_layer_reload_data(s_menu);
}

static uint16_t prv_rows(MenuLayer *menu, uint16_t section, void *context) {
  return s_count;
}

static int16_t prv_cell_height(MenuLayer *menu, MenuIndex *index, void *context) {
  return ROW_H;
}

static void prv_draw_row(GContext *ctx, const Layer *cell, MenuIndex *index, void *context) {
  const int slot = s_slots[index->row];
  const Timer *t = timers_get(slot);
  if (!t) {
    return;
  }
  const GRect b = layer_get_bounds(cell);
  const bool hi = menu_cell_layer_is_highlighted(cell);
  const TimerState state = (TimerState)t->state;
  char clock[16];
  ui_format_clock(clock, sizeof(clock), timers_left(slot));
  const GColor accent = state == TimerRunning ? UI_HOT : state == TimerRinging ? UI_FG : UI_ACCENT;
  graphics_context_set_text_color(ctx, hi ? GColorBlack : accent);
  graphics_draw_text(ctx, clock, ui_font_digits(), GRect(UI_PAD, 5, 110, 26), GTextOverflowModeFill,
                     GTextAlignmentLeft, NULL);
  const char *badge = state == TimerPaused ? "PAUSED" : state == TimerRinging ? "DONE" : NULL;
  if (badge) {
    graphics_draw_text(ctx, badge, ui_font_small_bold(), GRect(UI_PAD, 9, b.size.w - 2 * UI_PAD, 18),
                       GTextOverflowModeFill, GTextAlignmentRight, NULL);
  }
  char line[TIMER_LABEL_LEN + 16];
  snprintf(line, sizeof(line), "%s \u00b7 step %d", t->label, t->step + 1);
  graphics_context_set_text_color(ctx, hi ? GColorBlack : UI_MUTED);
  graphics_draw_text(ctx, line, ui_font_small(), GRect(UI_PAD, 32, b.size.w - 2 * UI_PAD, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

// Timer menu ------------------------------------------------------------------------------------

typedef enum { ActToggle, ActMinute, ActRestart, ActStep, ActCancel } Act;

static int s_menu_slot = -1;

static void prv_action(ActionMenu *menu, const ActionMenuItem *item, void *context) {
  const Act act = (Act)(uintptr_t)action_menu_item_get_action_data(item);
  const Timer *t = timers_get(s_menu_slot);
  if (!t) {
    return;
  }
  switch (act) {
    case ActToggle: timers_toggle(s_menu_slot); break;
    case ActMinute: timers_add_minute(s_menu_slot); break;
    case ActRestart: timers_restart(s_menu_slot); break;
    case ActCancel: timers_cancel(s_menu_slot); break;
    case ActStep: {
      char label[TIMER_LABEL_LEN];
      ui_copy(label, sizeof(label), t->label);
      recipe_win_open_step(t->recipe_id, label, t->step);
      break;
    }
  }
}

static void prv_menu_closed(ActionMenu *menu, const ActionMenuItem *performed, void *context) {
  action_menu_hierarchy_destroy(action_menu_get_root_level(menu), NULL, NULL);
}

static void prv_open_menu(int slot) {
  const Timer *t = timers_get(slot);
  if (!t) {
    return;
  }
  s_menu_slot = slot;
  ActionMenuLevel *level = action_menu_level_create(5);
  if (!level) {
    return;
  }
  if (t->state == TimerRunning) {
    action_menu_level_add_action(level, "Pause", prv_action, (void *)ActToggle);
  } else if (t->state == TimerPaused) {
    action_menu_level_add_action(level, "Resume", prv_action, (void *)ActToggle);
  }
  action_menu_level_add_action(level, "Add a minute", prv_action, (void *)ActMinute);
  action_menu_level_add_action(level, "Restart", prv_action, (void *)ActRestart);
  action_menu_level_add_action(level, "Open the step", prv_action, (void *)ActStep);
  action_menu_level_add_action(level, "Cancel timer", prv_action, (void *)ActCancel);
  ActionMenuConfig config = {
      .root_level = level,
      .colors = {.background = UI_HOT, .foreground = GColorBlack},
      .did_close = prv_menu_closed,
      .align = ActionMenuAlignCenter,
  };
  action_menu_open(&config);
}

static void prv_select(MenuLayer *menu, MenuIndex *index, void *context) {
  prv_open_menu(s_slots[index->row]);
}

// Window ----------------------------------------------------------------------------------------

static void prv_empty_update(Layer *layer, GContext *ctx) {
  ui_draw_message(ctx, layer_get_bounds(layer), IconTimer, UI_ACCENT, "No timers",
                  "Start one from a step while you cook.");
}

static void prv_timers_changed(void) {
  uint8_t slots[MAX_TIMERS];
  if (timers_sorted(slots) != s_count) {
    prv_reload();
  } else {
    s_count = timers_sorted(s_slots);
    layer_mark_dirty(menu_layer_get_layer(s_menu));
  }
}

static void prv_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  const GRect b = layer_get_bounds(root);
  s_status = ui_status_create(GRect(0, 0, b.size.w, UI_STATUS_H), "Timers");
  const GRect body = GRect(0, UI_STATUS_H, b.size.w, b.size.h - UI_STATUS_H);
  s_menu = menu_layer_create(body);
  ui_style_menu(s_menu);
  menu_layer_set_callbacks(s_menu, NULL,
                           (MenuLayerCallbacks){
                               .get_num_rows = prv_rows,
                               .get_cell_height = prv_cell_height,
                               .draw_row = prv_draw_row,
                               .select_click = prv_select,
                           });
  menu_layer_set_click_config_onto_window(s_menu, window);
  s_empty = layer_create(body);
  layer_set_update_proc(s_empty, prv_empty_update);
  layer_add_child(root, menu_layer_get_layer(s_menu));
  layer_add_child(root, s_empty);
  layer_add_child(root, s_status);
}

static void prv_appear(Window *window) {
  prv_reload();
  timers_observe(prv_timers_changed);
}

static void prv_disappear(Window *window) {
  timers_unobserve(prv_timers_changed);
}

static void prv_unload(Window *window) {
  menu_layer_destroy(s_menu);
  layer_destroy(s_empty);
  ui_status_destroy(s_status);
  window_destroy(s_window);
  s_window = NULL;
}

void timers_win_push(void) {
  if (s_window) {
    return;
  }
  s_window = window_create();
  window_set_background_color(s_window, UI_BG);
  window_set_window_handlers(s_window, (WindowHandlers){
                                           .load = prv_load,
                                           .appear = prv_appear,
                                           .disappear = prv_disappear,
                                           .unload = prv_unload,
                                       });
  window_stack_push(s_window, true);
}
