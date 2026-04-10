#include "dsp/window.h"

#include <cmath>

namespace fim::dsp {

std::vector<float> HannWindow(std::size_t length) {
    if (length == 0) {
        return {};
    }
    if (length == 1) {
        return {0.0f};
    }
    std::vector<float> w(length);
    const float denom = static_cast<float>(length - 1);
    constexpr float kTwoPi = 6.28318530717958647692f;
    for (std::size_t n = 0; n < length; ++n) {
        w[n] = 0.5f * (1.0f - std::cos(kTwoPi * static_cast<float>(n) / denom));
    }
    return w;
}

}  // namespace fim::dsp
