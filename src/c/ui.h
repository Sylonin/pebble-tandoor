#pragma once

#include <pebble.h>

// Charcoal and flame, after Tandoor's logo.
#define UI_BG GColorBlack
#define UI_FG GColorWhite
#define UI_ACCENT GColorChromeYellow
#define UI_HOT GColorOrange
#define UI_MUTED GColorLightGray
#define UI_LINE GColorDarkGray
#define UI_DONE GColorMayGreen

#define UI_PAD 8
#define UI_STATUS_H 22

typedef enum {
  IconPlan,
  IconSearch,
  IconRecent,
  IconBook,
  IconTag,
  IconAll,
  IconRandom,
  IconTimer,
  IconResume,
  IconChevron,
  IconClock,
  IconUsers,
  IconPlay,
  IconCheckOn,
  IconCheckOff,
  IconPause,
  IconPlaySmall,
  IconPlus,
  IconDismiss,
  IconStep,
  IconBell,
  IconDone,
  IconSetup,
  IconOffline,
  IconEmpty,
  IconCount,
} IconId;

void ui_deinit(void);

GFont ui_font_title(void);       // Gothic 24 Bold
GFont ui_font_big_title(void);   // Gothic 28 Bold
GFont ui_font_row(void);         // Gothic 18 Bold
GFont ui_font_text(void);        // Gothic 18
GFont ui_font_body(void);        // Gothic 24
GFont ui_font_small(void);       // Gothic 14
GFont ui_font_small_bold(void);  // Gothic 14 Bold
GFont ui_font_digits(void);      // LECO 20 Bold numbers
GFont ui_font_digits_big(void);  // LECO 36 Bold numbers

// Draws an icon as a mask in `color`, keeping its antialiasing.
void ui_icon_draw(GContext *ctx, IconId id, GPoint origin, GColor color);
GSize ui_icon_size(IconId id);

int16_t ui_text_height(const char *text, GFont font, int16_t width, int16_t max_height);

// "4:05", "1:02:03"; negative values get a leading "+" (time past the end).
void ui_format_clock(char *buf, size_t size, int32_t seconds);
// "25 min", "1 h", "1 h 10 min".
void ui_format_minutes(char *buf, size_t size, int32_t minutes);
// "45 s", "10 min", "1 h 30 min"; "1:30" when not whole minutes.
void ui_format_duration(char *buf, size_t size, int32_t seconds);
// An amount given ×1000: "2", "1½", "⅓" as "1/3", "0.15", "250".
void ui_format_amount(char *buf, size_t size, int32_t milli);
// Copies with UTF-8-safe truncation.
void ui_copy(char *dst, size_t size, const char *src);

// Three pulsing dots centred in `rect`; `frame` advances the pulse.
void ui_draw_loading(GContext *ctx, GRect rect, uint8_t frame);
// A centred card: a large icon, a title and a sentence.
void ui_draw_message(GContext *ctx, GRect rect, IconId icon, GColor icon_color, const char *title, const char *body);
// Menu row: optional icon at left, title, optional subtitle.
void ui_draw_row(GContext *ctx, const Layer *cell, IconId icon, const char *title, const char *subtitle);
// Small-caps-style section header with a hairline.
void ui_draw_section_header(GContext *ctx, const Layer *cell, const char *text);
void ui_style_menu(MenuLayer *menu);

// Status bar: title at left, the clock or the next timer at right. One per window.
Layer *ui_status_create(GRect frame, const char *title);
void ui_status_set_title(Layer *layer, const char *title);
void ui_status_destroy(Layer *layer);
// Redraws every status bar (called from the timers' tick).
void ui_status_refresh_all(void);
