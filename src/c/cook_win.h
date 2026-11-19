#pragma once

#include <pebble.h>

// Cooks the model's recipe from `step`.
void cook_win_push(uint8_t step);
// Answer to COOKED: state 0 is success, otherwise an error code.
void cook_win_on_result(uint32_t req, uint8_t state, const char *text);
// Closes the cooking window, if open, without animation (its recipe is about to change).
void cook_win_close(void);
