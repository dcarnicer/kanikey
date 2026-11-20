#pragma once
// AUD-03 app adapter: current voice setting, the account-scoped cache and the
// subject views' audio hints. The decoder/player itself lives in
// platform/AudioPlayer; screens only see an AudioHint.
#include <cstdint>
#include <vector>

#include "wk/AudioPlan.h"
#include "wk/Types.h"

namespace kk::app {

// M7: 0 female, 1 male.
uint8_t audioVoice();

void audioBegin();
void audioTick();
void audioStop();

// None for kanji/radicals; Available when a cached MP3 exists; Missing for a
// vocabulary/kana-vocabulary subject without a playable clip.
kk::AudioHint audioHintFor(const kk::Subject& subject);

// Starts the primary accepted pronunciation. No autoplay: only the explicit R
// action calls this.
bool audioPlay(const kk::Subject& subject);

// AUD-02 hook, called right after a review or lesson batch fetch succeeds, with
// that batch's subjects (their pronunciation_audios are still parsed here). It
// downloads the preferred MP3 of each vocabulary subject not on the card yet,
// within a time budget. Skipped when there is no card or token, the token is
// blocked or rate limited, Wi-Fi is down, or the largest free block is below
// the TLS need. Never OGG; no retry; no UI beyond the hint refresh.
void audioPrefetchBatch(const std::vector<kk::Subject>& subjects);

}  // namespace kk::app
