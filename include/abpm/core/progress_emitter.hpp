#pragma once

#include <string>

namespace abpm::core {

class ProgressEmitter {
public:
    // Active ou désactive le mode flux de progression
    static void init(bool enabled);
    static void set_enabled(bool enabled) { init(enabled); }
    static bool is_enabled();

    // Émet : PROGRESS:<POURCENTAGE>:<MESSAGE> avec flush immédiat
    static void emit(int percent, const std::string& message);

    // Émet : PROGRESS:ERROR:<MESSAGE> avec flush immédiat
    static void emit_error(const std::string& message);

private:
    static inline bool s_enabled = false;
};

} // namespace abpm::core