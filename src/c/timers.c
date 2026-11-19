#include "timers.h"

#include <string.h>

#include "alert_win.h"
#include "glance.h"
#include "ui.h"

#define KEY_TIMER_BASE 10
#define MAX_OBSERVERS 6
// A timer left ringing this long (the app closed under it) is dropped on the next launch.
#define STALE_RINGING (10 * 60)
#define MAX_SECONDS (24 * 60 * 60)

_Static_assert(sizeof(Timer) <= PERSIST_DATA_MAX_LENGTH, "Timer fits one persist key");

static Timer s_timers[MAX_TIMERS];
static TimersObserver s_observers[MAX_OBSERVERS];
static TimeUnits s_tick_unit;

static void prv_tick(struct tm *tick_time, TimeUnits units_changed);

static bool prv_valid(int slot) {
  return slot >= 0 && slot < MAX_TIMERS && s_timers[slot].state != TimerIdle;
}

static void prv_save(int slot) {
  if (s_timers[slot].state == TimerIdle) {
    persist_delete(KEY_TIMER_BASE + slot);
  } else {
    persist_write_data(KEY_TIMER_BASE + slot, &s_timers[slot], sizeof(Timer));
  }
}

static void prv_notify(void) {
  for (int i = 0; i < MAX_OBSERVERS; i++) {
    if (s_observers[i]) {
      s_observers[i]();
    }
  }
  ui_status_refresh_all();
}

// Seconds ticks while anything counts down; minute ticks keep the status bar clock right.
static void prv_update_tick(void) {
  TimeUnits unit = MINUTE_UNIT;
  for (int i = 0; i < MAX_TIMERS; i++) {
    if (s_timers[i].state == TimerRunning || s_timers[i].state == TimerRinging) {
      unit = SECOND_UNIT;
      break;
    }
  }
  if (unit != s_tick_unit) {
    s_tick_unit = unit;
    tick_timer_service_subscribe(unit, prv_tick);
  }
}

static void prv_changed(int slot) {
  prv_save(slot);
  prv_update_tick();
  prv_notify();
}

// Moves running timers that reached their end to ringing; returns whether any did.
static bool prv_check_ends(void) {
  const int32_t now = (int32_t)time(NULL);
  bool rang = false;
  for (int i = 0; i < MAX_TIMERS; i++) {
    if (s_timers[i].state == TimerRunning && s_timers[i].end <= now) {
      s_timers[i].state = TimerRinging;
      prv_save(i);
      rang = true;
    }
  }
  return rang;
}

static void prv_tick(struct tm *tick_time, TimeUnits units_changed) {
  if (prv_check_ends()) {
    prv_update_tick();
    alert_win_show();
  }
  prv_notify();
}

void timers_init(void) {
  const int32_t now = (int32_t)time(NULL);
  for (int i = 0; i < MAX_TIMERS; i++) {
    if (persist_read_data(KEY_TIMER_BASE + i, &s_timers[i], sizeof(Timer)) != (int)sizeof(Timer) ||
        s_timers[i].state > TimerRinging) {
      memset(&s_timers[i], 0, sizeof(Timer));
      continue;
    }
    if (s_timers[i].state != TimerPaused && s_timers[i].end < now - STALE_RINGING) {
      s_timers[i].state = TimerIdle;
      prv_save(i);
    }
  }
  // While the app runs the tick fires the timers; a Wakeup would only relaunch it.
  wakeup_cancel_all();
  s_tick_unit = SECOND_UNIT;
  tick_timer_service_subscribe(SECOND_UNIT, prv_tick);
  prv_check_ends();
  prv_update_tick();
  if (timers_first_ringing() >= 0) {
    alert_win_show();
  }
}

void timers_deinit(void) {
  tick_timer_service_unsubscribe();
  int next = -1;
  for (int i = 0; i < MAX_TIMERS; i++) {
    if (s_timers[i].state == TimerRunning && (next < 0 || s_timers[i].end < s_timers[next].end)) {
      next = i;
    }
  }
  glance_update(next >= 0 ? &s_timers[next] : NULL);
  if (next < 0) {
    return;
  }
  // The system keeps a minute between Wakeups of all apps. Early is better than late: the app then
  // shows the timer counting and rings on time.
  static const int16_t OFFSETS[] = {0, -15, -30, -45, -60, 15, 30, 45, 60};
  const time_t now = time(NULL);
  for (size_t i = 0; i < ARRAY_LENGTH(OFFSETS); i++) {
    const time_t at = (time_t)s_timers[next].end + OFFSETS[i];
    if (at <= now) {
      continue;
    }
    const WakeupId id = wakeup_schedule(at, next, true);
    if (id >= 0) {
      return;
    }
    if (id != E_RANGE) {
      APP_LOG(APP_LOG_LEVEL_WARNING, "wakeup_schedule failed: %d", (int)id);
      return;
    }
  }
}

const Timer *timers_get(int slot) {
  return prv_valid(slot) ? &s_timers[slot] : NULL;
}

static int prv_rank(int slot) {
  switch ((TimerState)s_timers[slot].state) {
    case TimerRinging: return 0;
    case TimerRunning: return 1;
    default: return 2;
  }
}

static int32_t prv_sort_key(int slot) {
  return s_timers[slot].state == TimerPaused ? s_timers[slot].left : s_timers[slot].end;
}

uint8_t timers_sorted(uint8_t out[MAX_TIMERS]) {
  uint8_t n = 0;
  for (int i = 0; i < MAX_TIMERS; i++) {
    if (s_timers[i].state != TimerIdle) {
      out[n++] = (uint8_t)i;
    }
  }
  // Insertion sort; six entries at most.
  for (uint8_t i = 1; i < n; i++) {
    const uint8_t v = out[i];
    int j = i - 1;
    while (j >= 0 && (prv_rank(out[j]) > prv_rank(v) ||
                      (prv_rank(out[j]) == prv_rank(v) && prv_sort_key(out[j]) > prv_sort_key(v)))) {
      out[j + 1] = out[j];
      j--;
    }
    out[j + 1] = v;
  }
  return n;
}

int timers_find(uint32_t recipe_id, uint8_t step) {
  for (int i = 0; i < MAX_TIMERS; i++) {
    if (s_timers[i].state != TimerIdle && s_timers[i].recipe_id == recipe_id && s_timers[i].step == step) {
      return i;
    }
  }
  return -1;
}

int timers_start(uint32_t recipe_id, uint8_t step, int32_t seconds, const char *label) {
  int slot = timers_find(recipe_id, step);
  for (int i = 0; slot < 0 && i < MAX_TIMERS; i++) {
    if (s_timers[i].state == TimerIdle) {
      slot = i;
    }
  }
  if (slot < 0) {
    return -1;
  }
  if (seconds > MAX_SECONDS) {
    seconds = MAX_SECONDS;
  }
  Timer *t = &s_timers[slot];
  t->recipe_id = recipe_id;
  t->step = step;
  t->state = TimerRunning;
  t->duration = seconds;
  t->end = (int32_t)time(NULL) + seconds;
  t->left = seconds;
  ui_copy(t->label, sizeof(t->label), label);
  prv_changed(slot);
  return slot;
}

void timers_toggle(int slot) {
  if (!prv_valid(slot)) {
    return;
  }
  Timer *t = &s_timers[slot];
  const int32_t now = (int32_t)time(NULL);
  if (t->state == TimerRunning) {
    t->left = t->end - now;
    t->state = TimerPaused;
  } else if (t->state == TimerPaused) {
    t->end = now + t->left;
    t->state = TimerRunning;
  } else {
    return;
  }
  prv_changed(slot);
}

void timers_add_minute(int slot) {
  if (!prv_valid(slot)) {
    return;
  }
  Timer *t = &s_timers[slot];
  const int32_t now = (int32_t)time(NULL);
  switch ((TimerState)t->state) {
    case TimerRinging:
      t->state = TimerRunning;
      t->end = now + 60;
      break;
    case TimerRunning: t->end += 60; break;
    case TimerPaused: t->left += 60; break;
    case TimerIdle: return;
  }
  prv_changed(slot);
}

void timers_restart(int slot) {
  if (!prv_valid(slot)) {
    return;
  }
  Timer *t = &s_timers[slot];
  t->state = TimerRunning;
  t->end = (int32_t)time(NULL) + t->duration;
  prv_changed(slot);
}

void timers_cancel(int slot) {
  if (!prv_valid(slot)) {
    return;
  }
  s_timers[slot].state = TimerIdle;
  prv_changed(slot);
}

int32_t timers_left(int slot) {
  if (!prv_valid(slot)) {
    return 0;
  }
  const Timer *t = &s_timers[slot];
  return t->state == TimerPaused ? t->left : t->end - (int32_t)time(NULL);
}

int timers_first_ringing(void) {
  int best = -1;
  for (int i = 0; i < MAX_TIMERS; i++) {
    if (s_timers[i].state == TimerRinging && (best < 0 || s_timers[i].end < s_timers[best].end)) {
      best = i;
    }
  }
  return best;
}

int timers_next(int32_t *left, bool *ringing) {
  int best = timers_first_ringing();
  *ringing = best >= 0;
  for (int i = 0; !*ringing && i < MAX_TIMERS; i++) {
    if (s_timers[i].state == TimerRunning && (best < 0 || s_timers[i].end < s_timers[best].end)) {
      best = i;
    }
  }
  if (best >= 0) {
    *left = timers_left(best);
  }
  return best;
}

void timers_observe(TimersObserver observer) {
  for (int i = 0; i < MAX_OBSERVERS; i++) {
    if (s_observers[i] == observer) {
      return;
    }
  }
  for (int i = 0; i < MAX_OBSERVERS; i++) {
    if (!s_observers[i]) {
      s_observers[i] = observer;
      return;
    }
  }
}

void timers_unobserve(TimersObserver observer) {
  for (int i = 0; i < MAX_OBSERVERS; i++) {
    if (s_observers[i] == observer) {
      s_observers[i] = NULL;
    }
  }
}
