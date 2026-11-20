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
// queues the chosen MP3 of each vocabulary subject not on the card yet (the URL
// is copied into the queue's fixed storage; bounded, deduped by subject and
// voice). It downloads nothing by itself unless `drainNow`: the review hook
// passes false, because the heap is short right after a large fetch; the lesson
// hook passes true and drains inline within 8 s under the same link and heap
// rules. Otherwise audioTick() drains the queue one clip at a time on an idle
// Home. Never OGG; a clip gives up after two failures.
void audioPrefetchBatch(const std::vector<kk::Subject>& subjects, bool drainNow = false);

// AUD-02 refill: the next audioTick() queues again the playable subjects of the
// batch in RAM that have no clip on the card. Called after a batch loads from the
// card, since its URLs are not in the queue after a restart or a cache wipe. A
// new clip cache asks by itself. Costs nothing until the batch is in RAM.
void audioRequestRefill();

// M6 autoplay. A request is made at a reading point (a checked reading answer
// on review feedback, or a lesson card's first draw for its subject). Does
// nothing while M6 is Off. audioTick() starts the clip once the answer cue has
// finished and every rule in wk/AudioPlan audioAutoplayWanted() still holds; it
// tries once and never retries a refused start.
void audioAutoplayRequest(const kk::Subject& subject);

// Lesson card: called from the card's draw. Requests autoplay on the first draw
// of each card with a reading during a lesson session (the cards and their
// quiz); paging back to a card does not repeat it. In memory only; the draw
// does no I/O.
void audioLessonCardShown(const kk::Subject& subject, bool hasReading);

// Drops a pending autoplay without starting it. Any key calls this.
void audioAutoplayCancel();

}  // namespace kk::app
