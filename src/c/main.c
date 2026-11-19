#include <pebble.h>

#include "comm.h"
#include "home_win.h"
#include "model.h"
#include "timers.h"
#include "ui.h"

static void prv_init(void) {
  model_init();
  // Swipes on menus and scrolling text act as button presses where the user turned touch on.
  app_touch_navigation_enable(true);
  home_win_push();
  comm_init();
  // Last: a Wakeup launch or a timer that ended while closed opens the alert over Home.
  timers_init();
}

static void prv_deinit(void) {
  timers_deinit();
  comm_deinit();
  model_deinit();
  ui_deinit();
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
