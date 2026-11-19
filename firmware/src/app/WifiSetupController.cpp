// SETUP-09: on-device Wi-Fi setup. The scan list is filtered to 2.4 GHz, the
// password is kept in RAM only, and DevWifi's non-saving join runs before the
// credentials are written. The phone portal remains the token/time path.
#include "app/WifiSetupController.h"

#include <Arduino.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "app/AppState.h"
#include "app/AppUi.h"
#include "app/SetupController.h"
#include "app/WifiSetupScreen.h"
#include "board/Board.h"
#include "platform/DeviceSettings.h"
#include "platform/DevWifi.h"
#include "ui/KeyInput.h"
#include "ui/Theme.h"

namespace kk::app {
namespace {

using tgc::Key;
using tgc::KeyEvent;
using namespace kk::ui;

constexpr size_t kMaxScanEntries = 32;

void clearPassword(WifiState& wifi) {
    if (!wifi.gWifiPassword.empty())
        std::fill(wifi.gWifiPassword.begin(), wifi.gWifiPassword.end(), '\0');
    wifi.gWifiPassword.clear();
}

void setNote(WifiState& wifi, const char* text, uint16_t color = kFaint) {
    snprintf(wifi.gWifiNote, sizeof wifi.gWifiNote, "%s", text ? text : "");
    wifi.gWifiNoteColor = color;
}

void listNote(WifiState& wifi) {
    if (wifi.gWifiNetworks.empty())
        setNote(wifi, "No 2.4 GHz networks; R: rescan", kFaint);
    else
        setNote(wifi, "", kFaint);
}

void setFailureNote(WifiState& wifi, kk::WifiJoinFailure failure) {
    const char* reason = kk::wifiFailureLabel(failure);
    char text[64];
    switch (failure) {
        case kk::WifiJoinFailure::NotFound:
            snprintf(text, sizeof text, "%s; check the name", reason);
            break;
        case kk::WifiJoinFailure::WrongPassword:
        case kk::WifiJoinFailure::Timeout:
        case kk::WifiJoinFailure::SaveFailed:
            snprintf(text, sizeof text, "%s; try again", reason);
            break;
        case kk::WifiJoinFailure::Unsupported:
            snprintf(text, sizeof text, "%s; use phone setup", reason);
            break;
        case kk::WifiJoinFailure::None:
            text[0] = '\0';
            break;
    }
    setNote(wifi, text, kErr);
}

void scanNetworks(WifiState& wifi) {
    drawLoading("Scanning Wi-Fi...", "2.4 GHz networks");
    kk::WifiNetwork entries[kMaxScanEntries];
    const size_t visible = kk::DevWifi::scan(entries, kMaxScanEntries);
    const size_t count =
        kk::wifiBuildScanList(entries, visible, entries, kMaxScanEntries);
    wifi.gWifiNetworks.assign(entries, entries + count);
    wifi.gWifiIndex = 0;
    listNote(wifi);
    // A scan can block for a few seconds; keep the idle timers from firing
    // immediately after the list appears.
    state().ui.gLastInputAt = millis();
}

void finishSuccess(WifiState& wifi) {
    const std::string ssid = wifi.gWifiSsid;
    const WifiSetupOrigin origin = wifi.gWifiOrigin;
    clearPassword(wifi);
    wifi.gWifiActive = false;
    wifi.gWifiStep = WifiSetupStep::List;
    wifi.gWifiNetworks.clear();
    wifi.gWifiNote[0] = '\0';
    wifi.gWifiFailure = kk::WifiJoinFailure::None;
    if (origin == WifiSetupOrigin::Boot) {
        drawLoading("Wi-Fi connected", ssid.c_str());
        continueBootAfterStorage();
    } else {
        snprintf(state().ui.gSettingsNote, sizeof state().ui.gSettingsNote, "Wi-Fi: %s",
                 ssid.c_str());
        state().ui.gSettingsNoteColor = kOk;
    }
    state().display.gDirty = true;
}

void attemptJoin(WifiState& wifi) {
    const bool allowEmpty = wifi.gWifiSecurity == kk::WifiSecurity::Open ||
                            wifi.gWifiSecurity == kk::WifiSecurity::Unknown;
    const kk::WifiPasswordStatus check =
        kk::wifiCheckPassword(wifi.gWifiPassword.c_str(), allowEmpty);
    if (check != kk::WifiPasswordStatus::Ok) {
        setNote(wifi, kk::wifiPasswordError(check), kErr);
        wifi.gWifiStep = WifiSetupStep::Password;
        state().display.gDirty = true;
        return;
    }

    wifi.gWifiStep = WifiSetupStep::Joining;
    setNote(wifi, "", kFaint);
    drawLoading("Connecting Wi-Fi...", wifi.gWifiSsid.c_str());
    kk::WifiLinkStatus status = kk::WifiLinkStatus::Other;
    bool timedOut = false;
    const bool joined =
        kk::DevWifi::connectTo(String(wifi.gWifiSsid.c_str()),
                               String(wifi.gWifiPassword.c_str()), 30000, &status,
                               &timedOut);
    state().ui.gLastInputAt = millis();
    if (!joined) {
        wifi.gWifiFailure = kk::wifiClassifyFailure(status, timedOut, wifi.gWifiSecurity);
        setFailureNote(wifi, wifi.gWifiFailure);
        wifi.gWifiStep = kk::wifiSecurityNeedsPassword(wifi.gWifiSecurity)
                             ? WifiSetupStep::Password
                             : WifiSetupStep::List;
        state().display.gDirty = true;
        return;
    }
    if (!kk::DevWifi::saveCredentials(String(wifi.gWifiSsid.c_str()),
                                      String(wifi.gWifiPassword.c_str()))) {
        // Joined, but the next boot would forget the network. Do not leave the
        // user on a connection the device cannot reproduce.
        kk::DevWifi::disconnect();
        wifi.gWifiFailure = kk::WifiJoinFailure::SaveFailed;
        setNote(wifi, "save failed; try again", kErr);
        wifi.gWifiStep = kk::wifiSecurityNeedsPassword(wifi.gWifiSecurity)
                             ? WifiSetupStep::Password
                             : WifiSetupStep::List;
        state().display.gDirty = true;
        return;
    }
    finishSuccess(wifi);
}

void selectNetwork(WifiState& wifi, size_t index) {
    if (index >= wifi.gWifiNetworks.size()) return;
    const kk::WifiNetwork& network = wifi.gWifiNetworks[index];
    if (!kk::wifiSecuritySupported(network.security)) {
        setNote(wifi, "unsupported; use phone setup", kErr);
        state().display.gDirty = true;
        return;
    }
    wifi.gWifiSsid = network.ssid;
    wifi.gWifiSecurity = network.security;
    wifi.gWifiFailure = kk::WifiJoinFailure::None;
    wifi.gWifiShowPassword = false;
    clearPassword(wifi);
    if (!kk::wifiSecurityNeedsPassword(network.security)) {
        // Open network: no password field.
        setNote(wifi, "", kFaint);
        attemptJoin(wifi);
        return;
    }
    wifi.gWifiStep = WifiSetupStep::Password;
    setNote(wifi, "", kFaint);
    state().display.gDirty = true;
}

void closeWifiSetup() {
    WifiState& wifi = state().wifi;
    clearPassword(wifi);
    wifi.gWifiActive = false;
    wifi.gWifiStep = WifiSetupStep::List;
    wifi.gWifiNetworks.clear();
    wifi.gWifiSsid.clear();
    wifi.gWifiManual.clear();
    wifi.gWifiFailure = kk::WifiJoinFailure::None;
    wifi.gWifiNote[0] = '\0';
    state().display.gDirty = true;
}

char typedChar(const KeyEvent& e) {
    return tgc::cardputerCharAt(e.x, e.y, state().ui.gShift);
}

// On the Cardputer the comma, period, slash and quote keys are also the
// arrow keys. The list uses them for navigation; a text field has nothing to
// navigate, so those positions type their printed character instead. Esc and
// Backspace are handled before this, so only the backtick and tilde are left
// to the phone portal.
char fieldChar(const KeyEvent& e) {
    if (e.key != Key::Other && e.key != Key::Up && e.key != Key::Down &&
        e.key != Key::Left && e.key != Key::Right)
        return 0;
    return typedChar(e);
}

}  // namespace

void openWifiSetup(WifiSetupOrigin origin) {
    WifiState& wifi = state().wifi;
    if (wifi.gWifiActive) return;
    clearPassword(wifi);
    wifi.gWifiActive = true;
    wifi.gWifiOrigin = origin;
    wifi.gWifiStep = WifiSetupStep::List;
    wifi.gWifiNetworks.clear();
    wifi.gWifiIndex = 0;
    wifi.gWifiSsid.clear();
    wifi.gWifiManual.clear();
    wifi.gWifiSecurity = kk::WifiSecurity::Unknown;
    wifi.gWifiShowPassword = false;
    wifi.gWifiFailure = kk::WifiJoinFailure::None;
    wifi.gWifiNote[0] = '\0';
    state().ui.gSettingsNote[0] = '\0';
    tgc::board::setDisplayOn(true);
    tgc::board::setBrightness(kk::settingsBrightness());
    state().ui.gDimmed = false;
    state().ui.gScreenOff = false;
    state().ui.gLastInputAt = millis();
    scanNetworks(wifi);
    state().display.gDirty = true;
}

void wifiSetupKey(const KeyEvent& e) {
    if (!e.down) return;
    WifiState& wifi = state().wifi;
    if (!wifi.gWifiActive || wifi.gWifiStep == WifiSetupStep::Joining) return;

    if (wifi.gWifiStep == WifiSetupStep::List) {
        if (e.key == Key::Esc) {
            closeWifiSetup();
            return;
        }
        const int rowCount = static_cast<int>(wifi.gWifiNetworks.size()) + 1;
        if (e.key == Key::Up || e.key == Key::Down) {
            if (rowCount > 0) {
                wifi.gWifiIndex =
                    (wifi.gWifiIndex + (e.key == Key::Down ? 1 : rowCount - 1)) % rowCount;
            }
            wifi.gWifiFailure = kk::WifiJoinFailure::None;
            listNote(wifi);
            state().display.gDirty = true;
            return;
        }
        if (e.key == Key::Enter) {
            if (wifi.gWifiIndex < static_cast<int>(wifi.gWifiNetworks.size())) {
                selectNetwork(wifi, static_cast<size_t>(wifi.gWifiIndex));
            } else {
                wifi.gWifiManual.clear();
                wifi.gWifiFailure = kk::WifiJoinFailure::None;
                wifi.gWifiStep = WifiSetupStep::ManualSsid;
                setNote(wifi, "", kFaint);
                state().display.gDirty = true;
            }
            return;
        }
        if (e.key == Key::Other) {
            const char c = typedChar(e);
            if (c == 'r' || c == 'R') {
                scanNetworks(wifi);
                state().display.gDirty = true;
            }
        }
        return;
    }

    if (wifi.gWifiStep == WifiSetupStep::ManualSsid) {
        if (e.key == Key::Esc) {
            wifi.gWifiStep = WifiSetupStep::List;
            listNote(wifi);
            state().display.gDirty = true;
            return;
        }
        if (e.key == Key::Backspace) {
            if (!wifi.gWifiManual.empty()) wifi.gWifiManual.pop_back();
            setNote(wifi, "", kFaint);
            state().display.gDirty = true;
            return;
        }
        if (e.key == Key::Enter) {
            if (!kk::wifiSsidValid(wifi.gWifiManual.c_str())) {
                setNote(wifi, "Enter a name (1-32 characters)", kErr);
                state().display.gDirty = true;
                return;
            }
            wifi.gWifiSsid = wifi.gWifiManual;
            wifi.gWifiSecurity = kk::WifiSecurity::Unknown;
            wifi.gWifiFailure = kk::WifiJoinFailure::None;
            wifi.gWifiShowPassword = false;
            clearPassword(wifi);
            wifi.gWifiStep = WifiSetupStep::Password;
            setNote(wifi, "8-63 characters; empty for open", kFaint);
            state().display.gDirty = true;
            return;
        }
        const char c = fieldChar(e);
        if (c && wifi.gWifiManual.size() < kk::kWifiSsidMax) {
            wifi.gWifiManual.push_back(c);
            setNote(wifi, "", kFaint);
            state().display.gDirty = true;
        }
        return;
    }

    // Password.
    if (e.key == Key::Esc) {
        clearPassword(wifi);
        wifi.gWifiStep = WifiSetupStep::List;
        wifi.gWifiShowPassword = false;
        wifi.gWifiFailure = kk::WifiJoinFailure::None;
        listNote(wifi);
        state().display.gDirty = true;
        return;
    }
    if (e.key == Key::Tab) {
        wifi.gWifiShowPassword = !wifi.gWifiShowPassword;
        state().display.gDirty = true;
        return;
    }
    if (e.key == Key::Backspace) {
        if (!wifi.gWifiPassword.empty()) wifi.gWifiPassword.pop_back();
        wifi.gWifiFailure = kk::WifiJoinFailure::None;
        setNote(wifi, "", kFaint);
        state().display.gDirty = true;
        return;
    }
    if (e.key == Key::Enter) {
        attemptJoin(wifi);
        return;
    }
    const char c = fieldChar(e);
    if (c && wifi.gWifiPassword.size() < kk::kWifiPasswordMax) {
        wifi.gWifiPassword.push_back(c);
        wifi.gWifiFailure = kk::WifiJoinFailure::None;
        setNote(wifi, "", kFaint);
        state().display.gDirty = true;
    }
}

void drawWifiSetup() {
    if (!state().display.gDisp) return;
    const WifiState& wifi = state().wifi;

    WifiSetupView view;
    switch (wifi.gWifiStep) {
        case WifiSetupStep::ManualSsid:
            view.panel = WifiSetupPanel::ManualSsid;
            view.text = wifi.gWifiManual;
            view.note = wifi.gWifiNote;
            view.noteColor = wifi.gWifiNoteColor;
            view.help = "Enter: next  Esc: back";
            break;
        case WifiSetupStep::Password:
            view.panel = WifiSetupPanel::Password;
            view.ssid = wifi.gWifiSsid;
            view.showPassword = wifi.gWifiShowPassword;
            {
                char masked[kk::kWifiPasswordMax + 1];
                kk::wifiMaskText(wifi.gWifiPassword.c_str(), wifi.gWifiShowPassword,
                                 masked, sizeof masked);
                view.text = masked;
            }
            view.note = wifi.gWifiNote;
            view.noteColor = wifi.gWifiNoteColor;
            if (view.note.empty()) {
                view.note = wifi.gWifiSecurity == kk::WifiSecurity::Unknown
                                ? "8-63 characters; empty for open"
                                : "8-63 characters";
                view.noteColor = kFaint;
            }
            view.help = "Enter: connect  Esc: back";
            break;
        case WifiSetupStep::Joining:
        case WifiSetupStep::List:
        default:
            view.panel = WifiSetupPanel::List;
            view.rows.reserve(wifi.gWifiNetworks.size() + 1);
            for (const kk::WifiNetwork& network : wifi.gWifiNetworks) {
                WifiListRowView row;
                row.ssid = network.ssid;
                row.signal = kk::wifiSignalLabel(network.rssi);
                row.security = kk::wifiSecurityLabel(network.security);
                view.rows.push_back(std::move(row));
            }
            {
                WifiListRowView manual;
                manual.ssid = "+ Enter network name";
                manual.manual = true;
                view.rows.push_back(std::move(manual));
            }
            view.selectedIndex = wifi.gWifiIndex;
            if (view.selectedIndex < 0 ||
                view.selectedIndex >= static_cast<int>(view.rows.size()))
                view.selectedIndex = static_cast<int>(view.rows.size()) - 1;
            view.note = wifi.gWifiNote;
            view.noteColor = wifi.gWifiNoteColor;
            view.help = "Enter: choose  R: rescan";
            break;
    }

    auto& d = target();
    d.startWrite();
    drawWifiSetupScreen(d, view);
    d.endWrite();
    present();
}

}  // namespace kk::app
