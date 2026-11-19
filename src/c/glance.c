#include "glance.h"

// The template string may not exceed 150 bytes.
#define TEMPLATE_MAX 150

// Appends src without the template's braces, stopping at max bytes without splitting a UTF-8 sequence.
static size_t prv_escape_append(char *dst, size_t used, size_t max, const char *src) {
  for (; *src; src++) {
    const char c = *src == '{' ? '(' : *src == '}' ? ')' : *src;
    size_t need = 1;
    if (((uint8_t)c & 0xE0) == 0xC0) {
      need = 2;
    } else if (((uint8_t)c & 0xF0) == 0xE0) {
      need = 3;
    } else if (((uint8_t)c & 0xF8) == 0xF0) {
      need = 4;
    } else if (((uint8_t)c & 0xC0) == 0x80) {
      need = 0;  // a continuation byte; its lead byte already reserved room for it
    }
    // `used >= max` also stops a stray continuation byte, which reserved nothing.
    if (used >= max || used + need > max) {
      break;
    }
    dst[used++] = c;
  }
  dst[used] = '\0';
  return used;
}

static void prv_reload(AppGlanceReloadSession *session, size_t limit, void *context) {
  const Timer *next = context;
  char text[TEMPLATE_MAX + 1];
  // The countdown leads, so the launcher's ellipsis cuts the label instead. It keeps its braces.
  const int n = snprintf(text, sizeof(text), "{time_until(%ld)|format(>0S:'%%aT left','Time is up')} \u00b7 ",
                         (long)next->end);
  if (n > 0 && n < (int)sizeof(text)) {
    prv_escape_append(text, (size_t)n, TEMPLATE_MAX, next->label);
  }
  const AppGlanceSlice slice = {
      .layout = {.icon = APP_GLANCE_SLICE_DEFAULT_ICON, .subtitle_template_string = text},
      .expiration_time = next->end + 10 * 60,
  };
  const AppGlanceResult result = app_glance_add_slice(session, slice);
  if (result != APP_GLANCE_RESULT_SUCCESS) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "glance slice rejected: %d", (int)result);
  }
}

void glance_update(const Timer *next) {
  // The callback runs before app_glance_reload returns, so the timer pointer stays valid.
  app_glance_reload(next ? prv_reload : NULL, (void *)next);
}
