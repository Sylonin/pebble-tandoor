#include "alert.h"

#include "model.h"

#define ROUND_MS 4000
#define MAX_ROUNDS 30

// A kitchen timer's "ding ding ding".
static const SpeakerNote CHIME[] = {
    {.midi_note = 88, .waveform = SpeakerWaveformTriangle, .duration_ms = 140},
    {.midi_note = 0, .duration_ms = 70},
    {.midi_note = 88, .waveform = SpeakerWaveformTriangle, .duration_ms = 140},
    {.midi_note = 0, .duration_ms = 70},
    {.midi_note = 91, .waveform = SpeakerWaveformTriangle, .duration_ms = 420},
};

static const uint32_t PULSES[] = {350, 150, 350, 150, 350};

static AppTimer *s_timer;
static uint8_t s_rounds_left;

static void prv_round(void *data) {
  s_timer = NULL;
  if (s_rounds_left == 0) {
    return;
  }
  s_rounds_left--;
  light_enable_interaction();
  vibes_enqueue_custom_pattern((VibePattern){.durations = PULSES, .num_segments = ARRAY_LENGTH(PULSES)});
  // The watch mutes the speaker itself when the user asked for it (always, or in Quiet Time).
  if (model_settings()->sound && !speaker_is_muted()) {
    speaker_play_notes(CHIME, ARRAY_LENGTH(CHIME), 80);
  }
  s_timer = app_timer_register(ROUND_MS, prv_round, NULL);
}

void alert_play(void) {
  if (s_timer) {
    return;
  }
  s_rounds_left = MAX_ROUNDS;
  prv_round(NULL);
}

void alert_stop(void) {
  if (s_timer) {
    app_timer_cancel(s_timer);
    s_timer = NULL;
    vibes_cancel();
    speaker_stop();
  }
  s_rounds_left = 0;
}
