#include "sound.h"
#include "fc_config.h"

// ---------------------------------------------------------------------------
// Slice audio
//
// Four recorded swishes played in rotation, so consecutive cuts do not sound
// stamped from the same die. They ship as `raw` resources -- the SDK has no
// audio resource type, and resource_generator_raw.py embeds the file
// byte-for-byte -- and are handed to speaker_play_tracks() as a one-shot
// SpeakerSample. That call is fire-and-forget, which is why it is used here
// rather than the speaker_stream_* API: streaming would need pumping from the
// frame timer for something that is over in two thirds of a second.
//
// The clips are 16kHz 8-bit signed mono because that is the best the speaker
// takes (SpeakerPcmFormat tops out there), and each is under
// SPEAKER_MAX_SAMPLE_BYTES_TOTAL, which is the hard cap on a single call.
//
// The speaker_* calls are deliberately unguarded. They exist only on emery,
// gabbro and flint; on the older platforms pebble.h stubs them to (0), so a
// guard here would trade a loud compile error for a silent game. targetPlatforms
// is already pinned to the two touch platforms, both of which have a speaker.
// ---------------------------------------------------------------------------

// Key 5. Keys 1 through 4 belong to game.c (difficulty, then one high score per
// difficulty).
#define PERSIST_KEY_SOUND 5

// 16kHz, one byte per sample.
#define SOUND_BYTES_PER_MS 16

static const uint32_t s_clips[] = {
  RESOURCE_ID_SLICE_1,
  RESOURCE_ID_SLICE_2,
  RESOURCE_ID_SLICE_3,
  RESOURCE_ID_SLICE_4,
};

// One buffer, reused. Holding all four resident would cost ~43K of the 128K app
// RAM budget to save a flash read that takes well under a frame.
static uint8_t s_pcm[SPEAKER_MAX_SAMPLE_BYTES_TOTAL];
static uint8_t s_next;
static bool s_enabled = true;

void sound_init(void) {
  // Stored as a 1-based flag for the same reason game.c stores difficulty+1:
  // persist_read_int cannot tell a key holding 0 from one never written, and an
  // unwritten key has to mean sound on.
  const int32_t saved = persist_read_int(PERSIST_KEY_SOUND);
  s_enabled = (saved == 0) ? true : (saved == 2);
}

bool sound_is_enabled(void) { return s_enabled; }

void sound_set_enabled(bool on) {
  s_enabled = on;
  persist_write_int(PERSIST_KEY_SOUND, on ? 2 : 1);
  if (!on) {
    sound_stop();
  }
}

void sound_play_slice(void) {
  // A system-wide mute cannot be overridden by an app, so there is nothing to
  // do but skip the work.
  if (!s_enabled || speaker_is_muted()) {
    return;
  }

  // Stop before loading, not after: the previous swish may still be reading the
  // buffer we are about to overwrite. Preempting it is what we want anyway --
  // rapid cuts should sound like rapid cuts, not like a queue draining.
  speaker_stop();

  ResHandle h = resource_get_handle(s_clips[s_next]);
  s_next = (uint8_t)((s_next + 1) % ARRAY_LENGTH(s_clips));

  const size_t n = resource_load(h, s_pcm, sizeof(s_pcm));
  if (n == 0) {
    return;
  }

  const SpeakerSample sample = {
    .data = s_pcm,
    .num_bytes = (uint32_t)n,
    .format = SpeakerPcmFormat_16kHz_8bit,
    .base_midi_note = 60,
    .loop = false,
  };
  // One note at the sample's own pitch, lasting exactly as long as the sample,
  // so nothing is resampled and no silence is padded onto the end.
  const SpeakerNote note = {
    .midi_note = 60,
    .waveform = 0,  // ignored: the track carries a sample
    .duration_ms = (uint16_t)(n / SOUND_BYTES_PER_MS),
    .velocity = 0,  // 0 = use the volume passed to speaker_play_tracks
    .reserved = 0,
  };
  const SpeakerTrack track = {
    .notes = &note,
    .num_notes = 1,
    .sample = &sample,
  };
  speaker_play_tracks(&track, 1, FC_SOUND_VOLUME);
}

void sound_stop(void) {
  speaker_stop();
}
