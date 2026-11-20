// PD-02: e-paper present and refresh policy. See EpdRefreshPolicy.h.
#include "wk/EpdRefreshPolicy.h"

namespace kk::epd {
namespace {

// Unsigned subtraction: millis() wraps after ~49 days and the elapsed time
// stays right across the wrap.
uint32_t elapsed(uint32_t now, uint32_t then) { return now - then; }

}  // namespace

void markDirty(Presenter* p, uint32_t now) {
    if (!p) return;
    if (!p->pending) {
        p->pending = true;
        p->pendingSinceMs = now;
    }
    p->lastDirtyMs = now;
}

Decision decide(const Presenter& p, uint32_t now, bool busy, const Config& cfg, bool force) {
    Decision d;
    if (busy) return d;
    if (!force) {
        if (!p.pending) return d;
        if (p.presentedOnce) {
            if (elapsed(now, p.lastPresentMs) < cfg.floorMs) return d;
            const bool settled = elapsed(now, p.lastDirtyMs) >= cfg.settleMs;
            const bool deferred = elapsed(now, p.pendingSinceMs) >= cfg.maxDeferMs;
            if (!settled && !deferred) return d;
        }
    }
    d.action = Action::Present;
    d.full = !p.presentedOnce || p.partials >= cfg.fullEvery;
    d.mode = d.full ? Mode::Quality : Mode::Text;
    return d;
}

void recordPresent(Presenter* p, const Decision& d, uint32_t now) {
    if (!p) return;
    p->pending = false;
    p->presentedOnce = true;
    p->lastPresentMs = now;
    p->partials = d.full ? 0 : p->partials + 1;
}

}  // namespace kk::epd
