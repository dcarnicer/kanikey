#include "app/AudioController.h"

#include <SD.h>

#include "app/AppState.h"
#include "app/CacheController.h"
#include "platform/AudioPlayer.h"
#include "platform/DeviceSettings.h"
#include "platform/DevToken.h"

namespace kk::app {

uint8_t audioVoice() {
    int voice = 0;
    if (kk::settingsGetInt("rm.voice", &voice) && voice == 1) return 1;
    return 0;
}

void audioBegin() { kk::audioPlayerBegin(SD); }

void audioTick() { kk::audioPlayerTick(); }

void audioStop() { kk::audioPlayerStop(); }

kk::AudioHint audioHintFor(const kk::Subject& subject) {
    if (!kk::audioSubjectPlayable(subject.type)) return kk::AudioHint::None;
    if (!state().storage.gSdOk || !kk::DevToken::has()) return kk::AudioHint::Missing;
    return kk::audioPlayerHint(currentAccountKey(), subject, audioVoice());
}

bool audioPlay(const kk::Subject& subject) {
    if (!kk::audioSubjectPlayable(subject.type)) return false;
    if (!state().storage.gSdOk || !kk::DevToken::has()) return false;
    const bool started = kk::audioPlayerPlay(currentAccountKey(), subject, audioVoice());
    state().display.gDirty = true;
    return started;
}

}  // namespace kk::app
