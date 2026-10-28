#pragma once
// KaniKey's first-boot setup portal, adapted from Tangocho's WifiPortal but
// reduced to setup: home Wi-Fi, WaniKani token validation, browser time/zone
// and a visual-pack check.
//
// Two modes:
//   - Ap: the device starts a WPA2 network "KaniKey-XXXX" and serves the page
//     at http://192.168.4.1/. The page saves the home Wi-Fi.
//   - Station: the device has joined the home network and serves the page at
//     its station IP (shown on the device screen). The token is validated with
//     GET /user and the browser sends the clock/time zone.
//
// The portal never logs or shows the Wi-Fi password or the token. Arduino/
// ESP32 only; not part of the native test build.
#include <Arduino.h>
#include <WebServer.h>

#include <vector>

namespace kk {

struct SetupPortalHooks {
    // Checks GET /user with `token`; fills the user name/level or `error`.
    bool (*validateToken)(const String& token, String* username, int* level,
                          String* error) = nullptr;
    // Re-opens the SD pack after the user inserts/changes the card.
    bool (*recheckPack)() = nullptr;
    // gPack.statusLine(), for the page and the device screen.
    String (*packStatus)() = nullptr;
    // The AP page saved the home Wi-Fi: stop the AP, join it, then beginStation.
    void (*onSwitchToStation)() = nullptr;
    // The station page finished setup: stop the portal and start the app.
    void (*onFinish)() = nullptr;
};

class SetupPortal {
public:
    enum class Mode : uint8_t { Ap, Station };

    SetupPortal() = default;
    ~SetupPortal() { end(false); }
    SetupPortal(const SetupPortal&) = delete;
    SetupPortal& operator=(const SetupPortal&) = delete;

    bool beginAp(const SetupPortalHooks& hooks, const String& notice = String());
    bool beginStation(const SetupPortalHooks& hooks, const String& notice = String());
    void poll();
    void end(bool keepStation);

    bool active() const { return active_; }
    Mode mode() const { return mode_; }
    const char* ssid() const { return ssid_; }
    const char* apPassword() const { return apPassword_; }
    const char* url() const { return url_; }
    const char* stationIp() const { return stationIp_; }
    const char* message() const { return message_; }
    bool tokenValidated() const { return tokenValidated_; }
    const char* tokenUser() const { return tokenUser_.c_str(); }
    int tokenLevel() const { return tokenLevel_; }
    uint32_t lastActivityMs() const { return lastActivity_; }
    String packStatus() const;

private:
    enum class Leave : uint8_t { None, SwitchStation, Finish };

    bool startServer(const SetupPortalHooks& hooks);
    void registerRoutes();
    void handleRoot();
    void handleWifi();
    void handleToken();
    void handlePack();
    void handleTime();
    void handleFinish();
    void handleNotFound();
    void sendPage(const String& notice, bool ok, int code = 200);
    String timeScript() const;
    void touch() { lastActivity_ = millis(); }

    WebServer* server_ = nullptr;
    SetupPortalHooks hooks_{};
    bool active_ = false;
    Mode mode_ = Mode::Ap;
    Leave leave_ = Leave::None;
    uint32_t leaveAt_ = 0;
    uint32_t lastActivity_ = 0;
    char ssid_[33] = "";
    char apPassword_[20] = "";
    char url_[40] = "";
    char stationIp_[20] = "";
    char message_[96] = "ready";
    String tokenUser_;
    int tokenLevel_ = 0;
    bool tokenValidated_ = false;
    bool timeSet_ = false;
    String timeText_;
    std::vector<String> nearby_;
};

}  // namespace kk
