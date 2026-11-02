#include "abpm/core/progress_emitter.hpp"
#include <iostream>
#include <algorithm>

namespace abpm::core {

void ProgressEmitter::init(bool enabled) {
    s_enabled = enabled;
}

bool ProgressEmitter::is_enabled() {
    return s_enabled;
}

void ProgressEmitter::emit(int percent, const std::string& message) {
    if (!s_enabled) return;
    int clamped = std::clamp(percent, 0, 100);
    // std::endl garantit le flush immédiat du buffer stdout pour Blue Hub
    std::cout << "PROGRESS:" << clamped << ":" << message << std::endl;
}

void ProgressEmitter::emit_error(const std::string& message) {
    if (!s_enabled) return;
    std::cout << "PROGRESS:ERROR:" << message << std::endl;
}

} // namespace abpm::core