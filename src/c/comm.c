#include "comm.h"

#include <string.h>

#include "cook_win.h"
#include "home_win.h"
#include "list_win.h"
#include "model.h"
#include "recipe_win.h"
#include "ui.h"

// The biggest message is a STEP: up to 1800 bytes of text, a name and a few integers.
#define INBOX_SIZE 2400
#define OUTBOX_SIZE 256

#define QUEUE_LEN 4
#define MAX_RETRIES 2
#define RETRY_MS 400
// How long to wait for READY before sending anyway: a phone app that kept the JavaScript running
// from an earlier launch doesn't fire its ready event again.
#define JS_READY_WAIT_MS 6000
// PKJS gives up on Tandoor after 15 s; this covers a phone that never answers at all.
#define ANSWER_WAIT_S 25
#define WATCHDOG_MS 5000
#define MAX_PENDING 4

enum {
  CmdHello = 1,
  CmdList = 2,
  CmdRecipe = 3,
  CmdCooked = 4,
  CmdReady = 10,
  CmdStatus = 11,
  CmdSettings = 12,
  CmdItems = 13,
  CmdRecipeHead = 14,
  CmdIngredients = 15,
  CmdStep = 16,
  CmdDone = 17,
  CmdError = 18,
  CmdResult = 19,
};

typedef struct {
  uint8_t cmd;
  uint8_t kind;
  uint16_t page;
  uint32_t req;
  uint32_t id;
  int32_t servings;
  char query[QUERY_LEN];
} OutMsg;

// A request waiting for its answer, with the time it gives up.
typedef struct {
  uint32_t req;
  uint8_t cmd;
  int32_t deadline;
} Pending;

static OutMsg s_queue[QUEUE_LEN];
static uint8_t s_queue_len;
static bool s_in_flight;
static uint8_t s_retries;
static AppTimer *s_retry_timer;
static AppTimer *s_ready_timer;
static AppTimer *s_watchdog;
static bool s_js_ready;
static bool s_hello_unanswered;
static uint32_t s_next_req = 1;
static uint32_t s_recipe_req;
static Pending s_pending[MAX_PENDING];

static void prv_pump(void);

// Dictionary helpers ----------------------------------------------------------------------------

static int32_t prv_int(DictionaryIterator *iter, uint32_t key, int32_t fallback) {
  const Tuple *t = dict_find(iter, key);
  if (!t) {
    return fallback;
  }
  switch (t->type) {
    case TUPLE_INT:
      return t->length == 1 ? t->value->int8 : t->length == 2 ? t->value->int16 : t->value->int32;
    case TUPLE_UINT:
      return t->length == 1 ? t->value->uint8 : t->length == 2 ? t->value->uint16 : (int32_t)t->value->uint32;
    default:
      return fallback;
  }
}

static const char *prv_str(DictionaryIterator *iter, uint32_t key) {
  const Tuple *t = dict_find(iter, key);
  return (t && t->type == TUPLE_CSTRING) ? t->value->cstring : "";
}

// Pending answers -------------------------------------------------------------------------------

static void prv_dispatch_error(uint32_t req, uint8_t cmd, uint8_t code, const char *text) {
  switch (cmd) {
    case CmdList: list_win_on_error(req, code, text); break;
    case CmdRecipe:
      if (req == s_recipe_req) {
        model_recipe_fail(code, text);
        recipe_win_on_update();
      }
      break;
    case CmdCooked: cook_win_on_result(req, code, text); break;
    default: break;
  }
}

static void prv_pending_remove(uint32_t req) {
  for (int i = 0; i < MAX_PENDING; i++) {
    if (s_pending[i].req == req) {
      s_pending[i].req = 0;
    }
  }
}

static void prv_watchdog(void *data);

static void prv_watchdog_arm(void) {
  if (!s_watchdog) {
    s_watchdog = app_timer_register(WATCHDOG_MS, prv_watchdog, NULL);
  }
}

static void prv_pending_add(uint32_t req, uint8_t cmd) {
  int slot = 0;
  for (int i = 0; i < MAX_PENDING; i++) {
    if (s_pending[i].req == 0) {
      slot = i;
      break;
    }
    // All taken: replace the oldest.
    if (s_pending[i].req < s_pending[slot].req) {
      slot = i;
    }
  }
  s_pending[slot] = (Pending){.req = req, .cmd = cmd, .deadline = (int32_t)time(NULL) + ANSWER_WAIT_S};
  prv_watchdog_arm();
}

// Any answer shows the phone is working on it.
static void prv_pending_touch(uint32_t req) {
  for (int i = 0; i < MAX_PENDING; i++) {
    if (s_pending[i].req == req) {
      s_pending[i].deadline = (int32_t)time(NULL) + ANSWER_WAIT_S;
    }
  }
}

static const char *prv_offline_text(void) {
  return connection_service_peek_pebble_app_connection() ? "Your phone didn\u2019t answer. Try again."
                                                         : "Your phone isn\u2019t connected.";
}

static void prv_watchdog(void *data) {
  s_watchdog = NULL;
  const int32_t now = (int32_t)time(NULL);
  bool any = false;
  for (int i = 0; i < MAX_PENDING; i++) {
    if (s_pending[i].req == 0) {
      continue;
    }
    if (s_pending[i].deadline <= now) {
      const Pending p = s_pending[i];
      s_pending[i].req = 0;
      prv_dispatch_error(p.req, p.cmd, ERR_OFFLINE, prv_offline_text());
    } else {
      any = true;
    }
  }
  if (any) {
    prv_watchdog_arm();
  }
}

// Outbox ----------------------------------------------------------------------------------------

static void prv_pop(void) {
  if (s_queue_len > 0) {
    memmove(&s_queue[0], &s_queue[1], (size_t)(s_queue_len - 1) * sizeof(OutMsg));
    s_queue_len--;
  }
  s_retries = 0;
}

static void prv_fail_head(void) {
  const OutMsg head = s_queue[0];
  prv_pop();
  if (head.req) {
    prv_pending_remove(head.req);
    prv_dispatch_error(head.req, head.cmd, ERR_OFFLINE, prv_offline_text());
  }
}

static void prv_retry(void *data) {
  s_retry_timer = NULL;
  prv_pump();
}

static void prv_pump(void) {
  if (s_in_flight || s_retry_timer || s_queue_len == 0 || !s_js_ready) {
    return;
  }
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) {
    s_retry_timer = app_timer_register(RETRY_MS, prv_retry, NULL);
    return;
  }
  const OutMsg *m = &s_queue[0];
  dict_write_uint8(iter, MESSAGE_KEY_Cmd, m->cmd);
  if (m->req) {
    dict_write_int32(iter, MESSAGE_KEY_Req, (int32_t)m->req);
  }
  switch (m->cmd) {
    case CmdList:
      dict_write_uint8(iter, MESSAGE_KEY_Kind, m->kind);
      dict_write_int32(iter, MESSAGE_KEY_Id, (int32_t)m->id);
      dict_write_int32(iter, MESSAGE_KEY_Page, m->page);
      if (m->query[0]) {
        dict_write_cstring(iter, MESSAGE_KEY_Query, m->query);
      }
      break;
    case CmdRecipe: dict_write_int32(iter, MESSAGE_KEY_Id, (int32_t)m->id); break;
    case CmdCooked:
      dict_write_int32(iter, MESSAGE_KEY_Id, (int32_t)m->id);
      dict_write_int32(iter, MESSAGE_KEY_Servings, m->servings);
      break;
    default: break;
  }
  if (app_message_outbox_send() == APP_MSG_OK) {
    s_in_flight = true;
  } else {
    s_retry_timer = app_timer_register(RETRY_MS, prv_retry, NULL);
  }
}

static uint32_t prv_enqueue(OutMsg msg) {
  if (s_queue_len >= QUEUE_LEN) {
    return 0;
  }
  if (msg.cmd != CmdHello) {
    msg.req = s_next_req++;
    prv_pending_add(msg.req, msg.cmd);
  }
  s_queue[s_queue_len++] = msg;
  prv_pump();
  return msg.req;
}

static void prv_send_hello(void) {
  s_hello_unanswered = true;
  prv_enqueue((OutMsg){.cmd = CmdHello});
}

uint32_t comm_request_list(uint8_t kind, uint32_t id, uint16_t page, const char *query) {
  OutMsg msg = {.cmd = CmdList, .kind = kind, .id = id, .page = page};
  ui_copy(msg.query, sizeof(msg.query), query);
  return prv_enqueue(msg);
}

uint32_t comm_request_recipe(uint32_t id) {
  if (s_recipe_req) {
    prv_pending_remove(s_recipe_req);
  }
  s_recipe_req = prv_enqueue((OutMsg){.cmd = CmdRecipe, .id = id});
  return s_recipe_req;
}

uint32_t comm_send_cooked(uint32_t id, int32_t servings_x100) {
  return prv_enqueue((OutMsg){.cmd = CmdCooked, .id = id, .servings = servings_x100});
}

static void prv_outbox_sent(DictionaryIterator *iter, void *context) {
  s_in_flight = false;
  prv_pop();
  prv_pump();
}

static void prv_outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  s_in_flight = false;
  APP_LOG(APP_LOG_LEVEL_WARNING, "outbox failed: %d", (int)reason);
  if (++s_retries > MAX_RETRIES) {
    prv_fail_head();
    prv_pump();
    return;
  }
  s_retry_timer = app_timer_register(RETRY_MS, prv_retry, NULL);
}

// Inbox -----------------------------------------------------------------------------------------

static void prv_set_js_ready(void) {
  s_js_ready = true;
  if (s_ready_timer) {
    app_timer_cancel(s_ready_timer);
    s_ready_timer = NULL;
  }
  prv_pump();
}

static void prv_ready_timeout(void *data) {
  s_ready_timer = NULL;
  prv_set_js_ready();
}

static void prv_handle_recipe(uint8_t cmd, uint32_t req, DictionaryIterator *iter) {
  if (req != s_recipe_req) {
    return;
  }
  prv_pending_touch(req);
  switch (cmd) {
    case CmdRecipeHead:
      model_recipe_head((uint32_t)prv_int(iter, MESSAGE_KEY_Id, 0), prv_str(iter, MESSAGE_KEY_Name),
                        prv_str(iter, MESSAGE_KEY_Desc), prv_int(iter, MESSAGE_KEY_Servings, 0),
                        prv_str(iter, MESSAGE_KEY_ServText), (int16_t)prv_int(iter, MESSAGE_KEY_Time1, 0),
                        (int16_t)prv_int(iter, MESSAGE_KEY_Time2, 0), (uint8_t)prv_int(iter, MESSAGE_KEY_Rating, 0),
                        (uint8_t)prv_int(iter, MESSAGE_KEY_Count, 0), (uint8_t)prv_int(iter, MESSAGE_KEY_Total, 0),
                        prv_str(iter, MESSAGE_KEY_Sub));
      break;
    case CmdIngredients:
      model_recipe_ingredients((uint8_t)prv_int(iter, MESSAGE_KEY_Index, 0), prv_str(iter, MESSAGE_KEY_Data));
      break;
    case CmdStep:
      model_recipe_step((uint8_t)prv_int(iter, MESSAGE_KEY_Index, 0), prv_str(iter, MESSAGE_KEY_Name),
                        prv_str(iter, MESSAGE_KEY_Text), prv_int(iter, MESSAGE_KEY_Time1, 0),
                        (uint8_t)prv_int(iter, MESSAGE_KEY_Flags, 0));
      break;
    case CmdDone:
      prv_pending_remove(req);
      model_recipe_done();
      break;
    default: break;
  }
  recipe_win_on_update();
}

static void prv_inbox_received(DictionaryIterator *iter, void *context) {
  const uint8_t cmd = (uint8_t)prv_int(iter, MESSAGE_KEY_Cmd, 0);
  const uint32_t req = (uint32_t)prv_int(iter, MESSAGE_KEY_Req, 0);
  switch (cmd) {
    case CmdReady:
      prv_set_js_ready();
      // A late READY: the HELLO sent on the fallback may have gone nowhere.
      if (s_hello_unanswered) {
        prv_send_hello();
      }
      break;
    case CmdStatus:
      s_hello_unanswered = false;
      model_set_ready(prv_int(iter, MESSAGE_KEY_State, 1) == 0, prv_str(iter, MESSAGE_KEY_Text));
      home_win_on_status();
      break;
    case CmdSettings:
      model_set_settings(prv_int(iter, MESSAGE_KEY_Backlight, 1) != 0, prv_int(iter, MESSAGE_KEY_Sound, 1) != 0);
      break;
    case CmdItems: {
      const bool done = prv_int(iter, MESSAGE_KEY_Done, 1) != 0;
      if (done) {
        prv_pending_remove(req);
      } else {
        prv_pending_touch(req);
      }
      list_win_on_items(req, (uint16_t)prv_int(iter, MESSAGE_KEY_Index, 0), prv_str(iter, MESSAGE_KEY_Data), done,
                        prv_int(iter, MESSAGE_KEY_More, 0) != 0, prv_int(iter, MESSAGE_KEY_Total, -1));
      break;
    }
    case CmdRecipeHead:
    case CmdIngredients:
    case CmdStep:
    case CmdDone:
      prv_handle_recipe(cmd, req, iter);
      break;
    case CmdError:
    case CmdResult: {
      const uint8_t code = (uint8_t)prv_int(iter, MESSAGE_KEY_State, ERR_SERVER);
      const char *text = prv_str(iter, MESSAGE_KEY_Text);
      uint8_t sent = CmdList;
      for (int i = 0; i < MAX_PENDING; i++) {
        if (s_pending[i].req == req) {
          sent = s_pending[i].cmd;
        }
      }
      if (req == s_recipe_req) {
        sent = CmdRecipe;
      }
      if (cmd == CmdResult) {
        sent = CmdCooked;
      }
      prv_pending_remove(req);
      prv_dispatch_error(req, sent, code, text);
      break;
    }
    default:
      APP_LOG(APP_LOG_LEVEL_WARNING, "unknown command %d", (int)cmd);
      break;
  }
}

static void prv_inbox_dropped(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "inbox dropped: %d", (int)reason);
}

// The phone came back: its JavaScript restarts and sends READY again.
static void prv_phone_connection(bool connected) {
  if (connected) {
    s_js_ready = false;
    prv_send_hello();
    if (!s_ready_timer) {
      s_ready_timer = app_timer_register(JS_READY_WAIT_MS, prv_ready_timeout, NULL);
    }
  }
}

void comm_init(void) {
  app_message_register_inbox_received(prv_inbox_received);
  app_message_register_inbox_dropped(prv_inbox_dropped);
  app_message_register_outbox_sent(prv_outbox_sent);
  app_message_register_outbox_failed(prv_outbox_failed);
  app_message_open(INBOX_SIZE, OUTBOX_SIZE);
  connection_service_subscribe((ConnectionHandlers){.pebble_app_connection_handler = prv_phone_connection});
  s_ready_timer = app_timer_register(JS_READY_WAIT_MS, prv_ready_timeout, NULL);
  prv_send_hello();
}

void comm_deinit(void) {
  connection_service_unsubscribe();
  app_message_deregister_callbacks();
  if (s_retry_timer) {
    app_timer_cancel(s_retry_timer);
  }
  if (s_ready_timer) {
    app_timer_cancel(s_ready_timer);
  }
  if (s_watchdog) {
    app_timer_cancel(s_watchdog);
  }
}
