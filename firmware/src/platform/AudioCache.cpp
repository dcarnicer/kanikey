#include "platform/AudioCache.h"

#include <Arduino.h>
#include <WiFiClientSecure.h>

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

bool downloadClip(fs::FS& sd, const AudioEntry& entry, const String& url, String* error) {
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
    client.setHandshakeTimeout(15);
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
    uint8_t buf[4096];
    size_t total = 0;
    uint32_t lastData = millis();
    bool ok = true;
    while (total < contentLength) {
        const int avail = client.available();
        if (avail > 0) {
            const size_t want = std::min<size_t>(sizeof buf, contentLength - total);
            const int n = client.read(buf, want);
            if (n > 0) {
                if (file.write(buf, static_cast<size_t>(n)) != static_cast<size_t>(n)) {
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

bool audioCacheSyncSubjects(fs::FS& sd, const std::vector<Subject>& subjects, uint8_t voice,
                            size_t maxClips) {
    if (!gAudio.open || maxClips == 0) return false;
    size_t downloaded = 0;
    for (const Subject& subject : subjects) {
        if (downloaded >= maxClips) break;
        if (subject.type != SubjectType::Vocabulary || subject.audios.empty()) continue;
        const AudioClip* preferred = nullptr;
        for (const AudioClip& clip : subject.audios)
            if (clip.voice == voice) {
                preferred = &clip;
                break;
            }
        if (!preferred) preferred = &subject.audios[0];
        const AudioFormat format = audioFormatFromContentType(preferred->contentType.c_str());
        AudioEntry entry;
        entry.subjectId = subject.id;
        entry.voice = preferred->voice;
        entry.format = format;
        AudioEntry* existing = findEntry(subject.id, entry.voice, entry.format);
        char path[80];
        if (existing) {
            audioPath(*existing, path, sizeof path);
            if (sd.exists(path)) {
                existing->usedAt = nowSeconds();
                continue;
            }
            gAudio.entries.erase(std::remove_if(gAudio.entries.begin(), gAudio.entries.end(),
                                                [&entry](const AudioEntry& e) {
                                                    return e.subjectId == entry.subjectId &&
                                                           e.voice == entry.voice &&
                                                           e.format == entry.format;
                                                }),
                                 gAudio.entries.end());
        }
        entry.usedAt = nowSeconds();
        String error;
        if (!downloadClip(sd, entry, preferred->url.c_str(), &error)) {
            Serial.printf("[audio] %u: %s\n", static_cast<unsigned>(subject.id), error.c_str());
            continue;
        }
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
        ++downloaded;
        Serial.printf("[audio] cached %u %s (%u bytes)\n", static_cast<unsigned>(subject.id),
                      path, static_cast<unsigned>(entry.bytes));
    }
    if (downloaded) {
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
    return downloaded > 0;
}

size_t audioCacheCount() { return gAudio.open ? gAudio.entries.size() : 0; }
uint64_t audioCacheBytes() { return gAudio.open ? gAudio.bytes : 0; }

void audioCacheClear(fs::FS& sd) {
    clearDir(sd);
    Serial.println("[audio] cleared");
}

}  // namespace kk
