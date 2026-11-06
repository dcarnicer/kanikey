#pragma once
// Pure audio-cache index: entries, file names, LRU eviction and the on-card
// KKAU format. The clips themselves stay on the user's microSD.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace kk {

constexpr uint64_t kAudioCacheMaxBytes = 50ull * 1024ull * 1024ull;
constexpr size_t kAudioIndexMaxEntries = 2000;

enum class AudioFormat : uint8_t { Mp3 = 0, Ogg = 1 };

struct AudioEntry {
    uint32_t subjectId = 0;
    uint32_t bytes = 0;
    uint32_t usedAt = 0;
    uint8_t voice = 0;  // 0 female, 1 male
    AudioFormat format = AudioFormat::Mp3;
};

// `<subjectId>-<voice>.<mp3|ogg>`.
bool audioFileName(const AudioEntry& entry, char* out, size_t n);
AudioFormat audioFormatFromContentType(const char* contentType);

// Picks the preferred voice, then the other one. Returns false when neither is
// present.
bool audioChooseEntry(const std::vector<AudioEntry>& entries, uint32_t subjectId, uint8_t voice,
                      AudioEntry* out);

// Returns the entries to drop, oldest `usedAt` first, until `bytes + addBytes`
// fits the cap. Ties go to the lowest subject id.
std::vector<AudioEntry> audioEvict(const std::vector<AudioEntry>& entries, uint64_t bytes,
                                   uint64_t addBytes);

bool encodeAudioIndex(const std::vector<AudioEntry>& entries, uint32_t accountKey,
                      uint32_t savedAt, std::vector<uint8_t>* out);
bool decodeAudioIndex(const uint8_t* data, size_t size, uint32_t accountKey,
                      std::vector<AudioEntry>* out);

}  // namespace kk
