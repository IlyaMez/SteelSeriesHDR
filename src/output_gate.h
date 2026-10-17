#pragma once
#include "color.h"
namespace arena {
// Retry state is committed only after a successful USB send.
class OutputGate {
    Colors last_{};
    double sentAt_{};
    bool sent_ = false;
public:
    bool needsSend(const Colors& colors, double seconds, bool repeatHeld = false) const {
        if (!sent_) return true;
        double elapsed = seconds - sentAt_;
        // The physical speaker test uses 10 Hz. Coalesce live targets to that same
        // cadence instead of repeatedly restarting device effects at up to 30 Hz.
        return elapsed >= .1 && (repeatHeld || colors != last_ || elapsed >= 1.0);
    }
    void sent(const Colors& colors, double seconds) { last_ = colors; sentAt_ = seconds; sent_ = true; }
};
}
