#include "home_win.h"

#include "comm.h"
#include "list_win.h"
#include "model.h"
#include "msg_win.h"
#include "recipe_win.h"
#include "timers.h"
#include "timers_win.h"
#include "ui.h"

#define HERO_H 54
#define LABEL_H 22
#define ROW_H 38
#define COOK_ROW_H 46

typedef enum {
  RowResume,
  RowTimers,
  RowPlan,
  RowSearch,
  RowRecent,
  RowBooks,
  RowKeywords,
  RowAll,
  RowRandom,
} RowKind;

typedef struct {
  RowKind kind;
  IconId icon;
  const char *title;
} BrowseRow;

static const BrowseRow BROWSE[] = {
    {RowPlan, IconPlan, "Meal plan"},     {RowSearch, IconSearch, "Search"}, {RowRecent, IconRecent, "Recent"},
    {RowBooks, IconBook, "Books"},        {RowKeywords, IconTag, "Keywords"}, {RowAll, IconAll, "All recipes"},
    {RowRandom, IconRandom, "Surprise me"},
};

static Window *s_window;
static Layer *s_status;
static MenuLayer *s_menu;
static Layer *s_setup;
static DictationSession *s_dictation;
static RowKind s_cook_rows[2];
static uint8_t s_cook_count;

static void prv_rebuild(void) {
  s_cook_count = 0;
  if (model_session()) {
    s_cook_rows[s_cook_count++] = RowResume;
  }
  uint8_t slots[MAX_TIMERS];
  if (timers_sorted(slots) > 0) {
    s_cook_rows[s_cook_count++] = RowTimers;
  }
}

// Menu ------------------------------------------------------------------------------------------

static uint16_t prv_sections(MenuLayer *menu, void *context) {
  return 2;
}

static uint16_t prv_rows(MenuLayer *menu, uint16_t section, void *context) {
  return section == 0 ? s_cook_count : ARRAY_LENGTH(BROWSE);
}

static int16_t prv_header_height(MenuLayer *menu, uint16_t section, void *context) {
  if (section == 0) {
    return (int16_t)(HERO_H + (s_cook_count ? LABEL_H : 0));
  }
  return LABEL_H + 4;
}

static int16_t prv_cell_height(MenuLayer *menu, MenuIndex *index, void *context) {
  return index->section == 0 ? COOK_ROW_H : ROW_H;
}

static const char *prv_greeting(void) {
  const time_t now = time(NULL);
  const struct tm *t = localtime(&now);
  if (t->tm_hour >= 5 && t->tm_hour < 11) {
    return "Good morning";
  }
  if (t->tm_hour < 14 && t->tm_hour >= 11) {
    return "Lunch time?";
  }
  if (t->tm_hour < 17 && t->tm_hour >= 14) {
    return "Good afternoon";
  }
  if (t->tm_hour < 22 && t->tm_hour >= 17) {
    return "What\u2019s for dinner?";
  }
  return "Midnight snack?";
}

static void prv_draw_header(GContext *ctx, const Layer *cell, uint16_t section, void *context) {
  const GRect b = layer_get_bounds(cell);
  graphics_context_set_fill_color(ctx, UI_BG);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  if (section == 1) {
    ui_draw_section_header(ctx, cell, "RECIPES");
    return;
  }
  graphics_context_set_text_color(ctx, UI_FG);
  graphics_draw_text(ctx, prv_greeting(), ui_font_title(), GRect(UI_PAD, 2, b.size.w - 2 * UI_PAD, 30),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  const char *host = model_host();
  graphics_context_set_text_color(ctx, UI_MUTED);
  graphics_draw_text(ctx, host[0] ? host : "Tandoor recipes", ui_font_small(),
                     GRect(UI_PAD, 30, b.size.w - 2 * UI_PAD, 18), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);
  if (s_cook_count) {
    graphics_context_set_text_color(ctx, UI_ACCENT);
    graphics_draw_text(ctx, "COOKING", ui_font_small_bold(), GRect(UI_PAD, HERO_H + LABEL_H - 19, b.size.w, 18),
                       GTextOverflowModeFill, GTextAlignmentLeft, NULL);
    graphics_context_set_stroke_color(ctx, UI_LINE);
    graphics_draw_line(ctx, GPoint(UI_PAD, b.size.h - 1), GPoint(b.size.w - UI_PAD, b.size.h - 1));
  }
}

static void prv_draw_row(GContext *ctx, const Layer *cell, MenuIndex *index, void *context) {
  if (index->section == 1) {
    ui_draw_row(ctx, cell, BROWSE[index->row].icon, BROWSE[index->row].title, NULL);
    return;
  }
  char sub[64];
  if (s_cook_rows[index->row] == RowResume) {
    const Session *session = model_session();
    if (!session) {
      return;
    }
    snprintf(sub, sizeof(sub), "Step %d \u00b7 %s", session->step + 1, session->name);
    ui_draw_row(ctx, cell, IconResume, "Continue cooking", sub);
    return;
  }
  uint8_t slots[MAX_TIMERS];
  const uint8_t n = timers_sorted(slots);
  if (n == 0) {
    return;
  }
  const Timer *t = timers_get(slots[0]);
  char clock[16];
  if (t->state == TimerRinging) {
    snprintf(clock, sizeof(clock), "done");
  } else {
    ui_format_clock(clock, sizeof(clock), timers_left(slots[0]));
  }
  if (n == 1) {
    snprintf(sub, sizeof(sub), "Step %d \u00b7 %s%s", t->step + 1, clock, t->state == TimerPaused ? ", paused" : "");
    ui_draw_row(ctx, cell, IconTimer, t->label, sub);
    return;
  }
  char title[16];
  snprintf(title, sizeof(title), "%d timers", n);
  snprintf(sub, sizeof(sub), "Next %s \u00b7 %s", clock, t->label);
  ui_draw_row(ctx, cell, IconTimer, title, sub);
}

// Voice search ----------------------------------------------------------------------------------

static void prv_dictation_done(DictationSession *session, DictationSessionStatus status, char *text, void *context) {
  if (status == DictationSessionStatusSuccess && text && text[0]) {
    char title[48];
    snprintf(title, sizeof(title), "\u201c%s\u201d", text);
    list_win_push(KIND_SEARCH, 0, title, text);
    return;
  }
  switch (status) {
    case DictationSessionStatusFailureSystemAborted:
    case DictationSessionStatusFailureNoSpeechDetected:
      // The user backed out or said nothing; the menu is the right place to be.
      return;
    case DictationSessionStatusFailureConnectivityError:
      msg_win_push(IconOffline, "No connection", "Voice search needs your phone and its internet connection.");
      return;
    case DictationSessionStatusFailureDisabled:
      msg_win_push(IconSearch, "Voice is off", "Turn on voice input in the Pebble app to search by voice.");
      return;
    default:
      msg_win_push(IconSearch, "Didn\u2019t catch that", "Voice search couldn\u2019t understand you. Try again.");
      return;
  }
}

static void prv_search(void) {
  if (!s_dictation) {
    s_dictation = dictation_session_create(QUERY_LEN, prv_dictation_done, NULL);
  }
  if (!s_dictation) {
    msg_win_push(IconSearch, "No voice search", "This watch can\u2019t take voice input right now.");
    return;
  }
  dictation_session_enable_confirmation(s_dictation, true);
  dictation_session_start(s_dictation);
}

// Selection -------------------------------------------------------------------------------------

static void prv_select(MenuLayer *menu, MenuIndex *index, void *context) {
  const RowKind kind = index->section == 0 ? s_cook_rows[index->row] : BROWSE[index->row].kind;
  switch (kind) {
    case RowResume: {
      const Session *session = model_session();
      if (session) {
        recipe_win_push(session->recipe_id, session->name, session->step);
      }
      break;
    }
    case RowTimers: timers_win_push(); break;
    case RowPlan: list_win_push(KIND_PLAN, 0, "Meal plan", NULL); break;
    case RowSearch: prv_search(); break;
    case RowRecent: list_win_push(KIND_RECENT, 0, "Recent", NULL); break;
    case RowBooks: list_win_push(KIND_BOOKS, 0, "Books", NULL); break;
    case RowKeywords: list_win_push(KIND_KEYWORDS, 0, "Keywords", NULL); break;
    case RowAll: list_win_push(KIND_ALL, 0, "All recipes", NULL); break;
    case RowRandom: recipe_win_push(0, "Surprise me", -1); break;
  }
}

// Long Select on "Continue cooking" forgets the session.
static void prv_select_long(MenuLayer *menu, MenuIndex *index, void *context) {
  if (index->section == 0 && s_cook_rows[index->row] == RowResume) {
    model_session_clear();
    prv_rebuild();
    menu_layer_reload_data(s_menu);
    vibes_short_pulse();
  }
}

// Window ----------------------------------------------------------------------------------------

static void prv_setup_update(Layer *layer, GContext *ctx) {
  ui_draw_message(ctx, layer_get_bounds(layer), IconSetup, UI_ACCENT, "Set up Tandoor",
                  "Open Tandoor\u2019s settings in the Pebble app and enter your server and API token.");
}

static void prv_apply_status(void) {
  const bool setup = model_status_known() && !model_ready();
  layer_set_hidden(s_setup, !setup);
  layer_set_hidden(menu_layer_get_layer(s_menu), setup);
}

static void prv_timers_changed(void) {
  const uint8_t before = s_cook_count;
  prv_rebuild();
  if (before != s_cook_count) {
    menu_layer_reload_data(s_menu);
  } else {
    layer_mark_dirty(menu_layer_get_layer(s_menu));
  }
}

static void prv_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  const GRect b = layer_get_bounds(root);
  // Before the menu exists, so its first selection is "Continue cooking" when there is one.
  prv_rebuild();
  s_status = ui_status_create(GRect(0, 0, b.size.w, UI_STATUS_H), "Tandoor");
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
                               .select_long_click = prv_select_long,
                           });
  menu_layer_set_click_config_onto_window(s_menu, window);
  s_setup = layer_create(body);
  layer_set_update_proc(s_setup, prv_setup_update);
  layer_add_child(root, menu_layer_get_layer(s_menu));
  layer_add_child(root, s_setup);
  layer_add_child(root, s_status);
  prv_apply_status();
}

static void prv_appear(Window *window) {
  prv_rebuild();
  menu_layer_reload_data(s_menu);
  timers_observe(prv_timers_changed);
}

static void prv_disappear(Window *window) {
  timers_unobserve(prv_timers_changed);
}

static void prv_unload(Window *window) {
  if (s_dictation) {
    dictation_session_destroy(s_dictation);
    s_dictation = NULL;
  }
  menu_layer_destroy(s_menu);
  layer_destroy(s_setup);
  ui_status_destroy(s_status);
  window_destroy(s_window);
  s_window = NULL;
}

void home_win_on_status(void) {
  if (s_window) {
    prv_apply_status();
    layer_mark_dirty(menu_layer_get_layer(s_menu));
  }
}

void home_win_push(void) {
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
