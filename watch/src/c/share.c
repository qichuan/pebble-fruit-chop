#include "share.h"

// Three integers out, nothing in. The inbox is opened at the minimum the SDK
// will take rather than zero, because a phone that acks a message still needs
// somewhere to put it.
#define SHARE_OUTBOX_SIZE 64
#define SHARE_INBOX_SIZE 64

void share_init(void) {
  const AppMessageResult res = app_message_open(SHARE_INBOX_SIZE, SHARE_OUTBOX_SIZE);
  if (res != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "share: app_message_open failed (%d)", (int)res);
  }
}

void share_report_run(int score, int difficulty, int best) {
  DictionaryIterator *iter;
  const AppMessageResult res = app_message_outbox_begin(&iter);
  if (res != APP_MSG_OK) {
    // Nothing is retried: the next run will send its own result, and a score
    // the phone missed is not worth carrying state around for.
    APP_LOG(APP_LOG_LEVEL_DEBUG, "share: outbox unavailable (%d)", (int)res);
    return;
  }

  dict_write_int32(iter, MESSAGE_KEY_SCORE, (int32_t)score);
  dict_write_int32(iter, MESSAGE_KEY_DIFF, (int32_t)difficulty);
  dict_write_int32(iter, MESSAGE_KEY_BEST, (int32_t)best);
  app_message_outbox_send();
}
