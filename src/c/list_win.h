#pragma once

#include <pebble.h>

// Opens a list of `kind` (KIND_* in comm.h). `title` and `query` are copied.
void list_win_push(uint8_t kind, uint32_t id, const char *title, const char *query);
void list_win_on_items(uint32_t req, uint16_t index, const char *data, bool done, bool more, int32_t total);
void list_win_on_error(uint32_t req, uint8_t code, const char *text);
