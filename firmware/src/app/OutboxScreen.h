#pragma once
// Outbox screen: the entire Outbox frame, drawn from explicit view data. The
// app adapter fills OutboxView from its state and keeps startWrite()/present().
#include <cstdint>
#include <string>
#include <vector>

#include "board/Board.h"
#include "ui/Theme.h"

namespace kk::app {

enum class OutboxReasonKind : uint8_t { Faint, Err, Warn };

struct OutboxRowView {
    std::string label;
    std::string reason;
    OutboxReasonKind reasonKind = OutboxReasonKind::Faint;
};

struct OutboxView {
    unsigned total = 0;
    std::vector<OutboxRowView> rows;
    int selectedIndex = -1;
    bool confirm = false;
    std::string confirmLabel;
    std::string confirmReason;
    std::string note;
};

// Draws the Outbox frame into d. The caller owns startWrite()/endWrite() and
// present(); this function only draws.
void drawOutboxScreen(lgfx::LovyanGFX& d, const OutboxView& view);

}  // namespace kk::app
