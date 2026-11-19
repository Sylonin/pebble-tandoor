#include "ui.h"

#include <string.h>

#include "timers.h"

#define MAX_STATUS 8

static const uint32_t ICON_RESOURCES[IconCount] = {
    [IconPlan] = RESOURCE_ID_ICON_PLAN,
    [IconSearch] = RESOURCE_ID_ICON_SEARCH,
    [IconRecent] = RESOURCE_ID_ICON_RECENT,
    [IconBook] = RESOURCE_ID_ICON_BOOK,
    [IconTag] = RESOURCE_ID_ICON_TAG,
    [IconAll] = RESOURCE_ID_ICON_ALL,
    [IconRandom] = RESOURCE_ID_ICON_RANDOM,
    [IconTimer] = RESOURCE_ID_ICON_TIMER,
    [IconResume] = RESOURCE_ID_ICON_RESUME,
    [IconChevron] = RESOURCE_ID_ICON_CHEVRON,
    [IconClock] = RESOURCE_ID_ICON_CLOCK,
    [IconUsers] = RESOURCE_ID_ICON_USERS,
    [IconPlay] = RESOURCE_ID_ICON_PLAY,
    [IconCheckOn] = RESOURCE_ID_ICON_CHECK_ON,
    [IconCheckOff] = RESOURCE_ID_ICON_CHECK_OFF,
    [IconPause] = RESOURCE_ID_ICON_PAUSE,
    [IconPlaySmall] = RESOURCE_ID_ICON_PLAY_SMALL,
    [IconPlus] = RESOURCE_ID_ICON_PLUS,
    [IconDismiss] = RESOURCE_ID_ICON_DISMISS,
    [IconStep] = RESOURCE_ID_ICON_STEP,
    [IconBell] = RESOURCE_ID_ICON_BELL,
    [IconDone] = RESOURCE_ID_ICON_DONE,
    [IconSetup] = RESOURCE_ID_ICON_SETUP,
    [IconOffline] = RESOURCE_ID_ICON_OFFLINE,
    [IconEmpty] = RESOURCE_ID_ICON_EMPTY,
};

static GBitmap *s_icons[IconCount];
static Layer *s_status[MAX_STATUS];

typedef struct {
  const char *title;
} StatusData;

// Fonts ---------------------------------------------------------------------------------------

GFont ui_font_title(void) {
  return fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
}

GFont ui_font_big_title(void) {
  return fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
}

GFont ui_font_row(void) {
  return fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
}

GFont ui_font_text(void) {
  return fonts_get_system_font(FONT_KEY_GOTHIC_18);
}

GFont ui_font_body(void) {
  return fonts_get_system_font(FONT_KEY_GOTHIC_24);
}

GFont ui_font_small(void) {
  return fonts_get_system_font(FONT_KEY_GOTHIC_14);
}

GFont ui_font_small_bold(void) {
  return fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
}

GFont ui_font_digits(void) {
  return fonts_get_system_font(FONT_KEY_LECO_20_BOLD_NUMBERS);
}

GFont ui_font_digits_big(void) {
  return fonts_get_system_font(FONT_KEY_LECO_36_BOLD_NUMBERS);
}

// Icons ---------------------------------------------------------------------------------------

static GBitmap *prv_icon(IconId id) {
  if (id >= IconCount) {
    return NULL;
  }
  if (!s_icons[id]) {
    s_icons[id] = gbitmap_create_with_resource(ICON_RESOURCES[id]);
  }
  return s_icons[id];
}

GSize ui_icon_size(IconId id) {
  GBitmap *bmp = prv_icon(id);
  return bmp ? gbitmap_get_bounds(bmp).size : GSize(0, 0);
}

void ui_icon_draw(GContext *ctx, IconId id, GPoint origin, GColor color) {
  GBitmap *bmp = prv_icon(id);
  if (!bmp) {
    return;
  }
  int entries = 0;
  switch (gbitmap_get_format(bmp)) {
    case GBitmapFormat1BitPalette: entries = 2; break;
    case GBitmapFormat2BitPalette: entries = 4; break;
    case GBitmapFormat4BitPalette: entries = 16; break;
    default: break;
  }
  GColor *palette = gbitmap_get_palette(bmp);
  for (int i = 0; palette && i < entries; i++) {
    // Keep the entry's alpha (the top two bits), take the colour from `color`.
    palette[i].argb = (uint8_t)((palette[i].argb & 0xC0) | (color.argb & 0x3F));
  }
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, bmp, GRect(origin.x, origin.y, gbitmap_get_bounds(bmp).size.w,
                                               gbitmap_get_bounds(bmp).size.h));
  graphics_context_set_compositing_mode(ctx, GCompOpAssign);
}

// Text and numbers ----------------------------------------------------------------------------

int16_t ui_text_height(const char *text, GFont font, int16_t width, int16_t max_height) {
  if (!text || !text[0]) {
    return 0;
  }
  // Ellipsis mode stops at the box, so `max_height` caps the lines the way drawing will.
  return graphics_text_layout_get_content_size(text, font, GRect(0, 0, width, max_height),
                                               GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft)
      .h;
}

void ui_copy(char *dst, size_t size, const char *src) {
  if (size == 0) {
    return;
  }
  size_t n = src ? strlen(src) : 0;
  if (n >= size) {
    n = size - 1;
    // Back off to the start of a UTF-8 sequence.
    while (n > 0 && ((uint8_t)src[n] & 0xC0) == 0x80) {
      n--;
    }
  }
  if (n) {
    memcpy(dst, src, n);
  }
  dst[n] = '\0';
}

void ui_format_clock(char *buf, size_t size, int32_t seconds) {
  const char *sign = "";
  if (seconds < 0) {
    sign = "+";
    seconds = -seconds;
  }
  const int32_t h = seconds / 3600;
  const int32_t m = (seconds / 60) % 60;
  const int32_t s = seconds % 60;
  if (h > 0) {
    snprintf(buf, size, "%s%d:%02d:%02d", sign, (int)h, (int)m, (int)s);
  } else {
    snprintf(buf, size, "%s%d:%02d", sign, (int)m, (int)s);
  }
}

void ui_format_minutes(char *buf, size_t size, int32_t minutes) {
  if (minutes < 60) {
    snprintf(buf, size, "%d min", (int)minutes);
  } else if (minutes % 60 == 0) {
    snprintf(buf, size, "%d h", (int)(minutes / 60));
  } else {
    snprintf(buf, size, "%d h %d min", (int)(minutes / 60), (int)(minutes % 60));
  }
}

void ui_format_duration(char *buf, size_t size, int32_t seconds) {
  if (seconds < 60) {
    snprintf(buf, size, "%d s", (int)seconds);
  } else if (seconds % 60 == 0) {
    ui_format_minutes(buf, size, seconds / 60);
  } else {
    ui_format_clock(buf, size, seconds);
  }
}

typedef struct {
  int16_t milli;
  const char *text;
} Fraction;

// Kitchen fractions, matched within FRACTION_SLACK thousandths.
static const Fraction FRACTIONS[] = {
    {125, "1/8"}, {250, "\u00bc"}, {333, "1/3"}, {500, "\u00bd"}, {667, "2/3"}, {750, "\u00be"},
};
#define FRACTION_SLACK 20

void ui_format_amount(char *buf, size_t size, int32_t milli) {
  if (milli < 0) {
    milli = 0;
  }
  int32_t whole = milli / 1000;
  const int32_t rest = milli % 1000;
  if (whole >= 10) {
    // Big amounts (250 g) don't need fractions.
    snprintf(buf, size, "%d", (int)((milli + 500) / 1000));
    return;
  }
  if (rest <= FRACTION_SLACK || rest >= 1000 - FRACTION_SLACK) {
    snprintf(buf, size, "%d", (int)((milli + 500) / 1000));
    return;
  }
  for (size_t i = 0; i < ARRAY_LENGTH(FRACTIONS); i++) {
    const int32_t diff = rest - FRACTIONS[i].milli;
    if (diff <= FRACTION_SLACK && diff >= -FRACTION_SLACK) {
      const bool ascii = FRACTIONS[i].text[0] != '\xc2';
      if (whole == 0) {
        snprintf(buf, size, "%s", FRACTIONS[i].text);
      } else {
        // "1 1/3" needs a space; "1½" reads better without.
        snprintf(buf, size, ascii ? "%d %s" : "%d%s", (int)whole, FRACTIONS[i].text);
      }
      return;
    }
  }
  // Anything else: one decimal, two below 1 (0.15 tsp).
  if (whole == 0 && rest < 100) {
    snprintf(buf, size, "0.%02d", (int)((rest + 5) / 10));
    return;
  }
  int32_t tenths = (rest + 50) / 100;
  if (tenths == 10) {
    whole++;
    tenths = 0;
  }
  if (tenths == 0) {
    snprintf(buf, size, "%d", (int)whole);
  } else {
    snprintf(buf, size, "%d.%d", (int)whole, (int)tenths);
  }
}

// Drawing -------------------------------------------------------------------------------------

void ui_draw_loading(GContext *ctx, GRect rect, uint8_t frame) {
  const int16_t cx = rect.origin.x + rect.size.w / 2;
  const int16_t cy = rect.origin.y + rect.size.h / 2;
  graphics_context_set_antialiased(ctx, true);
  for (int i = 0; i < 3; i++) {
    const bool lit = (frame % 3) == i;
    graphics_context_set_fill_color(ctx, lit ? UI_ACCENT : UI_LINE);
    graphics_fill_circle(ctx, GPoint((int16_t)(cx + (i - 1) * 16), cy), lit ? 5 : 4);
  }
}

void ui_draw_message(GContext *ctx, GRect rect, IconId icon, GColor icon_color, const char *title, const char *body) {
  const int16_t w = rect.size.w - 2 * UI_PAD;
  const GSize isz = ui_icon_size(icon);
  const int16_t title_h = ui_text_height(title, ui_font_title(), w, 60);
  const int16_t body_h = ui_text_height(body, ui_font_text(), w, 100);
  const int16_t total = isz.h + 8 + title_h + body_h;
  int16_t y = rect.origin.y + (rect.size.h - total) / 2;
  if (y < rect.origin.y + 4) {
    y = rect.origin.y + 4;
  }
  ui_icon_draw(ctx, icon, GPoint((int16_t)(rect.origin.x + (rect.size.w - isz.w) / 2), y), icon_color);
  y += isz.h + 6;
  graphics_context_set_text_color(ctx, UI_FG);
  graphics_draw_text(ctx, title, ui_font_title(), GRect(rect.origin.x + UI_PAD, y, w, title_h + 4),
                     GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  y += title_h + 2;
  if (body && body[0]) {
    graphics_context_set_text_color(ctx, UI_MUTED);
    graphics_draw_text(ctx, body, ui_font_text(), GRect(rect.origin.x + UI_PAD, y, w, body_h + 4),
                       GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }
}

void ui_style_menu(MenuLayer *menu) {
  menu_layer_set_normal_colors(menu, UI_BG, UI_FG);
  menu_layer_set_highlight_colors(menu, UI_ACCENT, GColorBlack);
}

void ui_draw_row(GContext *ctx, const Layer *cell, IconId icon, const char *title, const char *subtitle) {
  const GRect b = layer_get_bounds(cell);
  const bool hi = menu_cell_layer_is_highlighted(cell);
  int16_t x = UI_PAD;
  if (icon < IconCount) {
    const GSize isz = ui_icon_size(icon);
    ui_icon_draw(ctx, icon, GPoint(x, (int16_t)((b.size.h - isz.h) / 2)), hi ? GColorBlack : UI_ACCENT);
    x += isz.w + UI_PAD;
  }
  const int16_t w = b.size.w - x - UI_PAD;
  const bool has_sub = subtitle && subtitle[0];
  // One line when a subtitle follows; rows are sized for that.
  const int16_t title_h = ui_text_height(title, ui_font_row(), w, has_sub ? 22 : 44);
  const int16_t sub_h = has_sub ? 16 : 0;
  // Gothic glyphs sit low in their line box; lift the block a little.
  int16_t y = (int16_t)((b.size.h - title_h - sub_h) / 2 - 3);
  graphics_context_set_text_color(ctx, hi ? GColorBlack : UI_FG);
  graphics_draw_text(ctx, title, ui_font_row(), GRect(x, y, w, title_h + 2), GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);
  if (has_sub) {
    graphics_context_set_text_color(ctx, hi ? GColorBlack : UI_MUTED);
    graphics_draw_text(ctx, subtitle, ui_font_small(), GRect(x, (int16_t)(y + title_h), w, 18),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
}

void ui_draw_section_header(GContext *ctx, const Layer *cell, const char *text) {
  const GRect b = layer_get_bounds(cell);
  graphics_context_set_fill_color(ctx, UI_BG);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_text_color(ctx, UI_ACCENT);
  graphics_draw_text(ctx, text, ui_font_small_bold(), GRect(UI_PAD, b.size.h - 19, b.size.w - 2 * UI_PAD, 18),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  graphics_context_set_stroke_color(ctx, UI_LINE);
  graphics_draw_line(ctx, GPoint(UI_PAD, b.size.h - 1), GPoint(b.size.w - UI_PAD, b.size.h - 1));
}

// Status bar ----------------------------------------------------------------------------------

static void prv_status_update(Layer *layer, GContext *ctx) {
  const GRect b = layer_get_bounds(layer);
  const StatusData *data = layer_get_data(layer);
  graphics_context_set_fill_color(ctx, UI_BG);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  char right[16];
  int16_t right_w = 0;
  int32_t left = 0;
  bool ringing = false;
  if (timers_next(&left, &ringing) >= 0) {
    if (ringing) {
      snprintf(right, sizeof(right), "Done");
    } else {
      ui_format_clock(right, sizeof(right), left);
    }
    const GSize tsz = graphics_text_layout_get_content_size(right, ui_font_small_bold(), GRect(0, 0, 80, 20),
                                                            GTextOverflowModeFill, GTextAlignmentLeft);
    const GSize isz = ui_icon_size(IconClock);
    right_w = (int16_t)(tsz.w + isz.w + 14);
    const GRect pill = GRect(b.size.w - right_w - 4, 3, right_w, b.size.h - 6);
    graphics_context_set_fill_color(ctx, UI_HOT);
    graphics_fill_rect(ctx, pill, (uint16_t)(pill.size.h / 2), GCornersAll);
    ui_icon_draw(ctx, IconClock, GPoint(pill.origin.x + 5, (int16_t)(pill.origin.y + (pill.size.h - isz.h) / 2)),
                 GColorBlack);
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, right, ui_font_small_bold(),
                       GRect(pill.origin.x + 5 + isz.w + 3, pill.origin.y - 2, tsz.w + 2, pill.size.h),
                       GTextOverflowModeFill, GTextAlignmentLeft, NULL);
    right_w += 4;
  } else {
    clock_copy_time_string(right, sizeof(right));
    right_w = 60;
    graphics_context_set_text_color(ctx, UI_FG);
    graphics_draw_text(ctx, right, ui_font_small_bold(), GRect(b.size.w - right_w - UI_PAD, 1, right_w, 18),
                       GTextOverflowModeFill, GTextAlignmentRight, NULL);
    right_w += UI_PAD;
  }

  if (data->title) {
    graphics_context_set_text_color(ctx, UI_ACCENT);
    graphics_draw_text(ctx, data->title, ui_font_small_bold(), GRect(UI_PAD, 1, b.size.w - UI_PAD - right_w - 4, 18),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
}

Layer *ui_status_create(GRect frame, const char *title) {
  Layer *layer = layer_create_with_data(frame, sizeof(StatusData));
  StatusData *data = layer_get_data(layer);
  data->title = title;
  layer_set_update_proc(layer, prv_status_update);
  for (int i = 0; i < MAX_STATUS; i++) {
    if (!s_status[i]) {
      s_status[i] = layer;
      break;
    }
  }
  return layer;
}

void ui_status_set_title(Layer *layer, const char *title) {
  StatusData *data = layer_get_data(layer);
  data->title = title;
  layer_mark_dirty(layer);
}

void ui_status_destroy(Layer *layer) {
  for (int i = 0; i < MAX_STATUS; i++) {
    if (s_status[i] == layer) {
      s_status[i] = NULL;
    }
  }
  layer_destroy(layer);
}

void ui_status_refresh_all(void) {
  for (int i = 0; i < MAX_STATUS; i++) {
    if (s_status[i]) {
      layer_mark_dirty(s_status[i]);
    }
  }
}

// Lifecycle -----------------------------------------------------------------------------------

void ui_deinit(void) {
  for (int i = 0; i < IconCount; i++) {
    if (s_icons[i]) {
      gbitmap_destroy(s_icons[i]);
      s_icons[i] = NULL;
    }
  }
}
