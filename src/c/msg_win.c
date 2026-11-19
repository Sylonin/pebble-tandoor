#include "msg_win.h"

typedef struct {
  IconId icon;
  char title[40];
  char body[120];
  Layer *status;
  Layer *layer;
} MsgData;

static void prv_update(Layer *layer, GContext *ctx) {
  const MsgData *data = window_get_user_data(layer_get_window(layer));
  ui_draw_message(ctx, layer_get_bounds(layer), data->icon, UI_ACCENT, data->title, data->body);
}

static void prv_load(Window *window) {
  MsgData *data = window_get_user_data(window);
  Layer *root = window_get_root_layer(window);
  const GRect b = layer_get_bounds(root);
  data->status = ui_status_create(GRect(0, 0, b.size.w, UI_STATUS_H), NULL);
  data->layer = layer_create(GRect(0, UI_STATUS_H, b.size.w, b.size.h - UI_STATUS_H));
  layer_set_update_proc(data->layer, prv_update);
  layer_add_child(root, data->status);
  layer_add_child(root, data->layer);
}

static void prv_unload(Window *window) {
  MsgData *data = window_get_user_data(window);
  ui_status_destroy(data->status);
  layer_destroy(data->layer);
  free(data);
  window_destroy(window);
}

void msg_win_push(IconId icon, const char *title, const char *body) {
  MsgData *data = calloc(1, sizeof(MsgData));
  if (!data) {
    return;
  }
  data->icon = icon;
  ui_copy(data->title, sizeof(data->title), title);
  ui_copy(data->body, sizeof(data->body), body);
  Window *window = window_create();
  window_set_user_data(window, data);
  window_set_background_color(window, UI_BG);
  window_set_window_handlers(window, (WindowHandlers){.load = prv_load, .unload = prv_unload});
  window_stack_push(window, true);
}
