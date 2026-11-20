#pragma once
// AUD-03 app adapter: current voice setting, the account-scoped cache and the
// subject views' audio hints. The decoder/player itself lives in
// platform/AudioPlayer; screens only see an AudioHint.
#include <cstdint>

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

}  // namespace kk::app
