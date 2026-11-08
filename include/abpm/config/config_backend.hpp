#pragma once

#include <string>

namespace abpm::config {

class ConfigBackend {
public:
    // Récupère une valeur de configuration (ex: "auto-update")
    static std::string get(const std::string& key);

    // Définit une valeur de configuration
    static bool set(const std::string& key, const std::string& value);
};

} // namespace abpm::config