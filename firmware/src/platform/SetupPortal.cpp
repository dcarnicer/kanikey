// Setup portal implementation; see SetupPortal.h. The Wi-Fi/token/time flow is
// adapted from Tangocho's WifiPortal (MIT), reduced to KaniKey setup.
#include "platform/SetupPortal.h"

#include <WiFi.h>
#include <esp_mac.h>
#include <sys/time.h>

#include <cctype>
#include <cstring>
#include <ctime>

#include "platform/DeviceSettings.h"
#include "platform/DevToken.h"
#include "platform/DevWifi.h"

namespace kk {

namespace {

constexpr size_t kMaxNearby = 12;
constexpr int64_t kMinPlausibleMs = 1767225600000LL;  // 2026-01-01T00:00:00Z
constexpr int64_t kMaxPlausibleMs = 4102444800000LL;  // 2100-01-01T00:00:00Z
constexpr uint32_t kLeaveGraceMs = 1200;

String htmlEscape(const String& in) {
    String out;
    out.reserve(in.length() + 8);
    for (size_t i = 0; i < in.length(); ++i) {
        switch (in[i]) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += in[i]; break;
        }
    }
    return out;
}

String sanitizeZone(const String& in) {
    if (in.length() > 40) return String();
    for (size_t i = 0; i < in.length(); ++i) {
        const char c = in[i];
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == '/' || c == '_' || c == '+' || c == '-';
        if (!ok) return String();
    }
    return in;
}

bool parseDecimal(const String& in, int64_t* out) {
    const size_t n = in.length();
    if (n == 0 || n > 15) return false;
    size_t i = 0;
    bool neg = false;
    if (in[0] == '-') {
        neg = true;
        i = 1;
    }
    if (i >= n) return false;
    int64_t v = 0;
    for (; i < n; ++i) {
        const char c = in[i];
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    *out = neg ? -v : v;
    return true;
}

bool parseOffset(const String& in, int* out) {
    int64_t v = 0;
    if (!parseDecimal(in, &v) || v < -840 || v > 840) return false;
    *out = static_cast<int>(v);
    return true;
}

String localClockText() {
    const time_t t = time(nullptr);
    struct tm tm {};
    if (!localtime_r(&t, &tm)) return String("--");
    char buf[32];
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
    return String(buf);
}

// tzr(now): the browser's time zone as a POSIX TZ rule, from Tangocho's portal
// (MIT). It finds the current year's offset changes and writes them as POSIX
// Mm.w.d/time rules, with a fixed rule when there is no DST.
constexpr char kTzRuleJs[] = R"JS(function tzr(now){var D=864e5,M=6e4,y=new Date(now).getUTCFullYear(),a,c,F=[],i,s,d,u;
function off(t){return -new Date(t).getTimezoneOffset()}
function p2(n){return(n<10?'0':'')+n}
function nm(o){var a=Math.abs(o);return'<'+(o<0?'-':'+')+p2(a/60|0)+(a%60?p2(a%60):'')+'>'}
function po(o){var a=Math.abs(o);return(o>0?'-':'')+(a/60|0)+(a%60?':'+p2(a%60):'')}
function tr(y){var r=[],t=Date.UTC(y,0,1),b=Date.UTC(y+1,0,1),o=off(t),q,l,h,m;
for(;t<b;t+=D){q=off(t+D);if(q!=o){l=t;h=t+D;while(h-l>M){m=l+Math.floor((h-l)/2/M)*M;if(off(m)==o)l=m;else h=m}r.push([h,o,q]);o=q}}return r}
function at(r,y,o){var f=new Date(Date.UTC(y,r[0],1)).getUTCDay(),n=new Date(Date.UTC(y,r[0]+1,0)).getUTCDate(),d=1+(r[2]-f+7)%7+(r[1]-1)*7;
while(d>n)d-=7;return Date.UTC(y,r[0],d)+r[3]*1e3-o*M}
function tm(s){return s==7200?'':'/'+(s<0?'-':'')+(Math.abs(s)/3600|0)+(s%3600?':'+p2(Math.abs(s)/60%60|0):'')}
a=tr(y);c=off(now);
if(a.length!=2||a[0][1]!=a[1][2]||a[0][2]!=a[1][1])return nm(c)+po(c);
for(i=1;i<6;i++)F.push(tr(y+i));
s=Math.min(a[0][1],a[0][2]);d=s==a[0][1]?a[0][2]:a[0][1];u=nm(s)+po(s)+nm(d)+(d-s==60?'':po(d));
[s,d].forEach(function(o){var x=a[0][1]==o?a[0]:a[1],L=x[0]+o*M,b,top=-1;
[0,-1,1].forEach(function(k){var l=new Date(L+k*D),m=l.getUTCMonth(),e=l.getUTCDate(),
n=new Date(Date.UTC(l.getUTCFullYear(),m+1,0)).getUTCDate(),w=[Math.floor((e-1)/7)+1];
if(e+7>n)w.unshift(5);
w.forEach(function(v){var r=[m,v,l.getUTCDay(),L%D/1e3-k*86400],h=0;
F.forEach(function(f,j){f.forEach(function(z){if(z[1]==o&&z[0]==at(r,y+j+1,o))h++})});
if(h>top){top=h;b=r}})});
u+=',M'+(b[0]+1)+'.'+b[1]+'.'+b[2]+tm(b[3])});
return u}
)JS";

const char kPageStyle[] =
    "<style>body{font-family:system-ui,-apple-system,sans-serif;margin:1rem;max-width:44rem;"
    "line-height:1.45}h1{font-size:1.3rem;margin:0 0 .3rem}h1 small{font-size:.7em;font-weight:400;"
    "color:#666}h2{font-size:1.05rem;margin:1.2rem 0 .3rem}table{border-collapse:collapse}"
    "th,td{text-align:left;padding:.2rem .6rem .2rem 0;vertical-align:top}"
    "input[type=text],input[type=password]{width:100%;max-width:20rem;padding:.35rem;margin:.15rem 0}"
    "button{padding:.45rem .85rem}button.main{font-size:1.05rem;padding:.6rem 1.1rem}"
    ".muted{color:#666}.ok{color:#070}.err{color:#b00}code{background:#eee;padding:0 .2rem}"
    "details{margin:.7rem 0}summary{cursor:pointer;font-weight:600}footer{margin-top:1.6rem;"
    "padding-top:.4rem;border-top:1px solid #ccc;font-size:.8rem;color:#666}</style>";

}  // namespace

String SetupPortal::packStatus() const {
    return hooks_.packStatus ? hooks_.packStatus() : String("unknown");
}

bool SetupPortal::beginAp(const SetupPortalHooks& hooks, const String& notice) {
    if (active_) end(false);
    hooks_ = hooks;
    mode_ = Mode::Ap;
    leave_ = Leave::None;
    tokenValidated_ = false;
    tokenUser_ = "";
    tokenLevel_ = 0;
    timeSet_ = false;
    timeText_ = "";
    nearby_.clear();

    // Scan before starting the AP: the page offers nearby SSIDs.
    WiFi.mode(WIFI_STA);
    const int n = WiFi.scanNetworks();
    for (int i = 0; i < n && nearby_.size() < kMaxNearby; ++i) {
        const String name = WiFi.SSID(i);
        if (name.isEmpty() || name.length() > 32) continue;
        bool duplicate = false;
        for (const String& seen : nearby_)
            if (seen == name) {
                duplicate = true;
                break;
            }
        if (!duplicate) nearby_.push_back(name);
    }
    WiFi.scanDelete();

    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(ssid_, sizeof ssid_, "KaniKey-%02X%02X", mac[4], mac[5]);
    snprintf(apPassword_, sizeof apPassword_, "kani%02X%02X%02X%02X", mac[2], mac[3], mac[4],
             mac[5]);
    snprintf(stationIp_, sizeof stationIp_, "%s", "");
    snprintf(url_, sizeof url_, "http://192.168.4.1/");

    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(ssid_, apPassword_)) {
        snprintf(message_, sizeof message_, "Could not start the setup access point");
        WiFi.mode(WIFI_OFF);
        return false;
    }
    if (!startServer(hooks)) {
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
        return false;
    }
    if (notice.length()) snprintf(message_, sizeof message_, "%s", notice.c_str());
    Serial.printf("[setup] AP \"%s\" at %s\n", ssid_, url_);
    return true;
}

bool SetupPortal::beginStation(const SetupPortalHooks& hooks, const String& notice) {
    if (active_) end(false);
    if (WiFi.status() != WL_CONNECTED) {
        snprintf(message_, sizeof message_, "Wi-Fi is not connected");
        return false;
    }
    hooks_ = hooks;
    mode_ = Mode::Station;
    leave_ = Leave::None;
    tokenValidated_ = false;
    tokenUser_ = "";
    tokenLevel_ = 0;
    timeSet_ = false;
    timeText_ = "";
    snprintf(ssid_, sizeof ssid_, "%s", DevWifi::savedSsid().c_str());
    snprintf(stationIp_, sizeof stationIp_, "%s", WiFi.localIP().toString().c_str());
    snprintf(url_, sizeof url_, "http://%s/", stationIp_);
    if (!startServer(hooks)) return false;
    if (notice.length()) snprintf(message_, sizeof message_, "%s", notice.c_str());
    Serial.printf("[setup] station portal on \"%s\" at %s\n", ssid_, url_);
    return true;
}

bool SetupPortal::startServer(const SetupPortalHooks& hooks) {
    if (active_ || server_) return false;
    hooks_ = hooks;
    server_ = new WebServer(80);
    if (!server_) {
        snprintf(message_, sizeof message_, "Not enough memory for the setup portal");
        return false;
    }
    registerRoutes();
    server_->begin();
    active_ = true;
    lastActivity_ = millis();
    return true;
}

void SetupPortal::registerRoutes() {
    server_->on("/", HTTP_GET, [this]() { touch(); handleRoot(); });
    server_->on("/wifi", HTTP_POST, [this]() { touch(); handleWifi(); });
    server_->on("/token", HTTP_POST, [this]() { touch(); handleToken(); });
    server_->on("/pack", HTTP_POST, [this]() { touch(); handlePack(); });
    server_->on("/time", HTTP_POST, [this]() { touch(); handleTime(); });
    server_->on("/finish", HTTP_POST, [this]() { touch(); handleFinish(); });
    server_->on("/favicon.ico", HTTP_GET, [this]() { server_->send(204); });
    server_->onNotFound([this]() { touch(); handleNotFound(); });
}

void SetupPortal::poll() {
    if (!active_ || !server_) return;
    server_->handleClient();
    if (leave_ != Leave::None && millis() - leaveAt_ >= kLeaveGraceMs) {
        const Leave what = leave_;
        leave_ = Leave::None;
        if (what == Leave::SwitchStation) {
            Serial.println("[setup] switching the portal to station mode");
            if (hooks_.onSwitchToStation) hooks_.onSwitchToStation();
        } else {
            Serial.println("[setup] finishing setup");
            if (hooks_.onFinish) hooks_.onFinish();
        }
    }
}

void SetupPortal::end(bool keepStation) {
    if (server_) {
        server_->stop();
        delete server_;
        server_ = nullptr;
    }
    if (mode_ == Mode::Ap) {
        WiFi.softAPdisconnect(true);
        if (keepStation)
            WiFi.mode(WIFI_STA);
        else
            WiFi.mode(WIFI_OFF);
    } else if (!keepStation) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }
    active_ = false;
    leave_ = Leave::None;
    server_ = nullptr;
    Serial.println("[setup] portal stopped");
}

String SetupPortal::timeScript() const {
    String s = "<script>";
    s += kTzRuleJs;
    s += R"JS((function(){var d=new Date(),z='',r='';
try{z=Intl.DateTimeFormat().resolvedOptions().timeZone||''}catch(_){}
try{r=tzr(d.getTime())}catch(_){}
var x=new XMLHttpRequest();x.open('POST','/time');
x.setRequestHeader('Content-Type','application/x-www-form-urlencoded');
x.onload=function(){var e=document.getElementById('clock');if(e)e.textContent=x.responseText};
x.send('ms='+d.getTime()+'&tz='+(-d.getTimezoneOffset())+'&zone='+encodeURIComponent(z)+
'&tzr='+encodeURIComponent(r))})();)JS";
    s += "</script>";
    return s;
}

void SetupPortal::sendPage(const String& notice, bool ok, int code) {
    if (!server_) return;
    if (notice.length()) snprintf(message_, sizeof message_, "%s", notice.c_str());

    const String pack = htmlEscape(packStatus());
    const String zone = htmlEscape(settingsTimeZoneName().length() ? settingsTimeZoneName()
                                                                   : settingsTimeZoneRule());
    String h;
    h.reserve(7000);
    h += "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">";
    h += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
    h += "<title>KaniKey setup</title>";
    h += kPageStyle;
    h += "</head><body><h1>KaniKey <small>setup</small></h1>";

    if (mode_ == Mode::Ap) {
        h += "<p class=\"muted\">Setup access point</p><table>";
        h += "<tr><th>Wi-Fi</th><td><b>";
        h += htmlEscape(ssid_);
        h += "</b></td></tr><tr><th>Password</th><td><code>";
        h += htmlEscape(apPassword_);
        h += "</code></td></tr><tr><th>Open</th><td><a href=\"";
        h += url_;
        h += "\">";
        h += url_;
        h += "</a></td></tr></table>";
        h += "<p>Join this network from your phone or computer, then open the address above.</p>";
    } else {
        h += "<p class=\"muted\">Connected</p><table>";
        h += "<tr><th>Wi-Fi</th><td><b>";
        h += htmlEscape(ssid_);
        h += "</b></td></tr><tr><th>Address</th><td><a href=\"";
        h += url_;
        h += "\">";
        h += url_;
        h += "</a></td></tr></table>";
    }

    if (notice.length()) {
        h += ok ? "<p class=\"ok\">" : "<p class=\"err\">";
        h += htmlEscape(notice);
        h += "</p>";
    }

    h += "<h2>Visual pack</h2><p class=\"muted\">";
    h += pack;
    h += "</p><form method=\"post\" action=\"/pack\"><button type=\"submit\">Check pack again</button></form>";

    h += "<h2>Time</h2><p class=\"muted\">Zone: <code>";
    h += zone;
    h += "</code> &middot; <span id=\"clock\">";
    h += timeSet_ ? htmlEscape(timeText_) : String("waiting for this browser");
    h += "</span></p><p class=\"muted\">The page sets the clock and time zone from this browser "
         "automatically.</p>";

    if (mode_ == Mode::Ap) {
        h += "<h2 id=\"wifi\">Home Wi-Fi</h2>";
        h += "<form method=\"post\" action=\"/wifi\"><input type=\"text\" name=\"ssid\" list=\"ssids\" "
             "maxlength=\"32\" placeholder=\"Network name\" autocomplete=\"off\" required><br>";
        h += "<input type=\"password\" name=\"pass\" maxlength=\"63\" placeholder=\"Password\" "
             "autocomplete=\"new-password\"> <button type=\"submit\">Save and connect</button></form>";
        if (!nearby_.empty()) {
            h += "<datalist id=\"ssids\">";
            for (const String& name : nearby_) {
                h += "<option value=\"";
                h += htmlEscape(name);
                h += "\">";
            }
            h += "</datalist>";
        }
        h += "<p class=\"muted\">The device will join this network. This setup network will close; "
             "reconnect your phone to the same network and open the address shown on the device.</p>";
        h += "<h2>WaniKani token</h2><p class=\"muted\">The token is entered on the next page, after "
             "the device joins your Wi-Fi, so it can be validated with GET /user.</p>";
    } else {
        h += "<h2 id=\"token\">WaniKani token</h2>";
        if (tokenValidated_) {
            h += "<p class=\"ok\">Valid token for <b>";
            h += htmlEscape(tokenUser_);
            h += "</b> (level ";
            h += String(tokenLevel_);
            h += ").</p>";
        } else if (DevToken::has()) {
            h += "<p class=\"muted\">A token is already stored. Validate it again or replace it.</p>";
        }
        h += "<form method=\"post\" action=\"/token\"><input type=\"password\" name=\"token\" "
             "maxlength=\"128\" placeholder=\"WaniKani API token\" autocomplete=\"off\" required> "
             "<button type=\"submit\">Validate and save</button></form>";
        h += "<p class=\"muted\">The token is stored in NVS, never logged and only sent to "
             "api.wanikani.com.</p>";

        h += "<details><summary>Change Wi-Fi</summary>";
        h += "<form method=\"post\" action=\"/wifi\"><input type=\"text\" name=\"ssid\" maxlength=\"32\" "
             "placeholder=\"Network name\" required><br><input type=\"password\" name=\"pass\" "
             "maxlength=\"63\" placeholder=\"Password\" autocomplete=\"new-password\"> ";
        h += "<button type=\"submit\">Save and reconnect</button></form></details>";

        h += "<h2>Finish</h2><form method=\"post\" action=\"/finish\">";
        h += "<button type=\"submit\" class=\"main\">Start KaniKey</button></form>";
        h += "<p class=\"muted\">The token is validated once more before the setup closes.</p>";
    }

    h += timeScript();
    h += "<footer>KaniKey is an unofficial WaniKani client. This page is served locally by the "
         "device.</footer></body></html>";
    server_->send(code, "text/html", h);
}

void SetupPortal::handleRoot() { sendPage("", true); }

void SetupPortal::handleWifi() {
    String ssid = server_->arg("ssid");
    String pass = server_->arg("pass");
    ssid.trim();
    if (ssid.isEmpty() || ssid.length() > 32) {
        sendPage("Enter a network name (1-32 characters).", false, 400);
        return;
    }
    if (!DevWifi::saveCredentials(ssid, pass)) {
        sendPage("Could not save that network. Check the password (8-63 characters or empty for "
                 "an open network).",
                 false, 400);
        return;
    }
    leave_ = Leave::SwitchStation;
    leaveAt_ = millis();
    String notice = "Saved. Connecting to \"" + ssid + "\".";
    if (mode_ == Mode::Ap)
        notice += " This setup network will close; reconnect your phone to the same network and "
                  "open the address shown on the device.";
    else
        notice += " The device will reconnect and serve this page on the new network.";
    sendPage(notice, true);
}

void SetupPortal::handleToken() {
    if (mode_ != Mode::Station) {
        sendPage("Connect the device to Wi-Fi first.", false, 400);
        return;
    }
    String token = server_->arg("token");
    token.trim();
    if (token.isEmpty() || token.length() > 128) {
        sendPage("Enter a WaniKani API token (1-128 characters).", false, 400);
        return;
    }
    if (!hooks_.validateToken) {
        sendPage("Token validation is not available.", false, 500);
        return;
    }
    String user;
    String error;
    int level = 0;
    if (!hooks_.validateToken(token, &user, &level, &error)) {
        sendPage(error.length() ? error : String("The token was rejected."), false, 400);
        return;
    }
    if (!DevToken::save(token)) {
        sendPage("Valid token, but it could not be saved.", false, 500);
        return;
    }
    tokenValidated_ = true;
    tokenUser_ = user;
    tokenLevel_ = level;
    String notice = "Token valid for " + user + " (level " + String(level) + ").";
    sendPage(notice, true);
}

void SetupPortal::handlePack() {
    if (!hooks_.recheckPack) {
        sendPage("Pack check is not available.", false, 500);
        return;
    }
    const bool ok = hooks_.recheckPack();
    String notice = ok ? String("Visual pack ready: ") + packStatus()
                       : String("Visual pack not found: ") + packStatus();
    sendPage(notice, ok);
}

void SetupPortal::handleTime() {
    int64_t ms = 0;
    int offset = 0;
    if (!parseDecimal(server_->arg("ms"), &ms) || ms < kMinPlausibleMs || ms > kMaxPlausibleMs ||
        !parseOffset(server_->arg("tz"), &offset)) {
        server_->send(400, "text/plain", "bad time");
        return;
    }
    String zone = sanitizeZone(server_->arg("zone"));
    String rule = server_->arg("tzr");
    if (rule.length() > kTimeZoneRuleMax || !settingsSaveTimeZone(rule, zone)) {
        rule = formatFixedTimeZone(offset);
        if (!settingsSaveTimeZone(rule, zone)) {
            server_->send(500, "text/plain", "bad zone");
            return;
        }
    }
    struct timeval tv {};
    tv.tv_sec = static_cast<time_t>(ms / 1000);
    tv.tv_usec = static_cast<suseconds_t>((ms % 1000) * 1000);
    if (settimeofday(&tv, nullptr) != 0) {
        server_->send(500, "text/plain", "clock failed");
        return;
    }
    timeSet_ = true;
    timeText_ = localClockText();
    Serial.printf("[setup] clock from browser: %s, zone %s\n", timeText_.c_str(),
                  zone.length() ? zone.c_str() : rule.c_str());
    server_->send(200, "text/plain", timeText_);
}

void SetupPortal::handleFinish() {
    if (mode_ != Mode::Station) {
        sendPage("Connect the device to Wi-Fi first.", false, 400);
        return;
    }
    if (!tokenValidated_) {
        const String token = DevToken::load();
        if (token.isEmpty()) {
            sendPage("Enter a WaniKani token first.", false, 400);
            return;
        }
        if (!hooks_.validateToken) {
            sendPage("Token validation is not available.", false, 500);
            return;
        }
        String user;
        String error;
        int level = 0;
        if (!hooks_.validateToken(token, &user, &level, &error)) {
            sendPage(error.length() ? error : String("The token was rejected."), false, 400);
            return;
        }
        tokenValidated_ = true;
        tokenUser_ = user;
        tokenLevel_ = level;
    }
    leave_ = Leave::Finish;
    leaveAt_ = millis();
    sendPage("Setup complete. Starting KaniKey...", true);
}

void SetupPortal::handleNotFound() { server_->send(404, "text/plain", "not found"); }

}  // namespace kk
