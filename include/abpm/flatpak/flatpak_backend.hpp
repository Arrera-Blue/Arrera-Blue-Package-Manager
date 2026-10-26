#pragma once

#include "abpm/core/package_info.hpp"
#include <vector>

namespace abpm::flatpak {

class FlatpakBackend {
public:
    // Récupère la liste des applications Flatpak installées (système + utilisateur)
    static std::vector<core::PackageInfo> list_installed();
};

} // namespace abpm::flatpak