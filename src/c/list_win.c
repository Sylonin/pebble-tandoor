#include "list_win.h"

#include <string.h>

#include "comm.h"
#include "model.h"
#include "recipe_win.h"
#include "ui.h"

#define MAX_WINDOWS 6
#define FRAME_MS 300
#define ROW_PAD 10
#define SIMPLE_ROW_H 38
#define FOOTER_H 38
#define HEADER_H 26

#define FIELD_SEP '\x1f'
#define RECORD_END '\x1e'

typedef struct {
  char type;
  uint32_t id;
  int16_t height;
  char *title;
  char *sub;
} Row;

typedef struct {
  Window *window;
  Layer *status;
  MenuLayer *menu;
  Layer *overlay;
  AppTimer *anim;
  uint8_t frame;
  uint8_t kind;
  uint32_t id;
  char title[LIST_TITLE_LEN];
  char query[QUERY_LEN];
  Row *rows;
  uint16_t count;
  uint16_t cap;
  uint16_t page;
  uint32_t req;
  bool loading;
  bool more;
  int32_t total;
  uint8_t error;
  char error_text[96];
} ListData;

static ListData *s_lists[MAX_WINDOWS];

static void prv_click_config(void *context);

static bool prv_has_footer(const ListData *d) {
  return d->count > 0 && (d->loading || d->more || d->error);
}

// Rows ------------------------------------------------------------------------------------------

static char *prv_dup(const char *src) {
  const size_t n = strlen(src);
  char *out = malloc(n + 1);
  if (out) {
    memcpy(out, src, n + 1);
  }
  return out;
}

static int16_t prv_row_height(const Row *row, int16_t width) {
  if (row->type != 'r' && row->type != 'n') {
    return SIMPLE_ROW_H;
  }
  // Titles get two lines at most.
  const int16_t title_h = ui_text_height(row->title, ui_font_row(), (int16_t)(width - 2 * UI_PAD), 40);
  const int16_t sub_h = (row->sub && row->sub[0]) ? 16 : 0;
  const int16_t h = (int16_t)(title_h + sub_h + ROW_PAD);
  return h < SIMPLE_ROW_H ? SIMPLE_ROW_H : h;
}

static char *prv_field(char **cursor) {
  char *start = *cursor;
  char *p = start;
  while (*p && *p != FIELD_SEP) {
    p++;
  }
  if (*p == FIELD_SEP) {
    *p = '\0';
    *cursor = p + 1;
  } else {
    *cursor = p;
  }
  return start;
}

static void prv_append(ListData *d, const char *data) {
  const size_t len = strlen(data);
  char *copy = malloc(len + 1);
  if (!copy) {
    return;
  }
  memcpy(copy, data, len + 1);
  const int16_t width = layer_get_bounds(window_get_root_layer(d->window)).size.w;
  char *cursor = copy;
  while (*cursor && d->count < MAX_LIST) {
    char *end = strchr(cursor, RECORD_END);
    if (!end) {
      break;
    }
    *end = '\0';
    if (d->count == d->cap) {
      const uint16_t cap = d->cap ? (uint16_t)(d->cap * 2) : 20;
      Row *rows = realloc(d->rows, cap * sizeof(Row));
      if (!rows) {
        break;
      }
      d->rows = rows;
      d->cap = cap;
    }
    Row *row = &d->rows[d->count];
    const char *type = prv_field(&cursor);
    row->type = type[0];
    const char *id = prv_field(&cursor);
    row->id = 0;
    for (; *id >= '0' && *id <= '9'; id++) {
      row->id = row->id * 10 + (uint32_t)(*id - '0');
    }
    row->title = prv_dup(prv_field(&cursor));
    row->sub = prv_dup(prv_field(&cursor));
    if (!row->title) {
      free(row->sub);
      break;
    }
    row->height = prv_row_height(row, width);
    d->count++;
    cursor = end + 1;
  }
  free(copy);
}

static void prv_free_rows(ListData *d) {
  for (uint16_t i = 0; i < d->count; i++) {
    free(d->rows[i].title);
    free(d->rows[i].sub);
  }
  free(d->rows);
  d->rows = NULL;
  d->count = 0;
  d->cap = 0;
}

// Loading ---------------------------------------------------------------------------------------

static void prv_frame(void *context) {
  ListData *d = context;
  d->anim = NULL;
  d->frame++;
  layer_mark_dirty(d->overlay);
  if (d->loading && d->count == 0) {
    d->anim = app_timer_register(FRAME_MS, prv_frame, d);
  }
}

// The overlay covers the menu while the list is empty: loading, an error or "nothing here".
static void prv_refresh(ListData *d) {
  const bool overlay = d->count == 0;
  layer_set_hidden(d->overlay, !overlay);
  layer_set_hidden(menu_layer_get_layer(d->menu), overlay);
  if (overlay) {
    window_set_click_config_provider_with_context(d->window, prv_click_config, d);
    layer_mark_dirty(d->overlay);
    if (d->loading && !d->anim) {
      d->anim = app_timer_register(FRAME_MS, prv_frame, d);
    }
  } else {
    menu_layer_set_click_config_onto_window(d->menu, d->window);
    menu_layer_reload_data(d->menu);
  }
}

static void prv_request(ListData *d, uint16_t page) {
  d->page = page;
  d->error = 0;
  d->loading = true;
  d->req = comm_request_list(d->kind, d->id, page, d->query);
  if (!d->req) {
    d->loading = false;
    d->error = ERR_OFFLINE;
    ui_copy(d->error_text, sizeof(d->error_text), "Too many requests at once. Try again.");
  }
  prv_refresh(d);
}

static void prv_retry(ListData *d) {
  if (d->loading) {
    return;
  }
  prv_request(d, d->count == 0 ? 1 : (uint16_t)(d->page + (d->error ? 0 : 1)));
}

static void prv_select_overlay(ClickRecognizerRef recognizer, void *context) {
  ListData *d = context;
  if (d->error) {
    prv_retry(d);
  }
}

static void prv_click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_overlay);
}

static void prv_overlay_update(Layer *layer, GContext *ctx) {
  const ListData *d = window_get_user_data(layer_get_window(layer));
  const GRect b = layer_get_bounds(layer);
  if (d->loading) {
    ui_draw_loading(ctx, b, d->frame);
    return;
  }
  if (d->error) {
    char body[128];
    snprintf(body, sizeof(body), "%s\nSelect to try again.", d->error_text);
    const IconId icon = d->error == ERR_SETUP ? IconSetup : d->error == ERR_OFFLINE ? IconOffline : IconEmpty;
    const char *title = d->error == ERR_SETUP    ? "Not set up"
                        : d->error == ERR_OFFLINE ? "Can\u2019t connect"
                        : d->error == ERR_AUTH    ? "Token refused"
                                                  : "Something broke";
    ui_draw_message(ctx, b, icon, UI_HOT, title, body);
    return;
  }
  const char *body = d->kind == KIND_PLAN      ? "Nothing planned for the next seven days."
                     : d->kind == KIND_SEARCH  ? "No recipe matches what you said."
                     : d->kind == KIND_BOOKS   ? "You have no recipe books yet."
                     : d->kind == KIND_KEYWORDS ? "You have no keywords yet."
                                               : "There are no recipes here yet.";
  ui_draw_message(ctx, b, IconEmpty, UI_ACCENT, "Nothing here", body);
}

// Menu ------------------------------------------------------------------------------------------

static uint16_t prv_rows(MenuLayer *menu, uint16_t section, void *context) {
  ListData *d = context;
  return (uint16_t)(d->count + (prv_has_footer(d) ? 1 : 0));
}

static int16_t prv_cell_height(MenuLayer *menu, MenuIndex *index, void *context) {
  ListData *d = context;
  return index->row < d->count ? d->rows[index->row].height : FOOTER_H;
}

static int16_t prv_header_height(MenuLayer *menu, uint16_t section, void *context) {
  return HEADER_H;
}

static void prv_draw_header(GContext *ctx, const Layer *cell, uint16_t section, void *context) {
  ListData *d = context;
  char text[32];
  const int32_t total = d->total >= 0 ? d->total : d->count;
  if (d->kind == KIND_PLAN) {
    snprintf(text, sizeof(text), "NEXT 7 DAYS");
  } else if (d->kind == KIND_BOOKS) {
    snprintf(text, sizeof(text), total == 1 ? "1 BOOK" : "%d BOOKS", (int)total);
  } else if (d->kind == KIND_KEYWORDS) {
    snprintf(text, sizeof(text), "KEYWORDS");
  } else {
    snprintf(text, sizeof(text), total == 1 ? "1 RECIPE" : "%d RECIPES", (int)total);
  }
  ui_draw_section_header(ctx, cell, text);
}

static void prv_draw_footer(GContext *ctx, const Layer *cell, const ListData *d) {
  if (d->loading) {
    ui_draw_loading(ctx, layer_get_bounds(cell), d->frame);
    return;
  }
  ui_draw_row(ctx, cell, IconCount, d->error ? "Couldn\u2019t load more" : "Load more",
              d->error ? "Select to try again" : NULL);
}

static void prv_draw_recipe(GContext *ctx, const Layer *cell, const Row *row) {
  const GRect b = layer_get_bounds(cell);
  const bool hi = menu_cell_layer_is_highlighted(cell);
  const bool note = row->type == 'n';
  const int16_t w = (int16_t)(b.size.w - 2 * UI_PAD);
  const bool has_sub = row->sub && row->sub[0];
  const int16_t sub_h = has_sub ? 16 : 0;
  const int16_t title_h = (int16_t)(b.size.h - ROW_PAD - sub_h);
  const int16_t y = (int16_t)(ROW_PAD / 2 - 3);
  graphics_context_set_text_color(ctx, hi ? GColorBlack : note ? UI_MUTED : UI_FG);
  graphics_draw_text(ctx, row->title, ui_font_row(), GRect(UI_PAD, y, w, title_h + 4),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  if (has_sub) {
    graphics_context_set_text_color(ctx, hi ? GColorBlack : note ? UI_LINE : UI_MUTED);
    graphics_draw_text(ctx, row->sub, ui_font_small(), GRect(UI_PAD, (int16_t)(y + title_h), w, 18),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
}

static void prv_draw_row(GContext *ctx, const Layer *cell, MenuIndex *index, void *context) {
  ListData *d = context;
  if (index->row >= d->count) {
    prv_draw_footer(ctx, cell, d);
    return;
  }
  const Row *row = &d->rows[index->row];
  switch (row->type) {
    case 'b': ui_draw_row(ctx, cell, IconBook, row->title, NULL); break;
    case 'k': ui_draw_row(ctx, cell, IconTag, row->title, NULL); break;
    case 'f': {
      ui_draw_row(ctx, cell, IconTag, row->title, NULL);
      const GRect b = layer_get_bounds(cell);
      const GSize s = ui_icon_size(IconChevron);
      ui_icon_draw(ctx, IconChevron, GPoint((int16_t)(b.size.w - UI_PAD - s.w), (int16_t)((b.size.h - s.h) / 2)),
                   menu_cell_layer_is_highlighted(cell) ? GColorBlack : UI_MUTED);
      break;
    }
    default: prv_draw_recipe(ctx, cell, row); break;
  }
}

static void prv_selection_changed(MenuLayer *menu, MenuIndex new_index, MenuIndex old_index, void *context) {
  ListData *d = context;
  // Reaching the footer loads the next page.
  if (new_index.row >= d->count && d->more && !d->loading && !d->error) {
    prv_request(d, (uint16_t)(d->page + 1));
  }
}

static void prv_select(MenuLayer *menu, MenuIndex *index, void *context) {
  ListData *d = context;
  if (index->row >= d->count) {
    prv_retry(d);
    return;
  }
  const Row *row = &d->rows[index->row];
  switch (row->type) {
    case 'r': recipe_win_push(row->id, row->title, -1); break;
    case 'b': list_win_push(KIND_BOOK, row->id, row->title, NULL); break;
    case 'f': list_win_push(KIND_KEYWORDS, row->id, row->title, NULL); break;
    case 'k': list_win_push(KIND_KEYWORD, row->id, row->title, NULL); break;
    default: vibes_short_pulse(); break;
  }
}

// Answers ---------------------------------------------------------------------------------------

static ListData *prv_find(uint32_t req) {
  for (int i = 0; req && i < MAX_WINDOWS; i++) {
    if (s_lists[i] && s_lists[i]->req == req) {
      return s_lists[i];
    }
  }
  return NULL;
}

void list_win_on_items(uint32_t req, uint16_t index, const char *data, bool done, bool more, int32_t total) {
  ListData *d = prv_find(req);
  if (!d) {
    return;
  }
  const uint16_t before = d->count;
  prv_append(d, data);
  d->total = total;
  if (done) {
    d->loading = false;
    d->more = more && d->count < MAX_LIST;
    d->req = 0;
  }
  if (done || before != d->count) {
    prv_refresh(d);
  }
}

void list_win_on_error(uint32_t req, uint8_t code, const char *text) {
  ListData *d = prv_find(req);
  if (!d) {
    return;
  }
  d->loading = false;
  d->req = 0;
  d->error = code ? code : ERR_SERVER;
  ui_copy(d->error_text, sizeof(d->error_text), text);
  prv_refresh(d);
}

// Window ----------------------------------------------------------------------------------------

static void prv_load(Window *window) {
  ListData *d = window_get_user_data(window);
  Layer *root = window_get_root_layer(window);
  const GRect b = layer_get_bounds(root);
  d->status = ui_status_create(GRect(0, 0, b.size.w, UI_STATUS_H), d->title);
  const GRect body = GRect(0, UI_STATUS_H, b.size.w, b.size.h - UI_STATUS_H);
  d->menu = menu_layer_create(body);
  ui_style_menu(d->menu);
  menu_layer_set_callbacks(d->menu, d,
                           (MenuLayerCallbacks){
                               .get_num_rows = prv_rows,
                               .get_cell_height = prv_cell_height,
                               .get_header_height = prv_header_height,
                               .draw_header = prv_draw_header,
                               .draw_row = prv_draw_row,
                               .selection_changed = prv_selection_changed,
                               .select_click = prv_select,
                           });
  d->overlay = layer_create(body);
  layer_set_update_proc(d->overlay, prv_overlay_update);
  layer_add_child(root, menu_layer_get_layer(d->menu));
  layer_add_child(root, d->overlay);
  layer_add_child(root, d->status);
  prv_request(d, 1);
}

static void prv_unload(Window *window) {
  ListData *d = window_get_user_data(window);
  for (int i = 0; i < MAX_WINDOWS; i++) {
    if (s_lists[i] == d) {
      s_lists[i] = NULL;
    }
  }
  if (d->anim) {
    app_timer_cancel(d->anim);
  }
  menu_layer_destroy(d->menu);
  layer_destroy(d->overlay);
  ui_status_destroy(d->status);
  prv_free_rows(d);
  free(d);
  window_destroy(window);
}

void list_win_push(uint8_t kind, uint32_t id, const char *title, const char *query) {
  int slot = -1;
  for (int i = 0; i < MAX_WINDOWS; i++) {
    if (!s_lists[i]) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    vibes_short_pulse();
    return;
  }
  ListData *d = calloc(1, sizeof(ListData));
  if (!d) {
    return;
  }
  d->kind = kind;
  d->id = id;
  d->total = -1;
  ui_copy(d->title, sizeof(d->title), title);
  ui_copy(d->query, sizeof(d->query), query);
  s_lists[slot] = d;
  d->window = window_create();
  window_set_user_data(d->window, d);
  window_set_background_color(d->window, UI_BG);
  window_set_window_handlers(d->window, (WindowHandlers){.load = prv_load, .unload = prv_unload});
  window_stack_push(d->window, true);
}
