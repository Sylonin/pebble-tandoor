#pragma once

#include <pebble.h>

// Vibrates and chimes every few seconds for up to two minutes, or until alert_stop().
void alert_play(void);
void alert_stop(void);
