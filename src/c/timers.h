#pragma once

#include <pebble.h>

#include "model.h"

#define MAX_TIMERS 6

typedef enum {
  TimerIdle = 0,
  TimerRunning,
  TimerPaused,
  TimerRinging,
} TimerState;

typedef struct {
  uint32_t recipe_id;
  uint8_t step;
  uint8_t state;
  int32_t duration;
  int32_t end;   // running and ringing
  int32_t left;  // paused
  char label[TIMER_LABEL_LEN];
} Timer;

typedef void (*TimersObserver)(void);

// Loads the timers and starts the tick. Timers that ended while the app was closed start ringing.
void timers_init(void);
// Schedules a Wakeup for the next running timer.
void timers_deinit(void);

const Timer *timers_get(int slot);
// Active slots, ringing first, then running by end, then paused. Returns the count.
uint8_t timers_sorted(uint8_t out[MAX_TIMERS]);
int timers_find(uint32_t recipe_id, uint8_t step);
// Returns the slot, or -1 when all slots are taken.
int timers_start(uint32_t recipe_id, uint8_t step, int32_t seconds, const char *label);
void timers_toggle(int slot);
void timers_add_minute(int slot);
void timers_restart(int slot);
void timers_cancel(int slot);
// Seconds left; negative once ringing.
int32_t timers_left(int slot);
// The timer to show in the status bar: a ringing one, else the soonest running one; -1 for none.
int timers_next(int32_t *left, bool *ringing);
int timers_first_ringing(void);

// Called every second while a timer runs, and on every change.
void timers_observe(TimersObserver observer);
void timers_unobserve(TimersObserver observer);
