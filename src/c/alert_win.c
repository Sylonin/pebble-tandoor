#include "alert_win.h"

#include "alert.h"
#include "recipe_win.h"
#include "timers.h"
#include "ui.h"

#define BAR_W 34

static Window *s_window;
static Layer *s_layer;
static int s_slot = -1;

static void prv_update(Layer *layer, GContext *ctx) {
  const GRect b = layer_get_bounds(layer);
  const Timer *t = timers_get(s_slot);
  graphics_context_set_fill_color(ctx, UI_HOT);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  // Action bar.
  const GRect bar = GRect(b.size.w - BAR_W, 0, BAR_W, b.size.h);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bar, 0, GCornerNone);
  const IconId icons[] = {IconPlus, IconDismiss, IconStep};
  const int16_t centers[] = {(int16_t)(b.size.h / 6), (int16_t)(b.size.h / 2), (int16_t)(b.size.h * 5 / 6)};
  for (int i = 0; i < 3; i++) {
    const GSize s = ui_icon_size(icons[i]);
    ui_icon_draw(ctx, icons[i], GPoint((int16_t)(bar.origin.x + (BAR_W - s.w) / 2), (int16_t)(centers[i] - s.h / 2)),
                 i == 1 ? UI_ACCENT : UI_FG);
  }
  if (!t) {
    return;
  }

  const int16_t w = b.size.w - BAR_W;
  const int32_t over = -timers_left(s_slot);
  // The bell rocks while it rings.
  const GSize bell = ui_icon_size(IconBell);
  const int16_t rock = (int16_t)((over % 2) ? 2 : -2);
  ui_icon_draw(ctx, IconBell, GPoint((int16_t)((w - bell.w) / 2 + rock), 18), GColorBlack);

  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, "Time\u2019s up", ui_font_big_title(), GRect(4, 66, w - 8, 34), GTextOverflowModeFill,
                     GTextAlignmentCenter, NULL);
  graphics_draw_text(ctx, t->label, ui_font_row(), GRect(UI_PAD, 102, w - 2 * UI_PAD, 44),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  char line[48];
  char dur[16];
  ui_format_duration(dur, sizeof(dur), t->duration);
  snprintf(line, sizeof(line), "Step %d \u00b7 %s timer", t->step + 1, dur);
  graphics_draw_text(ctx, line, ui_font_text(), GRect(UI_PAD, 146, w - 2 * UI_PAD, 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

  char overdue[16];
  ui_format_clock(overdue, sizeof(overdue), -over);
  graphics_draw_text(ctx, overdue, ui_font_digits(), GRect(0, 180, w, 26), GTextOverflowModeFill,
                     GTextAlignmentCenter, NULL);
}

// Shows the next ringing timer, or closes when none is left.
static void prv_next(void) {
  s_slot = timers_first_ringing();
  if (s_slot < 0) {
    alert_stop();
    if (s_window) {
      window_stack_remove(s_window, true);
    }
    return;
  }
  layer_mark_dirty(s_layer);
}

static void prv_snooze(ClickRecognizerRef recognizer, void *context) {
  timers_add_minute(s_slot);
  prv_next();
}

static void prv_dismiss(ClickRecognizerRef recognizer, void *context) {
  timers_cancel(s_slot);
  prv_next();
}

static void prv_open_step(ClickRecognizerRef recognizer, void *context) {
  const Timer *t = timers_get(s_slot);
  if (!t) {
    return;
  }
  const uint32_t id = t->recipe_id;
  const uint8_t step = t->step;
  char label[TIMER_LABEL_LEN];
  ui_copy(label, sizeof(label), t->label);
  timers_cancel(s_slot);
  s_slot = timers_first_ringing();
  if (s_slot < 0) {
    alert_stop();
    window_stack_remove(s_window, false);
  }
  // On to the step after the one the timer belonged to.
  recipe_win_open_step(id, label, (uint8_t)(step + 1));
  if (s_slot >= 0) {
    // Another timer still rings: keep its alert in front.
    alert_win_show();
  }
}

static void prv_click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, prv_snooze);
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_dismiss);
  window_single_click_subscribe(BUTTON_ID_DOWN, prv_open_step);
  window_single_click_subscribe(BUTTON_ID_BACK, prv_dismiss);
}

static void prv_timers_changed(void) {
  if (s_layer) {
    layer_mark_dirty(s_layer);
  }
}

static void prv_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_layer, prv_update);
  layer_add_child(root, s_layer);
  timers_observe(prv_timers_changed);
}

static void prv_unload(Window *window) {
  timers_unobserve(prv_timers_changed);
  alert_stop();
  layer_destroy(s_layer);
  s_layer = NULL;
  window_destroy(s_window);
  s_window = NULL;
}

void alert_win_show(void) {
  s_slot = timers_first_ringing();
  if (s_slot < 0) {
    return;
  }
  if (s_window && window_stack_get_top_window() != s_window) {
    // Something opened on top since; unload frees the window, and it is pushed again below.
    window_stack_remove(s_window, false);
  }
  if (!s_window) {
    s_window = window_create();
    window_set_background_color(s_window, UI_HOT);
    window_set_click_config_provider(s_window, prv_click_config);
    window_set_window_handlers(s_window, (WindowHandlers){.load = prv_load, .unload = prv_unload});
    window_stack_push(s_window, true);
  }
  s_slot = timers_first_ringing();
  layer_mark_dirty(s_layer);
  alert_play();
}
