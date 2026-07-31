#include "sound.h"
#include "fc_config.h"

// ---------------------------------------------------------------------------
// Game audio
//
// Three sounds: the slice swish (four recordings played in rotation, so
// consecutive cuts do not sound stamped from the same die), the bomb fuse, and
// the explosion. They ship as `raw` resources -- the SDK has no audio resource
// type, and resource_generator_raw.py embeds the file byte-for-byte -- and are
// handed to speaker_play_tracks() as a SpeakerSample. That call is
// fire-and-forget, which is why it is used here rather than the
// speaker_stream_* API: streaming would need pumping from the frame timer.
//
// The clips are 16kHz 8-bit signed mono because that is the best the speaker
// takes (SpeakerPcmFormat tops out there). SPEAKER_MAX_SAMPLE_BYTES_TOTAL caps
// a single call at 16K, i.e. 1.02s, which the swishes and the fuse fit inside.
// The explosion does not: it has to keep rumbling under the game-over screen,
// so it goes through speaker_stream_* instead, fed a few kilobytes a frame from
// sound_update(). That is the one sound long enough to be worth pumping.
//
// The speaker is one voice. speaker_play_tracks() can mix up to four, but only
// within a single call -- there is no way to add a track to something already
// playing -- so this file arbitrates instead. The rules are: the explosion beats
// everything, a swish beats the fuse, and the fuse fills whatever silence is
// left while a bomb is in the air. sound_update(), polled once a frame, is what
// makes the fuse come back after a swish has stepped on it.
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

// What the speaker is busy with, so a lower-priority sound knows not to
// interrupt and so stopping the fuse cannot cut off an explosion.
typedef enum {
  VOICE_NONE = 0,
  VOICE_FUSE,
  VOICE_SLICE,
  VOICE_BOOM,
} Voice;

static const uint32_t s_clips[] = {
  RESOURCE_ID_SLICE_1,
  RESOURCE_ID_SLICE_2,
  RESOURCE_ID_SLICE_3,
  RESOURCE_ID_SLICE_4,
};

// Two buffers. The one-shots share one -- a swish and an explosion never
// overlap, since the explosion ends the run -- but the fuse needs its own: it
// loops, so it is still being read from long after the call that started it, and
// a swish loading into the same buffer would tear it.
static uint8_t s_pcm[SPEAKER_MAX_SAMPLE_BYTES_TOTAL];
static uint8_t s_fuse_pcm[SPEAKER_MAX_SAMPLE_BYTES_TOTAL];
static uint32_t s_fuse_bytes;

// The explosion is streamed rather than held whole, so it needs only a window
// into the resource. 4K is about eight frames of audio at 16kHz, which keeps the
// speaker's own buffer comfortably ahead of a 33ms tick.
#define BOOM_CHUNK_BYTES 4096
static uint8_t s_boom_chunk[BOOM_CHUNK_BYTES];
static uint32_t s_boom_pos;
static uint32_t s_boom_left;

static uint8_t s_next;
static bool s_enabled = true;
static bool s_fuse_wanted;
static Voice s_voice;

// Builds the one-note track that plays `data` at its own pitch, and starts it.
// A note is used rather than a raw buffer push because SpeakerSample is the only
// one-shot path the API offers; base and note pitch match so nothing resamples.
static void prv_play(const void *data, uint32_t bytes, bool loop,
                     uint16_t duration_ms, uint8_t volume) {
  const SpeakerSample sample = {
    .data = data,
    .num_bytes = bytes,
    .format = SpeakerPcmFormat_16kHz_8bit,
    .base_midi_note = 60,
    .loop = loop,
  };
  const SpeakerNote note = {
    .midi_note = 60,
    .waveform = 0,  // ignored: the track carries a sample
    .duration_ms = duration_ms,
    .velocity = 0,  // 0 = use the volume passed to speaker_play_tracks
    .reserved = 0,
  };
  const SpeakerTrack track = {
    .notes = &note,
    .num_notes = 1,
    .sample = &sample,
  };
  speaker_play_tracks(&track, 1, volume);
}

// A system-wide mute cannot be overridden by an app, so there is nothing to do
// but skip the work.
static bool prv_audible(void) {
  return s_enabled && !speaker_is_muted();
}

static void prv_start_fuse(void) {
  if (s_fuse_bytes == 0) {
    ResHandle h = resource_get_handle(RESOURCE_ID_BOMB_FUSE);
    s_fuse_bytes = (uint32_t)resource_load(h, s_fuse_pcm, sizeof(s_fuse_pcm));
    if (s_fuse_bytes == 0) {
      return;
    }
  }
  // Looped, and asked for far longer than any bomb survives -- a bomb is in the
  // air for about a second and a half. The fuse is stopped by hand when the
  // bomb goes, so the note length only has to outlast it.
  prv_play(s_fuse_pcm, s_fuse_bytes, true, 10000, FC_SOUND_FUSE_VOLUME);
  s_voice = VOICE_FUSE;
}

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
  if (!prv_audible() || s_voice == VOICE_BOOM) {
    return;
  }

  // Stop before loading, not after: whatever is playing may still be reading the
  // buffer we are about to overwrite. Preempting is what we want anyway -- rapid
  // cuts should sound like rapid cuts, not like a queue draining.
  speaker_stop();
  s_voice = VOICE_NONE;

  ResHandle h = resource_get_handle(s_clips[s_next]);
  s_next = (uint8_t)((s_next + 1) % ARRAY_LENGTH(s_clips));

  const size_t n = resource_load(h, s_pcm, sizeof(s_pcm));
  if (n == 0) {
    return;
  }
  prv_play(s_pcm, (uint32_t)n, false, (uint16_t)(n / SOUND_BYTES_PER_MS),
           FC_SOUND_VOLUME);
  s_voice = VOICE_SLICE;
}

// Pushes as much of the explosion into the speaker as it will take. Advancing by
// what speaker_stream_write() actually accepted is what paces this: once its
// buffer is full the write comes up short and the rest waits for the next frame.
static void prv_pump_boom(void) {
  ResHandle h = resource_get_handle(RESOURCE_ID_BOMB_BOOM);

  while (s_boom_left > 0) {
    const uint32_t want = (s_boom_left < BOOM_CHUNK_BYTES) ? s_boom_left
                                                           : BOOM_CHUNK_BYTES;
    const size_t got = resource_load_byte_range(h, s_boom_pos, s_boom_chunk, want);
    if (got == 0) {
      s_boom_left = 0;  // resource went away; close out rather than spin
      break;
    }
    const uint32_t wrote = speaker_stream_write(s_boom_chunk, (uint32_t)got);
    s_boom_pos += wrote;
    s_boom_left -= wrote;
    if (wrote < got) {
      return;  // speaker is full; pick this up next frame
    }
  }
  // Everything is handed over. Closing lets what is buffered play out; the
  // status stays non-idle until it has, which is what keeps the fuse off it.
  speaker_stream_close();
}

void sound_play_explosion(void) {
  // The fuse belongs to the bomb that just went off, so it never outlives it.
  s_fuse_wanted = false;
  if (!prv_audible()) {
    return;
  }

  speaker_stop();
  s_voice = VOICE_NONE;

  ResHandle h = resource_get_handle(RESOURCE_ID_BOMB_BOOM);
  const size_t total = resource_size(h);
  if (total == 0 ||
      !speaker_stream_open(SpeakerPcmFormat_16kHz_8bit, FC_SOUND_VOLUME)) {
    return;
  }
  s_boom_pos = 0;
  s_boom_left = (uint32_t)total;
  s_voice = VOICE_BOOM;
  prv_pump_boom();  // prime it now; waiting a frame would clip the transient
}

void sound_set_fuse(bool on) {
  if (on == s_fuse_wanted) {
    return;
  }
  s_fuse_wanted = on;
  // Only silence the fuse itself. A swish or an explosion that happens to be
  // sounding as the last bomb leaves has to be allowed to finish.
  if (!on && s_voice == VOICE_FUSE) {
    speaker_stop();
    s_voice = VOICE_NONE;
  }
}

void sound_update(void) {
  // A stream that has not been handed over yet owns the speaker outright: its
  // status reads idle until the first bytes land, which would otherwise look
  // like a free voice and let the fuse start on top of the explosion.
  if (s_boom_left > 0) {
    prv_pump_boom();
    return;
  }

  if (speaker_get_status() == SpeakerStatusIdle) {
    s_voice = VOICE_NONE;
  }
  // Claim the silence left by a finished swish, or by the loop running out of
  // note. Polling beats speaker_set_finish_callback here: the frame timer is
  // already running, and a callback that restarts playback would be re-entered
  // by its own speaker_stop().
  if (s_fuse_wanted && s_voice == VOICE_NONE && prv_audible()) {
    prv_start_fuse();
  }
}

void sound_stop(void) {
  // Close before stopping: close alone would let the buffered tail play on,
  // which is exactly what a retry is trying to get rid of.
  if (s_boom_left > 0) {
    s_boom_left = 0;
    speaker_stream_close();
  }
  speaker_stop();
  s_voice = VOICE_NONE;
}
