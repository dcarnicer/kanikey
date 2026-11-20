#include "platform/AudioCache.h"

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstring>
#include <ctime>

#include "Version.h"
#include "platform/RootCerts.h"
#include "wk/Crc32.h"

namespace kk {

namespace {
constexpr const char* kDir = "/kanikey/audio";
constexpr const char* kIndexPath = "/kanikey/audio/index.bin";
constexpr const char* kIndexTmp = "/kanikey/audio/index.tmp";
constexpr size_t kMaxClipBytes = 5u * 1024u * 1024u;

struct AudioState {
    bool open = false;
    uint32_t accountKey = 0;
    uint32_t savedAt = 0;
    std::vector<AudioEntry> entries;
    uint64_t bytes = 0;
};

AudioState gAudio;

uint32_t nowSeconds() {
    const uint32_t now = static_cast<uint32_t>(time(nullptr));
    return now >= 1700000000u ? now : 0;
}

void audioPath(const AudioEntry& entry, char* out, size_t n) {
    char name[48];
    if (!audioFileName(entry, name, sizeof name)) {
        if (n) out[0] = 0;
        return;
    }
    snprintf(out, n, "%s/%s", kDir, name);
}

AudioEntry* findEntry(uint32_t subjectId, uint8_t voice, AudioFormat format) {
    for (AudioEntry& entry : gAudio.entries)
        if (entry.subjectId == subjectId && entry.voice == voice && entry.format == format)
            return &entry;
    return nullptr;
}

void recountBytes() {
    gAudio.bytes = 0;
    for (const AudioEntry& entry : gAudio.entries) gAudio.bytes += entry.bytes;
}

bool writeIndex(fs::FS& sd) {
    std::vector<uint8_t> bytes;
    if (!encodeAudioIndex(gAudio.entries, gAudio.accountKey, nowSeconds(), &bytes)) return false;
    File file = sd.open(kIndexTmp, "w");
    if (!file) return false;
    const bool ok = file.write(bytes.data(), bytes.size()) == bytes.size();
    file.close();
    if (!ok) {
        sd.remove(kIndexTmp);
        return false;
    }
    if (sd.exists(kIndexPath)) sd.remove(kIndexPath);
    if (!sd.rename(kIndexTmp, kIndexPath)) {
        sd.remove(kIndexTmp);
        return false;
    }
    gAudio.savedAt = nowSeconds();
    return true;
}

bool readIndex(fs::FS& sd) {
    File file = sd.open(kIndexPath, "r");
    if (!file) return false;
    const size_t size = static_cast<size_t>(file.size());
    if (size < 20 || size > 16 + kAudioIndexMaxEntries * 16 + 4) {
        file.close();
        return false;
    }
    std::vector<uint8_t> bytes(size);
    size_t got = 0;
    while (got < size) {
        const int n = file.read(bytes.data() + got, size - got);
        if (n <= 0) break;
        got += static_cast<size_t>(n);
    }
    file.close();
    if (got != size || !decodeAudioIndex(bytes.data(), bytes.size(), gAudio.accountKey,
                                         &gAudio.entries))
        return false;
    recountBytes();
    return true;
}

void clearDir(fs::FS& sd) {
    File dir = sd.open(kDir);
    if (dir) {
        while (true) {
            File entry = dir.openNextFile();
            if (!entry) break;
            if (!entry.isDirectory()) sd.remove(entry.path());
            entry.close();
        }
        dir.close();
    }
    if (sd.exists(kIndexPath)) sd.remove(kIndexPath);
    if (sd.exists(kIndexTmp)) sd.remove(kIndexTmp);
    gAudio = AudioState{};
}

bool parseHttpsUrl(const String& url, String* host, String* path) {
    if (!url.startsWith("https://")) return false;
    const int slash = url.indexOf('/', 8);
    if (slash < 0) return false;
    *host = url.substring(8, slash);
    *path = url.substring(slash);
    return host->length() > 0 && path->length() > 1;
}

bool downloadClip(fs::FS& sd, const AudioEntry& entry, const String& url, uint32_t handshakeSec,
                  uint8_t* chunk, size_t chunkBytes, String* error) {
    String host;
    String path;
    if (!parseHttpsUrl(url, &host, &path)) {
        if (error) *error = "bad audio URL";
        return false;
    }
    char finalPath[80];
    char tmpPath[84];
    audioPath(entry, finalPath, sizeof finalPath);
    snprintf(tmpPath, sizeof tmpPath, "%s.tmp", finalPath);
    if (sd.exists(tmpPath)) sd.remove(tmpPath);

    WiFiClientSecure client;
    client.setCACert(tgc::kRootCaPem);
    client.setHandshakeTimeout(handshakeSec);
    if (!client.connect(host.c_str(), 443)) {
        if (error) *error = "audio connect";
        return false;
    }
    client.print("GET ");
    client.print(path);
    client.print(" HTTP/1.0\r\nHost: ");
    client.print(host);
    client.print("\r\nUser-Agent: KaniKey/");
    client.print(kk::kFirmwareVersion);
    client.print(" (unofficial WaniKani client)\r\nConnection: close\r\n\r\n");

    String status = client.readStringUntil('\n');
    status.trim();
    if (!status.startsWith("HTTP/")) {
        client.stop();
        if (error) *error = "audio status";
        return false;
    }
    const int code = status.substring(status.indexOf(' ') + 1, status.indexOf(' ') + 4).toInt();
    size_t contentLength = 0;
    while (client.connected() || client.available()) {
        String line = client.readStringUntil('\n');
        if (line == "\r" || line.isEmpty()) break;
        String lower = line;
        lower.toLowerCase();
        if (lower.startsWith("content-length:"))
            contentLength = static_cast<size_t>(lower.substring(15).toInt());
    }
    if (code != 200 || contentLength == 0 || contentLength > kMaxClipBytes) {
        client.stop();
        if (error) *error = "audio HTTP " + String(code);
        return false;
    }
    File file = sd.open(tmpPath, "w");
    if (!file) {
        client.stop();
        if (error) *error = "audio SD write";
        return false;
    }
    size_t total = 0;
    uint32_t lastData = millis();
    bool ok = true;
    while (total < contentLength) {
        const int avail = client.available();
        if (avail > 0) {
            const size_t want = std::min<size_t>(chunkBytes, contentLength - total);
            const int n = client.read(chunk, want);
            if (n > 0) {
                if (file.write(chunk, static_cast<size_t>(n)) != static_cast<size_t>(n)) {
                    ok = false;
                    break;
                }
                total += static_cast<size_t>(n);
                lastData = millis();
                continue;
            }
        }
        if (!client.connected() && !client.available()) break;
        if (millis() - lastData > 5000) break;
        delay(2);
    }
    file.close();
    client.stop();
    if (!ok || total != contentLength) {
        sd.remove(tmpPath);
        if (error) *error = "audio short";
        return false;
    }
    if (sd.exists(finalPath)) sd.remove(finalPath);
    if (!sd.rename(tmpPath, finalPath)) {
        sd.remove(tmpPath);
        if (error) *error = "audio rename";
        return false;
    }
    return true;
}
}  // namespace

bool audioCacheOpen(fs::FS& sd, uint32_t accountKey) {
    if (accountKey == 0) return false;
    if (gAudio.open && gAudio.accountKey == accountKey) return true;
    gAudio = AudioState{};
    gAudio.accountKey = accountKey;
    sd.mkdir("/kanikey");
    sd.mkdir(kDir);
    if (readIndex(sd)) {
        gAudio.open = true;
        Serial.printf("[audio] %u clips, %u bytes\n",
                      static_cast<unsigned>(gAudio.entries.size()),
                      static_cast<unsigned>(gAudio.bytes));
        return true;
    }
    clearDir(sd);
    gAudio.accountKey = accountKey;
    gAudio.open = true;
    writeIndex(sd);
    Serial.println("[audio] cache created");
    return true;
}

constexpr size_t kClipChunkBytes = 4096;

AudioPrefetchStats audioCachePrefetch(fs::FS& sd, const std::vector<Subject>& subjects,
                                      uint8_t voice, uint32_t budgetMs, size_t minLargestHeap) {
    AudioPrefetchStats stats;
    const uint32_t start = millis();
    if (!gAudio.open) return stats;

    // Wanted: the MP3 clip of each vocabulary subject that is not on the card
    // yet. OGG-only subjects are skipped; the pointers stay inside `subjects`.
    struct Want {
        const Subject* subject;
        size_t clip;
    };
    std::vector<Want> wanted;
    for (const Subject& subject : subjects) {
        if (subject.type != SubjectType::Vocabulary) continue;
        size_t index = 0;
        if (!audioPickDownload(subject.audios, voice, &index)) continue;
        const AudioClip& clip = subject.audios[index];
        if (AudioEntry* existing = findEntry(subject.id, clip.voice, AudioFormat::Mp3)) {
            char path[80];
            audioPath(*existing, path, sizeof path);
            if (sd.exists(path)) {
                existing->usedAt = nowSeconds();
                continue;
            }
        }
        wanted.push_back({&subject, index});
    }
    stats.wanted = wanted.size();

    // The 4 KB stream chunk is one heap allocation per batch, not a stack array.
    uint8_t* chunk = nullptr;
    if (!wanted.empty()) {
        chunk = static_cast<uint8_t*>(heap_caps_malloc(kClipChunkBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        if (!chunk) stats.stopped = "heap";
    }

    for (const Want& w : wanted) {
        if (!chunk) break;
        const uint32_t elapsed = millis() - start;
        if (elapsed >= budgetMs) {
            stats.stopped = "budget";
            break;
        }
        if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) <
            minLargestHeap) {
            stats.stopped = "heap";
            break;
        }
        const AudioClip& clip = w.subject->audios[w.clip];
        AudioEntry entry;
        entry.subjectId = w.subject->id;
        entry.voice = clip.voice;
        entry.format = AudioFormat::Mp3;
        // A missing file left an index entry behind: drop it before the new one.
        gAudio.entries.erase(std::remove_if(gAudio.entries.begin(), gAudio.entries.end(),
                                            [&entry](const AudioEntry& e) {
                                                return e.subjectId == entry.subjectId &&
                                                       e.voice == entry.voice &&
                                                       e.format == entry.format;
                                            }),
                             gAudio.entries.end());
        entry.usedAt = nowSeconds();
        // The handshake gets the rest of the budget (at least 2 s); the body
        // reads keep their own 5 s stall limit.
        const uint32_t remainingSec = std::max<uint32_t>(2, (budgetMs - elapsed) / 1000);
        String error;
        if (!downloadClip(sd, entry, String(clip.url.c_str()), remainingSec, chunk,
                          kClipChunkBytes, &error)) {
            Serial.printf("[audio] %u: %s\n", static_cast<unsigned>(entry.subjectId), error.c_str());
            continue;
        }
        char path[80];
        audioPath(entry, path, sizeof path);
        File file = sd.open(path, "r");
        entry.bytes = file ? static_cast<uint32_t>(file.size()) : 0;
        if (file) file.close();
        if (entry.bytes == 0) {
            sd.remove(path);
            continue;
        }
        gAudio.entries.push_back(entry);
        gAudio.bytes += entry.bytes;
        stats.bytes += entry.bytes;
        ++stats.downloaded;
        Serial.printf("[audio] cached %u %s (%u bytes)\n", static_cast<unsigned>(entry.subjectId),
                      path, static_cast<unsigned>(entry.bytes));
    }
    if (chunk) heap_caps_free(chunk);
    if (stats.downloaded) {
        while (gAudio.bytes > kAudioCacheMaxBytes) {
            std::vector<AudioEntry> victims = audioEvict(gAudio.entries, gAudio.bytes, 0);
            if (victims.empty()) break;
            const AudioEntry victim = victims.front();
            char path[80];
            audioPath(victim, path, sizeof path);
            sd.remove(path);
            gAudio.bytes = gAudio.bytes > victim.bytes ? gAudio.bytes - victim.bytes : 0;
            gAudio.entries.erase(std::remove_if(gAudio.entries.begin(), gAudio.entries.end(),
                                                [&victim](const AudioEntry& e) {
                                                    return e.subjectId == victim.subjectId &&
                                                           e.voice == victim.voice &&
                                                           e.format == victim.format;
                                                }),
                                 gAudio.entries.end());
            Serial.printf("[audio] evicted %u\n", static_cast<unsigned>(victim.subjectId));
        }
        writeIndex(sd);
    }
    stats.ms = millis() - start;
    Serial.printf("[audio] batch prefetch %u/%u clips, %u bytes, %u ms%s%s%s\n",
                  static_cast<unsigned>(stats.downloaded), static_cast<unsigned>(stats.wanted),
                  static_cast<unsigned>(stats.bytes), static_cast<unsigned>(stats.ms),
                  stats.stopped ? " (stopped: " : "", stats.stopped ? stats.stopped : "",
                  stats.stopped ? ")" : "");
    return stats;
}

size_t audioCacheCount() { return gAudio.open ? gAudio.entries.size() : 0; }
uint64_t audioCacheBytes() { return gAudio.open ? gAudio.bytes : 0; }

AudioPlan audioCachePlan(uint32_t subjectId, SubjectType type, uint8_t voice) {
    if (!gAudio.open) {
        AudioPlan plan;
        plan.applicable = audioSubjectPlayable(type);
        return plan;
    }
    return planAudio(gAudio.entries, type, subjectId, voice);
}

bool audioCachePath(const AudioEntry& entry, char* out, size_t n) {
    if (!out || n == 0) return false;
    audioPath(entry, out, n);
    return out[0] != 0;
}

void audioCacheClear(fs::FS& sd) {
    clearDir(sd);
    Serial.println("[audio] cleared");
}

}  // namespace kk
