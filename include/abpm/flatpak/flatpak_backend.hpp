#pragma once

#include "abpm/core/package_info.hpp"
#include <string_view>
#include <vector>

namespace abpm::flatpak {

class FlatpakBackend {
public:
    static std::vector<core::PackageInfo> list_installed();
    static std::vector<core::PackageInfo> search(std::string_view term);

    // Vérifie les mises à jour disponibles pour les Flatpaks
    static std::vector<core::UpdateInfo> check_updates();
};

} // namespace abpm::flatpak