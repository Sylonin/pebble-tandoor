#pragma once

#include <pebble.h>

// List kinds (docs/design.md, List kinds).
#define KIND_PLAN 1
#define KIND_RECENT 2
#define KIND_ALL 3
#define KIND_BOOKS 4
#define KIND_BOOK 5
#define KIND_KEYWORDS 6
#define KIND_KEYWORD 7
#define KIND_SEARCH 8

// Error codes in ERROR and RESULT.
#define ERR_SETUP 1
#define ERR_OFFLINE 2
#define ERR_AUTH 3
#define ERR_NOT_FOUND 4
#define ERR_SERVER 5
#define ERR_VOICE 6

void comm_init(void);
void comm_deinit(void);

// Each returns the request number its answers carry, or 0 when the queue is full.
uint32_t comm_request_list(uint8_t kind, uint32_t id, uint16_t page, const char *query);
uint32_t comm_request_recipe(uint32_t id);
uint32_t comm_send_cooked(uint32_t id, int32_t servings_x100);
