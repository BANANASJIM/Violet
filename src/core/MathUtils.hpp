#pragma once

#include <cmath>

namespace violet {

// Custom isfinite implementation without std
template<typename T>
inline bool isfinite(T value) {
    return !std::isnan(value) && !std::isinf(value);
}

} // namespace violet